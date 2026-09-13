#include "authoring/resource_catalog.h"
#include "assets/material_document.h"
#include "core/digest.h"
#include "scene/model_decoder.h"
#include <iomanip>

namespace studio {
namespace {
using Dependency = MapResourceDependency;
void checkpoint(std::atomic_bool *cancel) {
    require(!cancel || !cancel->load(), "Resource loading cancelled");
}
MapSourceReference reference(const std::filesystem::path &archive, std::size_t member,
                             const std::string &hash, std::vector<std::size_t> path) {
    return {{archive, member, std::move(path)}, hash};
}
void issue(MapResourceEntry &entry, const std::string &message) {
    if (std::find(entry.issues.begin(), entry.issues.end(), message) == entry.issues.end())
        entry.issues.push_back(message);
}
void geometry(MapResourceEntry &entry, View bytes, const MapSourceReference &source,
              const std::string &name,
              std::vector<std::pair<Dependency::Kind, std::string>> &needed,
              std::atomic_bool *cancel) {
    ModelDecoder decoder;
    decoder.cancel = cancel;
    decoder.model(bytes, name, "resource/");
    require(!decoder.out.draws.empty(), "Model has no previewable geometry");
    for (std::size_t i = 0; i < decoder.out.draws.size(); ++i) {
        const auto &draw = decoder.out.draws[i];
        entry.vertices += draw.vertices.size();
        entry.triangles += draw.indices.size() / 3;
        entry.meshes.push_back({draw.mesh, decoder.out.materials.at(draw.material).name, source, i,
                                draw.vertices.size(), draw.indices.size() / 3});
    }
    entry.draws += decoder.out.draws.size();
    entry.materials += decoder.out.materials.size();
    for (const auto &material : decoder.out.materials) {
        for (const auto &texture : material.texture_inputs)
            if (!texture.empty())
                needed.emplace_back(Dependency::Kind::Texture, texture.substr(9));
        for (const auto &shader : {material.vertex_shader, material.fragment_shader})
            if (!shader.empty())
                needed.emplace_back(Dependency::Kind::Shader, shader);
    }
    entry.dependencies.push_back({Dependency::Kind::Model, name, source});
}
void resolve(MapResourceEntry &entry,
             const std::vector<std::pair<Dependency::Kind, std::string>> &needed,
             const std::vector<Dependency> &local, const std::vector<Dependency> &shared) {
    std::set<std::pair<Dependency::Kind, std::string>> seen;
    for (const auto &key : needed)
        if (seen.insert(key).second) {
            std::vector<const Dependency *> matches;
            for (const auto *pool : {&local, &shared}) {
                for (const auto &dependency : *pool)
                    if (dependency.kind == key.first && dependency.name == key.second)
                        matches.push_back(&dependency);
                if (!matches.empty())
                    break;
            }
            const auto role = key.first == Dependency::Kind::Texture ? "texture" : "shader";
            if (matches.size() == 1)
                entry.dependencies.push_back(*matches.front());
            else
                issue(entry, std::string(matches.empty() ? "Missing " : "Ambiguous ") + role +
                                 ": " + key.second);
        }
}
std::vector<Dependency> shared_dependencies(const std::filesystem::path &field_archive,
                                            const Container &background, std::size_t member,
                                            const std::string &hash,
                                            std::vector<std::string> &issues) {
    std::vector<Dependency> result;
    for (unsigned slot = 0; slot < 2; ++slot) {
        if (background.files.at(slot).empty())
            continue;
        auto pack = Container::parse(background.files[slot], "BG");
        for (std::size_t i = 0; i < pack.files.size(); ++i) {
            if (pack.files[i].empty())
                continue;
            try {
                auto name = slot == 0 ? text(slice(pack.files[i], 40, 64))
                                      : decode_material_shader(pack.files[i]).name;
                result.push_back({slot == 0 ? Dependency::Kind::Texture : Dependency::Kind::Shader,
                                  name, reference(field_archive, member, hash, {slot, i})});
            } catch (const std::exception &error) {
                issues.push_back("Shared resource " + std::to_string(slot) + "/" +
                                 std::to_string(i) + ": " + error.what());
            }
        }
    }
    return result;
}
void terrain_model(MapResourceCatalog &catalog, View bytes, MapSourceReference source,
                   MapResourceKind kind, const std::vector<Dependency> &shared,
                   std::atomic_bool *cancel) {
    MapResourceEntry entry;
    entry.area = catalog.area;
    entry.kind = kind;
    entry.source = std::move(source);
    entry.name = kind == MapResourceKind::BakedTerrain ? "Baked terrain" : "Distant scenery";
    try {
        std::vector<std::pair<Dependency::Kind, std::string>> needed;
        geometry(entry, bytes, entry.source, entry.name, needed, cancel);
        entry.geometry_complete = true;
        resolve(entry, needed, {}, shared);
    } catch (const std::exception &error) {
        checkpoint(cancel);
        issue(entry, error.what());
    }
    catalog.entries.push_back(std::move(entry));
}
}
MapResourceCatalog load_map_resources(const std::filesystem::path &dump, unsigned area,
                                      std::atomic_bool *cancel) {
    checkpoint(cancel);
    Archive field(dump / GameProfile::field_archive(dump));
    require(area < field.size() / TargetProfile::area_stride, "Field area is out of range");
    MapResourceCatalog catalog;
    catalog.area = area;
    const auto member = [&](std::size_t slot) {
        return area * TargetProfile::area_stride + slot;
    };
    std::vector<Dependency> shared;
    std::optional<Container> background;
    std::string background_hash;
    try {
        auto bytes = field.decoded(member(TargetProfile::background_resource_slot));
        background_hash = sha256(bytes);
        background = Container::parse(bytes, "BG");
        shared = shared_dependencies(GameProfile::field_archive(dump), *background,
                                     member(TargetProfile::background_resource_slot),
                                     background_hash, catalog.issues);
    } catch (const std::exception &error) {
        catalog.issues.push_back("Shared terrain dependencies: " + std::string(error.what()));
    }
    checkpoint(cancel);
    try {
        const auto resources_member = member(TargetProfile::static_resource_slot);
        auto bytes = field.decoded(resources_member);
        const auto hash = sha256(bytes);
        auto resources = Container::parse(bytes, "AS");
        for (std::size_t slot = 0; slot < resources.files.size(); ++slot) {
            checkpoint(cancel);
            MapResourceEntry entry;
            entry.area = area;
            entry.name = "Static model";
            entry.source =
                reference(GameProfile::field_archive(dump), resources_member, hash, {slot});
            try {
                auto object = Container::parse(resources.files[slot], "SM");
                entry.static_model = u16(object.files.at(0), 0);
                auto pack = ModelPack::parse(object.files.at(1));
                std::vector<Dependency> local;
                for (std::size_t i = 0; i < pack.resources.size(); ++i) {
                    const auto &resource = pack.resources[i];
                    if (resource.category == 1 || resource.category == 4) {
                        const auto name = resource.category == 1
                                              ? text(slice(resource.bytes, 40, 64))
                                              : decode_material_shader(resource.bytes).name;
                        local.push_back({resource.category == 1 ? Dependency::Kind::Texture
                                                                : Dependency::Kind::Shader,
                                         name,
                                         reference(GameProfile::field_archive(dump),
                                                   resources_member, hash, {slot, 1, i})});
                    }
                }
                std::vector<std::pair<Dependency::Kind, std::string>> needed;
                bool has_model = false;
                for (std::size_t i = 0; i < pack.resources.size(); ++i)
                    if (pack.resources[i].category == 0) {
                        const auto &resource = pack.resources[i];
                        if (!has_model)
                            entry.name = resource.name;
                        has_model = true;
                        geometry(entry, resource.bytes,
                                 reference(GameProfile::field_archive(dump), resources_member, hash,
                                           {slot, 1, i}),
                                 resource.name, needed, cancel);
                    }
                require(has_model, "Static resource contains no model");
                entry.geometry_complete = true;
                resolve(entry, needed, local, shared);
                for (auto motion_slot :
                     {TargetProfile::static_loop_motion, TargetProfile::static_daily_motion})
                    if (motion_slot < object.files.size() && !object.files[motion_slot].empty()) {
                        const auto name = motion_slot == TargetProfile::static_loop_motion
                                              ? "Loop motion"
                                              : "Daily motion";
                        entry.dependencies.push_back(
                            {Dependency::Kind::Motion, name,
                             reference(GameProfile::field_archive(dump), resources_member, hash,
                                       {slot, motion_slot})});
                        try {
                            auto motion = decode_material_motion(object.files[motion_slot]);
                            std::vector<std::pair<Dependency::Kind, std::string>> textures;
                            for (const auto &track : motion.tracks)
                                for (const auto &key : track.textures)
                                    textures.emplace_back(Dependency::Kind::Texture, key.texture);
                            resolve(entry, textures, local, shared);
                        } catch (const std::exception &error) {
                            issue(entry, std::string(name) + ": " + error.what());
                        }
                    }
            } catch (const std::exception &error) {
                checkpoint(cancel);
                issue(entry, error.what());
            }
            catalog.entries.push_back(std::move(entry));
        }
        std::map<std::uint16_t, std::size_t> counts;
        for (const auto &entry : catalog.entries)
            if (entry.static_model)
                ++counts[*entry.static_model];
        for (auto &entry : catalog.entries)
            if (entry.static_model && counts[*entry.static_model] > 1)
                issue(entry, "Static model identity is duplicated in this area");
    } catch (const std::exception &error) {
        checkpoint(cancel);
        catalog.issues.push_back("Static resources: " + std::string(error.what()));
    }
    checkpoint(cancel);
    try {
        auto layout =
            Container::parse(field.decoded(member(TargetProfile::terrain_layout_slot)), "TR");
        require(layout.files.size() == 1, "Unsupported terrain layout variants");
        const auto &grid = layout.files[0];
        const auto width = u32(grid, 0), height = u32(grid, 4);
        require(width <= 1024 && height <= 1024 && std::size_t(width) * height <= 65536,
                "Terrain grid is too large");
        slice(grid, 20, std::size_t(width) * height * TargetProfile::terrain_cell_size);
        Archive terrain(dump / TargetProfile::terrain_archive);
        std::set<unsigned> seen;
        for (std::size_t cell = 0; cell < std::size_t(width) * height; ++cell) {
            checkpoint(cancel);
            auto id = u16(grid, 20 + cell * TargetProfile::terrain_cell_size);
            if (id == 65535 || !seen.insert(id).second)
                continue;
            try {
                auto bytes = terrain.decoded(id);
                if (bytes.empty()) {
                    catalog.issues.push_back("Empty terrain member " + std::to_string(id));
                    continue;
                }
                auto hash = sha256(bytes);
                auto block = Container::parse(bytes, "BG");
                require(block.files.size() >= 6, "Incomplete terrain model slots");
                for (std::size_t part = 0; part < 6; ++part)
                    if (!block.files[part].empty())
                        terrain_model(catalog, block.files[part],
                                      reference(TargetProfile::terrain_archive, id, hash, {part}),
                                      MapResourceKind::BakedTerrain, shared, cancel);
            } catch (const std::exception &error) {
                checkpoint(cancel);
                catalog.issues.push_back("Terrain member " + std::to_string(id) + ": " +
                                         error.what());
            }
        }
        if (u32(grid, 16))
            catalog.issues.push_back(
                "Terrain replacement variants are not indexed; the catalog uses the base layout");
    } catch (const std::exception &error) {
        checkpoint(cancel);
        catalog.issues.push_back("Terrain resources: " + std::string(error.what()));
    }
    if (background && background->files.size() > 4 && !background->files[4].empty())
        try {
            auto distant = Container::parse(background->files[4], "BG");
            for (std::size_t i = 0; i < distant.files.size(); ++i)
                if (!distant.files[i].empty()) {
                    checkpoint(cancel);
                    terrain_model(catalog, distant.files[i],
                                  reference(GameProfile::field_archive(dump),
                                            member(TargetProfile::background_resource_slot),
                                            background_hash, {4, i}),
                                  MapResourceKind::DistantScenery, shared, cancel);
                }
        } catch (const std::exception &error) {
            checkpoint(cancel);
            catalog.issues.push_back("Distant scenery: " + std::string(error.what()));
        }
    checkpoint(cancel);
    return catalog;
}
Bytes read_map_source(const std::filesystem::path &dump, const MapSourceReference &source) {
    const auto &path = source.location.archive;
    require(path == GameProfile::field_archive(dump) || path == TargetProfile::terrain_archive,
            "Unsupported map source archive");
    auto bytes = Archive(dump / path).decoded(source.location.member);
    require(sha256(bytes) == source.member_hash,
            "Map source changed; rebuild the asset catalog before loading this resource");
    return asset_resource(bytes, source.location.path);
}
Environment preview_map_resource(const std::filesystem::path &dump, const MapResourceEntry &entry,
                                 std::atomic_bool *cancel) {
    const auto start = std::chrono::steady_clock::now();
    checkpoint(cancel);
    ModelDecoder decoder;
    decoder.cancel = cancel;
    using Key = std::pair<std::filesystem::path, std::size_t>;
    struct CachedMember {
        Bytes bytes;
        std::string hash;
    };
    std::map<Key, CachedMember> members;
    const auto read = [&](const MapSourceReference &source) {
        checkpoint(cancel);
        const auto &location = source.location;
        require(location.archive == GameProfile::field_archive(dump) ||
                    location.archive == TargetProfile::terrain_archive,
                "Unsupported map source archive");
        auto [it, added] = members.try_emplace(Key{location.archive, location.member});
        if (added) {
            it->second.bytes = Archive(dump / location.archive).decoded(location.member);
            it->second.hash = sha256(it->second.bytes);
            decoder.out.source_bytes += it->second.bytes.size();
        }
        require(it->second.hash == source.member_hash,
                "Map dependency changed; rebuild the asset catalog");
        return asset_resource(it->second.bytes, location.path);
    };
    read(entry.source);
    const std::string prefix = "resource/";
    for (const auto &dependency : entry.dependencies) {
        if (dependency.kind == Dependency::Kind::Model)
            continue;
        auto bytes = read(dependency.source);
        if (dependency.kind == Dependency::Kind::Texture) {
            decoder.texture(bytes, prefix);
            decoder.out.texture_sources[prefix + dependency.name] = dependency.source.location;
        }
        if (dependency.kind == Dependency::Kind::Shader) {
            ++decoder.out.shader_resources;
            decoder.shader(bytes, prefix);
            decoder.out.shader_sources[prefix + dependency.name] = dependency.source.location;
        }
        if (dependency.kind == Dependency::Kind::Motion)
            decoder.motion(bytes, dependency.name, prefix, dependency.name == "Daily motion");
    }
    for (const auto &dependency : entry.dependencies)
        if (dependency.kind == Dependency::Kind::Model) {
            decoder.source = std::make_shared<SceneModelSource>(dependency.source.location);
            decoder.placed_model(read(dependency.source), dependency.name, prefix);
        }
    require(!decoder.out.draws.empty(), "This resource has no supported preview geometry");
    for (auto &animation : decoder.out.material_animations)
        for (std::size_t track = 0; track < animation.motion.tracks.size(); ++track)
            for (std::size_t material = 0; material < decoder.out.materials.size(); ++material)
                if (animation.motion.tracks[track].material == decoder.out.materials[material].name)
                    animation.bindings.push_back({track, material});
    bool first = true;
    for (const auto &draw : decoder.out.draws)
        for (const auto &vertex : draw.vertices) {
            std::array<float, 3> point{vertex.x, vertex.y, vertex.z};
            if (first) {
                decoder.out.low = decoder.out.high = point;
                first = false;
            } else
                for (unsigned axis = 0; axis < 3; ++axis) {
                    decoder.out.low[axis] = std::min(decoder.out.low[axis], point[axis]);
                    decoder.out.high[axis] = std::max(decoder.out.high[axis], point[axis]);
                }
        }
    for (const auto &message : entry.issues)
        decoder.note(message);
    decoder.out.load_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    checkpoint(cancel);
    return std::move(decoder.out);
}
std::string MapResourceCatalog::report() const {
    std::ostringstream out;
    out << "Field area " << area << " resource catalog\n";
    out << "Geometry counts describe one resource, not its placed instances. Baked scenery needs "
           "extraction before prop placement.\n";
    std::vector<const MapResourceEntry *> sorted;
    for (const auto &entry : entries)
        sorted.push_back(&entry);
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto *a, const auto *b) {
        if (a->geometry_complete != b->geometry_complete)
            return a->geometry_complete > b->geometry_complete;
        return a->vertices < b->vertices;
    });
    for (const auto *entry : sorted) {
        out << "Entry " << std::size_t(entry - entries.data()) << " | "
            << (entry->kind == MapResourceKind::StaticModel    ? "Static model"
                : entry->kind == MapResourceKind::BakedTerrain ? "Baked terrain"
                                                               : "Distant scenery")
            << " | " << std::quoted(entry->name) << " | vertices " << entry->vertices
            << " | triangles " << entry->triangles << " | draws " << entry->draws << " | materials "
            << entry->materials << " | "
            << (entry->geometry_complete ? "geometry decoded" : "incomplete geometry") << '\n';
        out << "  Source " << entry->source.location.archive.generic_string() << " member "
            << entry->source.location.member << " path";
        for (auto part : entry->source.location.path)
            out << ' ' << part;
        out << " hash " << entry->source.member_hash;
        if (entry->static_model)
            out << " model ID " << *entry->static_model;
        out << '\n';
        for (const auto &message : entry->issues)
            out << "  Issue: " << message << '\n';
    }
    for (const auto &message : issues)
        out << "Issue: " << message << '\n';
    return out.str();
}
}
