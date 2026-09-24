#include "field/field_systems.h"
#include "field/placement_document.h"
#include "scene/environment.h"
#include "field/encounter_document.h"
#include "scene/model_decoder.h"
#include "scene/player_assets.h"
#include "field/area.h"
#include "formats/compression.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
namespace studio {
Environment load_environment(const std::filesystem::path &dump, std::size_t area,
                             std::atomic_bool *cancel, const ArchiveSources &archives,
                             const std::map<std::size_t, Bytes> &working_members) {
    auto start = std::chrono::steady_clock::now();
    ModelDecoder l{{}, cancel};
    l.out.archive_sources = archives;
    Archive field(archives.resolve(dump, GameProfile::field_archive(dump)));
    require(field.size() % TargetProfile::area_stride == 0, "Invalid field archive layout");
    require(area < field.size() / TargetProfile::area_stride, "Field area is out of range");
    l.note("Field archive: " + archives.resolve(dump, GameProfile::field_archive(dump)).string());
    l.note("Terrain resources archive: " +
           archives.resolve(dump, TargetProfile::terrain_archive).string());
    auto member = [&](std::size_t slot) {
        l.checkpoint();
        auto index = area * TargetProfile::area_stride + slot;
        auto working = working_members.find(index);
        auto b = working == working_members.end() ? field.decoded(index) : working->second;
        l.out.source_bytes += b.size();
        return b;
    };
    try {
        auto catalog = load_map_catalog(dump, archives);
        for (auto &location : catalog.locations)
            if (location.area == int(area))
                l.out.locations.push_back(std::move(location));
    } catch (const std::exception &e) {
        l.note("Map start unavailable: " + std::string(e.what()));
    }
    try {
        l.out.lighting_contexts =
            load_area_lighting(dump, area, member(TargetProfile::environment_slot));
    } catch (const std::exception &e) {
        l.note("Game lighting unavailable: " + std::string(e.what()));
    }
    try {
        l.out.weather_particles = load_weather_particles(dump, l.out.diagnostics);
    } catch (const std::exception &e) {
        l.note("Weather particles: " + std::string(e.what()));
    }
    try {
        decode_camera_regions(l.out.spatial, member(TargetProfile::camera_slot));
    } catch (const std::exception &e) {
        l.note("Camera regions: " + std::string(e.what()));
    }
    try {
        Archive resident(dump / TargetProfile::resident_archive);
        auto resources = Container::parse(resident.decoded(0), "FR");
        l.out.spatial.defaults =
            decode_camera_settings(resources.files.at(TargetProfile::camera_defaults));
        l.out.spatial.support_defaults =
            decode_camera_settings(resources.files.at(TargetProfile::camera_support_defaults));
        Archive zones(dump / TargetProfile::zone_archive);
        auto table = zones.decoded(0);
        for (auto &location : l.out.locations)
            if (location.zone >= 0) {
                auto z = slice(table, std::size_t(location.zone) * 84, 84);
                auto binding = decode_zone_camera(z, location.zone);
                require(binding.camera < l.out.spatial.defaults.size() &&
                            binding.support < l.out.spatial.support_defaults.size(),
                        "Zone references a missing camera default");
                l.out.spatial.zones.push_back(binding);
            }
    } catch (const std::exception &e) {
        l.note("Zone camera defaults: " + std::string(e.what()));
    }
    try {
        Archive zones(dump / TargetProfile::zone_archive),
            worlds(dump / TargetProfile::world_archive);
        auto mapping = zones.decoded(1);
        std::map<unsigned, std::map<unsigned, int>> bindings;
        for (auto &location : l.out.locations)
            if (location.zone >= 0) {
                bindings[u16(mapping, std::size_t(location.zone) * 2)][unsigned(location.zone)] =
                    location.zone;
            }
        for (auto &[world, ids] : bindings)
            decode_zone_regions(l.out.spatial, worlds.decoded(world), unsigned(area), ids);
    } catch (const std::exception &e) {
        l.note("Zone boundaries: " + std::string(e.what()));
    }
    auto background = Container::parse(member(TargetProfile::background_resource_slot), "BG");
    require(background.files.size() == 8, "Unsupported area background pack");
    {
        auto textures = Container::parse(background.files[0], "BG");
        for (unsigned i = 0; i < textures.files.size(); ++i)
            if (!textures.files[i].empty()) {
                auto &t = textures.files[i];
                l.texture(t, "terrain/");
                l.out.texture_sources["terrain/" + text(slice(t, 40, 64))] = {
                    GameProfile::field_archive(dump),
                    area * TargetProfile::area_stride + TargetProfile::background_resource_slot,
                    {0, i}};
            }
    }
    if (!background.files[1].empty()) {
        auto shaders = Container::parse(background.files[1], "BG");
        for (unsigned i = 0; i < shaders.files.size(); ++i) {
            auto &shader = shaders.files[i];
            ++l.out.shader_resources;
            if (!shader.empty()) {
                l.shader(shader, "terrain/");
                l.out.shader_sources["terrain/" + decode_material_shader(shader).name] = {
                    GameProfile::field_archive(dump),
                    area * TargetProfile::area_stride + TargetProfile::background_resource_slot,
                    {1, i}};
            }
        }
    }
    for (auto slot : {TargetProfile::local_material_motion, TargetProfile::daily_material_motion})
        if (!background.files[slot].empty()) {
            auto motions = Container::parse(background.files[slot], "BG");
            if (motions.files.size() != 1)
                l.note("Area has multiple material motions; using the first");
            if (!motions.files.empty())
                l.motion(
                    motions.files.front(),
                    slot == TargetProfile::local_material_motion ? "Area loop" : "Area daily",
                    "terrain/", slot == TargetProfile::daily_material_motion,
                    std::make_shared<SceneModelSource>(SceneModelSource{
                        GameProfile::field_archive(dump),
                        area * TargetProfile::area_stride + TargetProfile::background_resource_slot,
                        {slot, 0}}));
        }
    auto layout = Container::parse(member(TargetProfile::terrain_layout_slot), "TR");
    require(layout.files.size() == 1, "Unsupported terrain layout variants");
    auto &grid = layout.files[0];
    auto nx = u32(grid, 0), nz = u32(grid, 4);
    require(nx <= 1024 && nz <= 1024 && std::size_t(nx) * nz <= 65536, "Terrain grid is too large");
    slice(grid, 20, std::size_t(nx) * nz * TargetProfile::terrain_cell_size);
    Archive terrain(archives.resolve(dump, TargetProfile::terrain_archive));
    std::set<unsigned> loaded;
    for (std::size_t cell = 0; cell < std::size_t(nx) * nz; ++cell) {
        l.checkpoint();
        auto id = u16(grid, 20 + cell * TargetProfile::terrain_cell_size);
        if (id == 65535 || !loaded.insert(id).second)
            continue;
        auto b = terrain.decoded(id);
        l.out.source_bytes += b.size();
        if (b.empty()) {
            l.note("Empty terrain resource " + std::to_string(id));
            continue;
        }
        auto block = Container::parse(b, "BG");
        require(block.files.size() >= 6, "Incomplete terrain block pack");
        ++l.out.terrain_blocks;
        auto collision = [&](View bytes, SpatialKind kind, std::vector<std::size_t> path) {
            if (bytes.empty())
                return;
            auto source = std::make_shared<CollisionSource>(
                CollisionSource{TargetProfile::terrain_archive, id, std::move(path),
                                Bytes(bytes.begin(), bytes.end())});
            auto begin = l.out.spatial.regions.size();
            try {
                decode_collision_mesh(l.out.spatial, bytes, kind,
                                      std::string(spatial_kind_name(kind)) + " / terrain " +
                                          std::to_string(id),
                                      std::move(source));
            } catch (...) {
                l.out.spatial.regions.resize(begin);
                throw;
            }
        };
        try {
            if (block.files.size() > TargetProfile::terrain_ground_slot)
                collision(block.files[TargetProfile::terrain_ground_slot], SpatialKind::Ground,
                          {TargetProfile::terrain_ground_slot});
            if (block.files.size() > TargetProfile::terrain_wall_slot &&
                !block.files[TargetProfile::terrain_wall_slot].empty()) {
                auto walls = Container::parse(block.files[TargetProfile::terrain_wall_slot], "BG");
                for (unsigned layer = 0; layer < std::min(std::size_t(4), walls.files.size());
                     ++layer)
                    collision(walls.files[layer], SpatialKind(unsigned(SpatialKind::Wall) + layer),
                              {TargetProfile::terrain_wall_slot, layer});
            }
        } catch (const std::exception &e) {
            l.note("Terrain collision " + std::to_string(id) + ": " + e.what());
        }
        for (std::size_t part = 0; part < 6; ++part)
            if (!block.files[part].empty()) {
                l.source = std::make_shared<SceneModelSource>(
                    SceneModelSource{TargetProfile::terrain_archive, id, {part}});
                l.placed_model(block.files[part],
                               "Terrain " + std::to_string(id) + " part " + std::to_string(part),
                               "terrain/");
            }
    }
    if (u32(grid, 16))
        l.note("Terrain replacement variants are available; showing the base layout");
    if (!background.files[4].empty()) {
        auto distant = Container::parse(background.files[4], "BG");
        for (std::size_t i = 0; i < distant.files.size(); ++i)
            if (!distant.files[i].empty()) {
                l.source = std::make_shared<SceneModelSource>(SceneModelSource{
                    GameProfile::field_archive(dump),
                    area * TargetProfile::area_stride + TargetProfile::background_resource_slot,
                    {4, i}});
                l.placed_model(distant.files[i], "Distant scenery " + std::to_string(i),
                               "terrain/");
            }
    }
    auto resources = Container::parse(member(TargetProfile::static_resource_slot), "AS");
    std::map<unsigned, Container> objects;
    std::map<unsigned, std::size_t> object_slots;
    for (auto &b : resources.files) {
        auto object = Container::parse(b, "SM");
        auto id = u16(object.files.at(0), 0);
        auto prefix = "static/" + std::to_string(id) + "/";
        for (auto slot : {TargetProfile::static_loop_motion, TargetProfile::static_daily_motion})
            if (slot < object.files.size())
                l.motion(
                    object.files[slot],
                    "Static model " + std::to_string(id) +
                        (slot == TargetProfile::static_loop_motion ? " loop" : " daily"),
                    prefix, slot == TargetProfile::static_daily_motion,
                    std::make_shared<SceneModelSource>(SceneModelSource{
                        GameProfile::field_archive(dump),
                        area * TargetProfile::area_stride + TargetProfile::static_resource_slot,
                        {std::size_t(&b - resources.files.data()), slot}}));
        object_slots[id] = std::size_t(&b - resources.files.data());
        objects.emplace(id, std::move(object));
    }
    l.out.placement_source = member(TargetProfile::placement_slot);
    auto placements = Container::parse(l.out.placement_source, "ED");
    auto zones = Container::parse(placements.files.at(TargetProfile::static_pack), "ES");
    try {
        auto ids = load_area_zone_ids(dump, unsigned(area));
        decode_overworld_regions(l.out.spatial, l.out.placement_source, ids);
        decode_encounter_regions(l.out.spatial, l.out.placement_source, ids);
        decode_field_system_regions(l.out.spatial, l.out.placement_source, ids);
    } catch (const std::exception &e) {
        l.note("Overworld overlays: " + std::string(e.what()));
    }
    for (std::size_t zone = 0; zone < zones.files.size(); ++zone)
        for (auto &placement : read_placements(zones.files[zone])) {
            int placement_id = int(l.out.placement_transforms.size());
            l.out.placement_transforms.push_back(pose_identity());
            auto collision_begin = l.out.spatial.regions.size();
            if (placement.collision)
                try {
                    decode_placement_collision(
                        l.out.spatial,
                        slice(zones.files[zone], placement.collision,
                              zones.files[zone].size() - placement.collision),
                        "Zone " + std::to_string(zone) + " object " +
                            std::to_string(placement.event),
                        int(zone));
                } catch (const std::exception &e) {
                    l.note("Placement collision: " + std::string(e.what()));
                }
            for (auto i = collision_begin; i < l.out.spatial.regions.size(); ++i)
                l.out.spatial.regions[i].placement = placement_id;
            l.checkpoint();
            ++l.out.static_placements;
            auto it = objects.find(placement.model);
            if (it == objects.end()) {
                l.note("Missing static model " + std::to_string(placement.model));
                continue;
            }
            auto pack = ModelPack::parse(it->second.files.at(1));
            auto prefix = "static/" + std::to_string(placement.model) + "/";
            for (auto &r : pack.resources)
                if (r.category == 4) {
                    l.shader(r.bytes, prefix);
                    l.out.shader_sources[prefix + decode_material_shader(r.bytes).name] = {
                        GameProfile::field_archive(dump),
                        area * TargetProfile::area_stride + TargetProfile::static_resource_slot,
                        {object_slots.at(placement.model), 1,
                         std::size_t(&r - pack.resources.data())}};
                }
            for (auto &r : pack.resources)
                if (r.category == 1) {
                    l.texture(r.bytes, prefix);
                    l.out.texture_sources[prefix + text(slice(r.bytes, 40, 64))] = {
                        GameProfile::field_archive(dump),
                        area * TargetProfile::area_stride + TargetProfile::static_resource_slot,
                        {object_slots.at(placement.model), 1,
                         std::size_t(&r - pack.resources.data())}};
                }
            auto draw_begin = l.out.draws.size();
            for (auto &r : pack.resources)
                if (r.category == 0) {
                    l.source = std::make_shared<SceneModelSource>(SceneModelSource{
                        GameProfile::field_archive(dump),
                        area * TargetProfile::area_stride + TargetProfile::static_resource_slot,
                        {object_slots.at(placement.model), 1,
                         std::size_t(&r - pack.resources.data())}});
                    l.placed_model(r.bytes,
                                   "Zone " + std::to_string(zone) + " object " +
                                       std::to_string(placement.event),
                                   prefix, &placement);
                }
            for (auto i = draw_begin; i < l.out.draws.size(); ++i)
                l.out.draws[i].placement = placement_id;
        }
    l.source.reset();
    const PlacementDocument placement_bindings(area, l.out.placement_source);
    std::map<std::tuple<unsigned, std::size_t, std::size_t>, int> npc_placements;
    for (std::size_t i = 0; i < placement_bindings.entries().size(); ++i) {
        const auto &entry = placement_bindings.entries()[i];
        if (entry.character)
            npc_placements[{entry.trainer ? TargetProfile::trainer_placement_pack
                                          : TargetProfile::character_placement_pack,
                            entry.zone, entry.row}] = int(i);
    }
    l.out.placement_transforms.resize(placement_bindings.entries().size(), pose_identity());
    for (auto &region : l.out.spatial.regions)
        if (region.overworld &&
            (region.overworld->category == TargetProfile::character_placement_pack ||
             region.overworld->category == TargetProfile::trainer_placement_pack)) {
            auto found = npc_placements.find(
                {region.overworld->category, region.overworld->local_zone, region.overworld->row});
            if (found != npc_placements.end())
                region.placement = found->second;
        }
    try {
        std::map<unsigned, Container> characters;
        std::map<unsigned, SceneModelSource> character_sources;
        auto resources = Container::parse(member(TargetProfile::character_resource_slot), "AC");
        for (auto &bytes : resources.files) {
            auto entry = Container::parse(bytes, "CP");
            auto id = std::stoul(text(entry.files.at(0)));
            characters.emplace(unsigned(id), Container::parse(entry.files.at(1), "CM"));
            character_sources.try_emplace(
                unsigned(id), SceneModelSource{GameProfile::field_archive(dump),
                                               area * TargetProfile::area_stride +
                                                   TargetProfile::character_resource_slot,
                                               {std::size_t(&bytes - resources.files.data()), 1}});
        }
        std::unique_ptr<Archive> shared;
        for (unsigned pack_index :
             {TargetProfile::character_placement_pack, TargetProfile::trainer_placement_pack,
              TargetProfile::contact_placement_pack}) {
            if (pack_index >= placements.files.size())
                continue;
            auto pack = Container::parse(placements.files[pack_index]);
            for (unsigned zone = 0; zone < pack.files.size(); ++zone) {
                auto &data = pack.files[zone];
                if (data.empty())
                    continue;
                auto count = u32(data, 0);
                auto stride = pack_index == TargetProfile::trainer_placement_pack   ? 84u
                              : pack_index == TargetProfile::contact_placement_pack ? 152u
                                                                                    : 120u;
                require(count <= 4096, "Excessive character placements");
                slice(data, 4, std::size_t(count) * stride);
                for (unsigned index = 0; index < count; ++index)
                    try {
                        auto p = 4 + index * stride;
                        require(u32(data, p) ==
                                    (pack_index == TargetProfile::trainer_placement_pack ? 7u : 1u),
                                "Unsupported character placement type");
                        auto character = u32(
                            data,
                            p + (pack_index == TargetProfile::trainer_placement_pack ? 48 : 52));
                        if (!character)
                            continue;
                        auto found = characters.find(character);
                        if (found == characters.end()) {
                            if (!shared)
                                shared = std::make_unique<Archive>(
                                    dump / TargetProfile::character_archive);
                            require(character < shared->size(),
                                    "Character resource index out of range");
                            found = characters
                                        .emplace(character,
                                                 Container::parse(shared->decoded(character), "CM"))
                                        .first;
                            character_sources.emplace(
                                character,
                                SceneModelSource{TargetProfile::character_archive, character, {}});
                        }
                        auto &resource = found->second;
                        Placement placement;
                        for (unsigned k = 0; k < 3; ++k)
                            placement.position[k] = f32(data, p + 4 + k * 4);
                        for (unsigned k = 0; k < 4; ++k)
                            placement.rotation[k] = f32(data, p + 16 + k * 4);
                        for (auto v : placement.position)
                            require(std::isfinite(v) && std::abs(v) < 1e7f,
                                    "Invalid character position");
                        float norm = 0;
                        for (auto v : placement.rotation)
                            norm += v * v;
                        require(std::isfinite(norm) && norm > .9f && norm < 1.1f,
                                "Invalid character rotation");
                        auto prefix = "character/" + std::to_string(pack_index) + "/" +
                                      std::to_string(zone) + "/" + std::to_string(index) + "/";
                        auto name = "Zone " + std::to_string(zone) + " character " +
                                    std::to_string(character) + " event " +
                                    std::to_string(u32(data, p + 44));
                        if (resource.files.size() > 1 && !resource.files[1].empty()) {
                            auto &motions = resource.files[1];
                            auto motion_id = pack_index == TargetProfile::trainer_placement_pack
                                                 ? 0
                                                 : u32(data, p + 60);
                            auto n = u32(motions, 0);
                            require(n <= 65536, "Excessive character motions");
                            slice(motions, 4, std::size_t(n) * 4);
                            if (motion_id < n && u32(motions, 4 + motion_id * 4)) {
                                auto offset = 4 + std::size_t(u32(motions, 4 + motion_id * 4));
                                l.motion(slice(motions, offset, motions.size() - offset),
                                         name + " idle", prefix, false);
                            } else
                                l.note(name + ": authored idle motion unavailable");
                        }
                        if (resource.files.size() > 4)
                            l.motion(resource.files[4], name + " face", prefix, false);
                        auto model = ModelPack::parse(resource.files.at(0));
                        for (auto &r : model.resources)
                            if (r.category == 4)
                                l.shader(r.bytes, prefix);
                        for (auto &r : model.resources)
                            if (r.category == 1)
                                l.texture(r.bytes, prefix);
                        auto first = l.out.draws.size();
                        for (auto &r : model.resources)
                            if (r.category == 0) {
                                auto link = character_sources.at(character);
                                link.path.push_back(0);
                                link.path.push_back(std::size_t(&r - model.resources.data()));
                                l.source = std::make_shared<SceneModelSource>(std::move(link));
                                l.placed_model(r.bytes, name, prefix, &placement);
                            }
                        l.source.reset();
                        for (auto draw = first; draw < l.out.draws.size(); ++draw) {
                            l.out.draws[draw].character = true;
                            if (pack_index == TargetProfile::character_placement_pack ||
                                pack_index == TargetProfile::trainer_placement_pack)
                                l.out.draws[draw].placement =
                                    npc_placements.at({pack_index, zone, index});
                            l.out.draws[draw].conditional =
                                u32(data, p + 36) != 0 || u32(data, p + 32) != 0;
                        }
                        if (l.out.draws.size() > first)
                            ++l.out.character_placements;
                    } catch (const std::exception &e) {
                        l.checkpoint();
                        l.note("Character placement " + std::to_string(pack_index) + "/" +
                               std::to_string(zone) + "/" + std::to_string(index) + ": " +
                               e.what());
                    }
            }
        }
    } catch (const std::exception &e) {
        l.checkpoint();
        l.note("Characters: " + std::string(e.what()));
    }
    l.source.reset();
    try {
        auto resident =
            Container::parse(Archive(dump / TargetProfile::resident_archive).decoded(0), "FR");
        auto effects = Container::parse(resident.files.at(TargetProfile::resident_effects), "SB");
        for (auto binding : TargetProfile::weather_boards) {
            auto resource = Container::parse(effects.files.at(binding[1]), "SB");
            auto prefix =
                "weather/" + std::to_string(binding[1]) + "/" + std::to_string(binding[2]) + "/";
            l.motion(resource.files.at(binding[3]), "Weather screen layer", prefix, false);
            auto pack = ModelPack::parse(resource.files.at(binding[2]));
            for (auto &r : pack.resources)
                if (r.category == 4)
                    l.shader(r.bytes, prefix);
            for (auto &r : pack.resources)
                if (r.category == 1)
                    l.texture(r.bytes, prefix);
            auto first = l.out.draws.size();
            for (auto &r : pack.resources)
                if (r.category == 0)
                    l.placed_model(r.bytes, "Weather screen layer", prefix);
            for (auto i = first; i < l.out.draws.size(); ++i) {
                auto &draw = l.out.draws[i];
                draw.weather_mask = binding[0];
                float x = 0, y = 0;
                for (auto &v : draw.vertices) {
                    x = std::max(x, std::abs(v.x));
                    y = std::max(y, std::abs(v.y));
                }
                require(x > 0 && y > 0, "Invalid weather screen geometry");
                draw.weather_scale = {1 / x, 1 / y};
            }
        }
    } catch (const std::exception &e) {
        l.checkpoint();
        l.note("Weather screen layers: " + std::string(e.what()));
    }
    if (std::any_of(l.out.lighting_contexts.begin(), l.out.lighting_contexts.end(), [](auto &c) {
            return c.sky_enabled;
        }))
        try {
            auto resource = Container::parse(Archive(dump / TargetProfile::sky_archive).decoded(0));
            for (unsigned part = 0; part < TargetProfile::sky_parts.size(); ++part) {
                auto binding = TargetProfile::sky_parts[part];
                auto prefix = "sky/" + std::to_string(part) + "/";
                for (unsigned slot = 1; slot < 4; ++slot)
                    if (binding[slot] >= 0)
                        l.motion(resource.files.at(unsigned(binding[slot])), "Sky", prefix,
                                 slot != 2);
                auto pack = ModelPack::parse(resource.files.at(unsigned(binding[0])));
                for (auto &r : pack.resources)
                    if (r.category == 4)
                        l.shader(r.bytes, prefix);
                for (auto &r : pack.resources)
                    if (r.category == 1)
                        l.texture(r.bytes, prefix);
                auto first = l.out.draws.size();
                for (auto &r : pack.resources)
                    if (r.category == 0)
                        l.placed_model(r.bytes, "Sky", prefix);
                for (auto i = first; i < l.out.draws.size(); ++i)
                    l.out.draws[i].sky_part = int(part);
            }
        } catch (const std::exception &e) {
            l.checkpoint();
            l.note("Sky: " + std::string(e.what()));
        }
    for (auto &animation : l.out.material_animations)
        for (std::size_t t = 0; t < animation.motion.tracks.size(); ++t) {
            auto &track = animation.motion.tracks[t];
            for (std::size_t m = 0; m < l.out.materials.size(); ++m)
                if (l.out.materials[m].resource_scope == animation.texture_prefix &&
                    l.out.materials[m].name == track.material)
                    animation.bindings.push_back({t, m});
            for (auto &key : track.textures)
                if (!l.out.textures.contains(animation.texture_prefix + key.texture))
                    l.note(animation.name + ": missing animated texture " + key.texture);
        }
    for (auto &m : l.out.materials)
        for (auto &input : m.texture_inputs)
            if (!input.empty() && !l.out.textures.contains(input))
                l.note("Missing texture: " + input);
    for (auto &context : l.out.lighting_contexts)
        for (auto &material : l.out.materials)
            if (material.fragment_lighting &&
                std::none_of(context.sets.begin(), context.sets.end(), [&](const auto &set) {
                    return int(set.index) == material.light_set;
                }))
                l.note(material.name + ": light set " + std::to_string(material.light_set) +
                       " unavailable in zone " + std::to_string(context.zone) +
                       "; using preview lighting");
    if (std::any_of(l.out.materials.begin(), l.out.materials.end(), [](const auto &m) {
            return m.combiner.lighting;
        }))
        l.note("Game lighting uses authored environment lights, daily curves and reflection lookup "
               "tables. Weather scheduling, transitions, distribution/Fresnel tables and "
               "tangent-map lighting are not yet reproduced.");
    l.note("Material preview: six-stage texture combiners, constants and three UV texture inputs. "
           "UV, constant-color and texture-pattern animation are supported. Native vertex programs "
           "are not yet reproduced.");
    for (unsigned appearance = 0; appearance < 2; ++appearance) {
        auto first = l.out.draws.size();
        try {
            auto player = load_player_assets(dump, appearance);
            int body_rig = int(l.out.skeletons.size());
            for (auto &part : player.parts) {
                auto prefix = "player/" + std::to_string(appearance) + "/" + part.name + "/";
                l.skeletal_motions[prefix] = {part.motions[0], false};
                auto pack = ModelPack::parse(part.model);
                for (auto &r : pack.resources)
                    if (r.category == 4)
                        l.shader(r.bytes, prefix);
                for (auto &[name, image] : part.textures)
                    l.out.textures.emplace(prefix + name, std::move(image));
                auto begin = l.out.draws.size();
                auto rig_begin = l.out.skeletons.size();
                for (auto &r : pack.resources)
                    if (r.category == 0)
                        l.placed_model(r.bytes,
                                       std::string(appearance ? "Player girl: " : "Player boy: ") +
                                           part.name,
                                       prefix);
                require(l.out.skeletons.size() > rig_begin,
                        "Player " + part.name + " has no animated skeleton");
                for (auto j = rig_begin; j < l.out.skeletons.size(); ++j) {
                    auto &rig = l.out.skeletons[j];
                    rig.player = int(appearance);
                    rig.locomotion = part.motions;
                    if (!part.attachment.empty()) {
                        rig.parent_skeleton = body_rig;
                        auto &joints = l.out.skeletons[body_rig].joints;
                        auto joint = std::find_if(joints.begin(), joints.end(), [&](const auto &b) {
                            return b.name == part.attachment;
                        });
                        require(joint != joints.end(), "Missing outfit attachment joint");
                        rig.parent_joint = int(joint - joints.begin());
                    }
                }
                for (auto j = begin; j < l.out.draws.size(); ++j)
                    l.out.draws[j].player = int(appearance);
            }
            std::erase_if(l.out.draws, [&](const auto &d) {
                return d.player == int(appearance) && d.mesh.find("nohat") != std::string::npos;
            });
            l.out.player_available[appearance] = true;
        } catch (const std::exception &e) {
            l.out.draws.resize(first);
            l.note("Player " + std::to_string(appearance) + ": " + e.what());
        }
    }
    l.note("Placements from all zones are shown together. Story conditions and scripted movement "
           "require game state; conditional characters can be hidden in Scene contents.");
    std::stable_sort(l.out.draws.begin(), l.out.draws.end(), [&](const auto &a, const auto &b) {
        if ((a.sky_part >= 0) != (b.sky_part >= 0))
            return a.sky_part >= 0;
        if (bool(a.weather_mask) != bool(b.weather_mask))
            return !a.weather_mask;
        auto &x = l.out.materials[a.material];
        auto &y = l.out.materials[b.material];
        return x.layer != y.layer ? x.layer < y.layer : x.priority < y.priority;
    });
    l.out.low.fill(std::numeric_limits<float>::max());
    l.out.high.fill(std::numeric_limits<float>::lowest());
    for (auto &d : l.out.draws)
        if (d.player < 0 && !d.weather_mask && d.sky_part < 0)
            for (auto v : d.vertices) {
                std::array<float, 3> p{v.x, v.y, v.z};
                for (unsigned k = 0; k < 3; ++k) {
                    l.out.low[k] = std::min(l.out.low[k], p[k]);
                    l.out.high[k] = std::max(l.out.high[k], p[k]);
                }
            }
    require(!l.out.draws.empty(), "No supported environment geometry was loaded");
    l.out.load_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return std::move(l.out);
}
std::vector<bool> evaluate_visibility(const Environment &scene, double seconds, float hour,
                                      bool enabled) {
    std::vector<bool> visible(scene.draws.size(), true);
    if (!enabled)
        return visible;
    for (auto &animation : scene.visibility_animations) {
        auto frame = unsigned(animation_frame(
            animation.motion.clock, seconds + animation.seconds_offset, animation.daily, hour));
        for (auto &track : animation.motion.tracks)
            for (unsigned i = 0; i < scene.draws.size(); ++i)
                if (scene.draws[i].scope == animation.scope && scene.draws[i].mesh == track.mesh &&
                    !track.frames.empty())
                    visible[i] =
                        (animation.override_previous || visible[i]) &&
                        track.frames[std::min(std::size_t(frame), track.frames.size() - 1)];
    }
    return visible;
}
std::string Environment::report() const {
    std::size_t vertices = 0, triangles = 0, texture_bytes = 0;
    for (auto &d : draws) {
        vertices += d.vertices.size();
        triangles += d.indices.size() / 3;
    }
    for (auto &[name, t] : textures)
        texture_bytes += t.rgba.size();
    std::size_t combiners = 0, multitexture = 0, lighting = 0, normal_maps = 0;
    for (auto &m : materials) {
        combiners += m.combiner.present && m.combiner.unsupported.empty() && !m.unsupported_mapping;
        multitexture += m.texture_count > 1;
        lighting += m.combiner.lighting;
        normal_maps += m.bump.mode == 1 && m.bump.unsupported.empty();
    }
    std::ostringstream o;
    o << "Spatial regions: " << spatial.regions.size()
      << "\nCamera settings: " << spatial.cameras.size()
      << "\nZone camera bindings: " << spatial.zones.size() << "\n";
    o << "Character placements: " << character_placements
      << "\nVisibility motions: " << visibility_animations.size() << "\n";
    o << "Animated skeletons: " << skeletons.size() << "\n";
    o << "Decoded material combiners: " << combiners << " / " << materials.size()
      << "\nMulti-texture materials: " << multitexture
      << "\nMaterials using lighting inputs: " << lighting
      << "\nNormal-map materials: " << normal_maps << "\n";
    o << "Material motion resources: " << material_animations.size() << "\n";
    for (auto &a : material_animations)
        o << a.name << ": " << a.motion.tracks.size() << " tracks, " << a.bindings.size()
          << " bindings, " << a.motion.frames << " frames\n";
    o << "Lighting contexts: " << lighting_contexts.size()
      << "\nLighting lookup tables: " << lighting_tables.size() << "\n";
    for (auto &context : lighting_contexts) {
        unsigned fogs = 0;
        for (auto &fog : context.effects.fog)
            fogs += fog.enabled;
        o << "Environment effects zone " << context.zone << ": " << fogs << " fog slots, bloom "
          << context.effects.bloom.enabled << ", weather profiles " << context.weather.size()
          << "\n";
    }
    for (auto &context : lighting_contexts)
        o << "Lighting zone " << context.zone << ": " << context.sets.size() << " sets, "
          << context.motions.size() << " daily variants\n";
    o << "Terrain resources: " << terrain_blocks << "\nStatic placements: " << static_placements
      << "\nDraws: " << draws.size() << "\nVertices: " << vertices << "\nTriangles: " << triangles
      << "\nMaterials: " << materials.size() << "\nTextures: " << textures.size()
      << "\nShared shaders inventoried: " << shader_resources
      << "\nDecoded texture bytes: " << texture_bytes
      << "\nDecoded source bytes read: " << source_bytes << "\nLoad milliseconds: " << load_ms
      << "\nBounds: " << low[0] << ',' << low[1] << ',' << low[2] << " to " << high[0] << ','
      << high[1] << ',' << high[2] << "\n";
    for (auto &d : diagnostics)
        o << "- " << d << '\n';
    return o.str();
}
}
