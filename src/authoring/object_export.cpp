#include "authoring/object_export.h"
#include "formats/compression.h"
#include "audio/audio_document.h"
#include "compiler/model_build.h"
#include "compiler/terrain.h"
#include "assets/material_document.h"
#include "core/digest.h"
#include "scene/model_decoder.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
namespace studio {
Bytes compile_composition_objects(View placements, const CompositionDocument &document,
                                  const std::map<std::size_t, std::uint16_t> &models) {
    auto field = Container::parse(placements, "ED");
    auto zones = Container::parse(field.files.at(TargetProfile::static_pack), "ES");
    const auto zone = document.object_zone();
    require(zone < zones.files.size(), "Object zone group does not exist in this area. Choose an "
                                       "existing group in Authoring Review.");
    if (document.instances().empty())
        return Bytes(placements.begin(), placements.end());
    std::set<std::uint32_t> events;
    std::size_t aliases = 0;
    for (auto &data : zones.files)
        for (auto &row : read_placements(data)) {
            events.insert(row.event);
            aliases += row.alias != 0;
        }
    const auto initial = read_placements(zones.files[zone]);
    auto ordinary = std::count_if(initial.begin(), initial.end(), [](const auto &row) {
        return row.alias == 0;
    });
    const auto count = std::size_t(ordinary) + aliases + document.instances().size();
    require(count <= TargetProfile::actor_capacity,
            "Object staging needs " + std::to_string(count) + " static actors in zone group " +
                std::to_string(zone) + "; the target supports " +
                std::to_string(TargetProfile::actor_capacity) +
                ". Remove added objects or choose another zone group.");
    for (const auto &instance : document.instances()) {
        std::uint16_t model;
        if (auto compiled = models.find(instance.resource); compiled != models.end())
            model = compiled->second;
        else {
            require(instance.resource < document.catalog().entries.size(),
                    "Compile project asset resources before placing them");
            const auto &resource = document.catalog().entries.at(instance.resource);
            require(resource.reusable_static() && resource.area == document.catalog().area,
                    "Compile the source map resource before placing it in this map");
            model = *resource.static_model;
        }
        auto donor = std::find_if(initial.begin(), initial.end(), [&](const auto &row) {
            return row.model == model && !row.alias;
        });
        if (donor == initial.end())
            donor = std::find_if(initial.begin(), initial.end(), [](const auto &row) {
                return !row.alias;
            });
        require(donor != initial.end(),
                "The selected zone needs an ordinary static placement to supply its record layout");
        composition_transform(instance.transform);
        std::uint32_t event = TargetProfile::authored_event_begin;
        while (events.contains(event) && event < 65535)
            ++event;
        require(event < 65535, "No free authored object event identity");
        events.insert(event);
        auto &bytes = zones.files[zone];
        const auto row = read_placements(bytes).size();
        bytes = append_placement(bytes, std::size_t(donor - initial.begin()), model, event,
                                 instance.transform.position, true);
        const auto angle = instance.transform.turn * .00872664626f;
        const std::array<float, 4> rotation{0, std::sin(angle), 0, std::cos(angle)};
        for (unsigned k = 0; k < 4; ++k)
            put_float(bytes, 4 + row * 56 + 16 + k * 4, rotation[k]);
        if (instance.collision) {
            const auto &box = *instance.collision;
            validate_object_collision(box);
            bytes = attach_box_collision(bytes, row, {box.size[0], box.size[1], box.size[2]});
            const auto offset = read_placements(bytes).at(row).collision;
            const auto matrix = composition_transform(instance.transform);
            for (unsigned a = 0; a < 3; ++a) {
                const float value = matrix[a * 4] * box.offset[0] +
                                    matrix[a * 4 + 1] * box.offset[1] +
                                    matrix[a * 4 + 2] * box.offset[2] + matrix[a * 4 + 3];
                require(std::isfinite(value) && std::abs(value) < 1e7f,
                        "Collision position is outside the supported map range");
                put_float(bytes, offset + 8 + a * 4, value);
            }
        }
    }
    field.files[TargetProfile::static_pack] = zones.write();
    auto result = field.write();
    const auto checked =
        Container::parse(Container::parse(result, "ED").files.at(TargetProfile::static_pack), "ES");
    require(read_placements(checked.files.at(zone)).size() ==
                initial.size() + document.instances().size(),
            "Added object count did not survive compilation");
    return result;
}
namespace {
Bytes compile_project_resource(const std::filesystem::path &dump, const MapResourceEntry &entry,
                               const ProjectAsset &asset, const Container &templates) {
    require(entry.geometry_complete && entry.issues.empty(),
            "Rebuild the source asset library and resolve its missing dependencies before export");
    const auto original_preview = preview_map_resource(dump, entry);
    auto preview = project_asset_preview(original_preview, asset);
    using MeshKey =
        std::tuple<std::filesystem::path, std::size_t, std::vector<std::size_t>, std::string>;
    std::map<MeshKey, unsigned> submeshes;
    std::vector<unsigned> selected_submeshes;
    for (std::size_t i = 0; i < original_preview.draws.size(); ++i) {
        const auto &draw = original_preview.draws[i];
        require(bool(draw.source), "Extracted geometry has no native model source");
        const auto submesh =
            submeshes[{draw.source->archive, draw.source->member, draw.source->path, draw.mesh}]++;
        if (!asset.faces.at(i).empty())
            selected_submeshes.push_back(submesh);
    }
    std::optional<Container> object;
    for (const auto &bytes : templates.files) {
        auto candidate = Container::parse(bytes, "SM");
        if (candidate.files.size() == 29 && candidate.files[0].size() >= 8 &&
            candidate.files[0][2] == 255 && candidate.files[0][3] == 0 &&
            candidate.files[0][4] == 0) {
            for (std::size_t i = 1; i < candidate.files.size(); ++i)
                candidate.files[i].clear();
            object = std::move(candidate);
            break;
        }
    }
    require(object.has_value(),
            "This area needs a plain static resource as an export template for extracted assets");
    struct CachedMember {
        Bytes bytes;
        std::string hash;
    };
    std::map<std::pair<std::filesystem::path, std::size_t>, CachedMember> members;
    const auto read = [&](const MapSourceReference &reference) {
        const auto &source = reference.location;
        require(source.archive == GameProfile::field_archive(dump) ||
                    source.archive == TargetProfile::terrain_archive,
                "Unsupported asset source archive");
        auto [member, fresh] = members.try_emplace(std::make_pair(source.archive, source.member));
        if (fresh) {
            member->second.bytes = Archive(dump / source.archive).decoded(source.member);
            member->second.hash = sha256(member->second.bytes);
        }
        require(member->second.hash == reference.member_hash,
                "Asset dependency changed; rebuild its source library");
        return asset_resource(member->second.bytes, source.path);
    };
    ModelDecoder validator;
    for (const auto &dependency : entry.dependencies)
        if (dependency.kind == MapResourceDependency::Kind::Shader)
            validator.shader(read(dependency.source), "resource/");
    std::vector<ModelResource> resources;
    std::array<std::size_t, 5> next{};
    std::size_t draw_count = 0;
    for (const auto &dependency : entry.dependencies) {
        auto bytes = read(dependency.source);
        if (dependency.kind == MapResourceDependency::Kind::Model) {
            const auto original = Model::parse(bytes);
            require(original.bones == 0,
                    "This extracted asset uses skeleton joints. Place the intact game model to "
                    "retain its rig, or choose an unskinned source.");
            auto compiled = original;
            bool first = true;
            for (std::size_t draw_index = 0; draw_index < preview.draws.size(); ++draw_index) {
                const auto &draw = preview.draws[draw_index];
                const auto &source = dependency.source.location;
                if (!draw.source || draw.source->archive != source.archive ||
                    draw.source->member != source.member || draw.source->path != source.path)
                    continue;
                TerrainExportMesh mesh;
                mesh.name = "AuthoredMesh" + std::to_string(draw_count++);
                mesh.indices = draw.indices;
                for (const auto &v : draw.vertices)
                    mesh.vertices.push_back({{v.x, v.y, v.z},
                                             {v.nx, v.ny, v.nz},
                                             {v.u, v.v},
                                             v.color,
                                             {v.u1, v.v1},
                                             {v.u2, v.v2},
                                             {v.tx, v.ty, v.tz}});
                const auto &material = preview.materials.at(draw.material).name;
                compiled = Model::parse(append_rigid_meshes(compiled, material, original, draw.mesh,
                                                            {mesh}, first,
                                                            selected_submeshes.at(draw_index)));
                auto decoder = validator;
                decoder.model(compiled.original, dependency.name, "resource/");
                const auto &decoded = decoder.out.draws.back();
                require(decoded.indices == draw.indices &&
                            decoded.vertices.size() == draw.vertices.size(),
                        "Compiled object topology differs from the authored asset");
                for (std::size_t i = 0; i < draw.vertices.size(); ++i) {
                    const auto actual = authored_vertex(decoded.vertices[i]);
                    const auto expected = authored_vertex(draw.vertices[i]);
                    bool equal = actual.color == expected.color;
                    for (unsigned component = 0; component < actual.values.size(); ++component)
                        equal &= component >= 3 && component < 9
                                     ? std::abs(actual.values[component] -
                                                expected.values[component]) <= 1e-5f
                                     : actual.values[component] == expected.values[component];
                    require(equal, "This vertex layout cannot preserve the edited attributes. Use "
                                   "a rigid source with matching UV and normal streams.");
                }
                first = false;
            }
            if (!first)
                resources.push_back(
                    {0, next[0]++, dependency.name, std::move(compiled.original), 0});
        } else if (dependency.kind == MapResourceDependency::Kind::Texture ||
                   dependency.kind == MapResourceDependency::Kind::Shader) {
            const unsigned category =
                dependency.kind == MapResourceDependency::Kind::Texture ? 1 : 4;
            resources.push_back({category, next[category]++, dependency.name, std::move(bytes), 0});
        } else if (dependency.kind == MapResourceDependency::Kind::Motion) {
            const auto slot = dependency.name == "Daily motion" ? TargetProfile::static_daily_motion
                                                                : TargetProfile::static_loop_motion;
            object->files.at(slot) = std::move(bytes);
        }
    }
    require(draw_count == preview.draws.size(),
            "Extracted geometry has an unresolved native model source");
    object->files[1] = pack_resources(resources);
    return object->write(TargetProfile::resource_alignment);
}
Bytes portable_static_resource(const std::filesystem::path &dump, const MapResourceEntry &entry) {
    auto object = Container::parse(read_map_source(dump, entry.source), "SM");
    require(object.files.size() == 29, "Unsupported static model resource layout");
    std::map<std::pair<unsigned, std::string>, Bytes> dependencies;
    Archive field(dump / GameProfile::field_archive(dump));
    auto background = Container::parse(field.decoded(entry.area * TargetProfile::area_stride +
                                                     TargetProfile::background_resource_slot),
                                       "BG");
    for (unsigned slot : {0u, 1u}) {
        if (background.files.at(slot).empty())
            continue;
        auto group = Container::parse(background.files[slot], "BG");
        for (const auto &bytes : group.files)
            if (!bytes.empty()) {
                auto name =
                    slot == 0 ? text(slice(bytes, 40, 64)) : decode_material_shader(bytes).name;
                dependencies.emplace(std::make_pair(slot == 0 ? 1u : 4u, name), bytes);
            }
    }
    for (auto lod : {1u, 10u, 19u}) {
        if (object.files[lod].empty())
            continue;
        for (const auto &resource : ModelPack::parse(object.files[lod]).resources) {
            if (resource.category == 1)
                dependencies.insert_or_assign({1, text(slice(resource.bytes, 40, 64))},
                                              resource.bytes);
            else if (resource.category == 3 || resource.category == 4)
                dependencies.insert_or_assign({4, decode_material_shader(resource.bytes).name},
                                              resource.bytes);
        }
    }
    for (auto lod : {1u, 10u, 19u}) {
        if (object.files[lod].empty())
            continue;
        auto pack = ModelPack::parse(object.files[lod]);
        std::set<std::pair<unsigned, std::string>> present, needed;
        std::array<std::size_t, 5> next{};
        for (const auto &resource : pack.resources) {
            next.at(resource.category) = std::max(next.at(resource.category), resource.index + 1);
            if (resource.category == 1)
                present.emplace(1, text(slice(resource.bytes, 40, 64)));
            else if (resource.category == 3 || resource.category == 4)
                present.emplace(4, decode_material_shader(resource.bytes).name);
            else if (resource.category == 0) {
                auto model = Model::parse(resource.bytes);
                for (const auto &name : model.names[1])
                    needed.emplace(1, name);
                for (const auto &name : model.names[0])
                    needed.emplace(4, name);
            }
        }
        for (const auto &dependency : entry.dependencies)
            if (dependency.kind == MapResourceDependency::Kind::Texture)
                needed.emplace(1, dependency.name);
        bool changed = false;
        for (const auto &key : needed)
            if (!present.contains(key)) {
                auto found = dependencies.find(key);
                require(found != dependencies.end(), "Source model '" + entry.name +
                                                         "' is missing dependency '" + key.second +
                                                         "'");
                pack.resources.push_back(
                    {key.first, next[key.first]++, key.second, found->second, 0});
                changed = true;
            }
        if (changed)
            object.files[lod] = pack_resources(pack.resources);
    }
    return object.write(TargetProfile::resource_alignment);
}
}
std::map<std::size_t, Bytes> compile_composition_resources(const std::filesystem::path &dump,
                                                           const CompositionDocument &document) {
    Archive archive(dump / GameProfile::field_archive(dump));
    const auto first = document.catalog().area * TargetProfile::area_stride;
    auto resources =
        Container::parse(archive.decoded(first + TargetProfile::static_resource_slot), "AS");
    std::set<std::uint16_t> used;
    for (const auto &bytes : resources.files)
        used.insert(u16(Container::parse(bytes, "SM").files.at(0), 0));
    std::map<std::size_t, std::uint16_t> models;
    bool changed = false;
    for (const auto &instance : document.instances()) {
        if (models.contains(instance.resource))
            continue;
        const auto *asset = document.project_asset(instance.resource);
        const auto &entry =
            document.catalog().entries.at(asset ? asset->source : instance.resource);
        require(asset || entry.reusable_static(), "Choose a complete static model resource");
        read_map_source(dump, entry.source);
        if (!asset && entry.area == document.catalog().area) {
            models[instance.resource] = *entry.static_model;
            continue;
        }
        auto bytes = asset ? compile_project_resource(dump, entry, *asset, resources)
                           : portable_static_resource(dump, entry);
        auto object = Container::parse(bytes, "SM");
        unsigned id = asset ? 0 : *entry.static_model;
        if (used.contains(std::uint16_t(id))) {
            id = 0;
            while (id < 65536 && used.contains(std::uint16_t(id)))
                ++id;
        }
        require(id < 65536, "No free static model identity in the target area");
        used.insert(std::uint16_t(id));
        put16(object.files.at(0), 0, std::uint16_t(id));
        resources.files.push_back(object.write(TargetProfile::resource_alignment));
        models[instance.resource] = std::uint16_t(id);
        changed = true;
    }
    std::map<std::size_t, Bytes> result;
    result[first + TargetProfile::placement_slot] = compile_composition_objects(
        archive.decoded(first + TargetProfile::placement_slot), document, models);
    if (changed) {
        auto bytes = resources.write(TargetProfile::resource_alignment);
        validate_static_alignment(bytes);
        result[first + TargetProfile::static_resource_slot] = std::move(bytes);
    }
    return result;
}
void export_composition_objects(const std::filesystem::path &dump,
                                const CompositionDocument &document,
                                const std::filesystem::path &output) {
    require_audio_output(dump, output);
    if (document.instances().empty())
        return;
    Archive archive(dump / GameProfile::field_archive(dump));
    auto members = compile_composition_resources(dump, document);
    for (auto &[member, bytes] : members) {
        auto original = archive.raw(member);
        if (!original.empty() && original.front() == 0x11) {
            auto stored = compress(bytes);
            require(decompress(stored) == bytes, "Object export compression verification failed");
            bytes = std::move(stored);
        }
    }
    const auto path = output / GameProfile::field_archive(dump);
    std::filesystem::create_directories(path.parent_path());
    archive.export_to(path, members);
}
}
