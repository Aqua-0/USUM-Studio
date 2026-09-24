#include "authoring/object_export.h"
#include "formats/compression.h"
#include "audio/audio_document.h"
#include "compiler/model_build.h"
#include "compiler/terrain.h"
#include "assets/material_document.h"
#include "assets/asset_package.h"
#include "core/digest.h"
#include "scene/model_decoder.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <iomanip>
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
Bytes compile_owned_resource(const ProjectAsset &asset) {
    auto object = Container::parse(asset.native_resource, "SM");
    auto pack = ModelPack::parse(object.files.at(1));
    const auto original = project_asset_resource_preview(asset.native_resource);
    const auto preview = project_asset_preview(original, asset);
    bool unchanged = asset.meshes.empty() && asset.pivot == SpatialPoint{} &&
                     asset.faces.size() == original.draws.size();
    for (std::size_t i = 0; unchanged && i < asset.faces.size(); ++i) {
        unchanged = asset.faces[i].size() == original.draws[i].indices.size() / 3;
        for (std::size_t f = 0; unchanged && f < asset.faces[i].size(); ++f)
            unchanged = asset.faces[i][f] == f;
    }
    if (unchanged)
        return asset.native_resource;
    std::map<std::pair<std::size_t, std::string>, unsigned> counters;
    std::vector<unsigned> selected;
    for (std::size_t i = 0; i < original.draws.size(); ++i) {
        const auto &draw = original.draws[i];
        auto submesh = counters[{draw.source->path.back(), draw.mesh}]++;
        if (!asset.faces.at(i).empty())
            selected.push_back(submesh);
    }
    for (std::size_t r = 0; r < pack.resources.size(); ++r) {
        auto &resource = pack.resources[r];
        if (resource.category != 0)
            continue;
        auto source = Model::parse(resource.bytes);
        auto compiled = source;
        bool first = true;
        for (std::size_t i = 0; i < preview.draws.size(); ++i) {
            const auto &draw = preview.draws[i];
            if (draw.source->path.back() != r)
                continue;
            TerrainExportMesh mesh;
            mesh.name = "AuthoredMesh" + std::to_string(i);
            mesh.indices = draw.indices;
            for (const auto &v : draw.vertices)
                mesh.vertices.push_back({{v.x, v.y, v.z},
                                         {v.nx, v.ny, v.nz},
                                         {v.u, v.v},
                                         v.color,
                                         {v.u1, v.v1},
                                         {v.u2, v.v2},
                                         {v.tx, v.ty, v.tz}});
            compiled =
                Model::parse(append_rigid_meshes(compiled, preview.materials.at(draw.material).name,
                                                 source, draw.mesh, {mesh}, first, selected.at(i)));
            first = false;
        }
        require(!first, "Keep a mesh in each native model, or remove it in Studio");
        resource.bytes = std::move(compiled.original);
    }
    object.files[1] = pack_resources(pack.resources);
    return object.write(TargetProfile::resource_alignment);
}
Bytes compile_project_resource(const std::filesystem::path &dump, const MapResourceEntry &entry,
                               const ProjectAsset &asset, const Container &templates) {
    if (!asset.native_resource.empty())
        return compile_owned_resource(asset);
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
                    const auto location = "Mesh '" + draw.mesh + "', material '" + material +
                                          "', vertex " + std::to_string(i);
                    require(actual.color == expected.color,
                            location + ": vertex color changed during native encoding");
                    constexpr const char *components[] = {
                        "position X", "position Y", "position Z", "normal X",  "normal Y",
                        "normal Z",   "tangent X",  "tangent Y",  "tangent Z", "UV0 U",
                        "UV0 V",      "UV1 U",      "UV1 V",      "UV2 U",     "UV2 V"};
                    for (unsigned component = 0; component < actual.values.size(); ++component) {
                        const auto expected_value = expected.values[component];
                        const auto actual_value = actual.values[component];
                        const bool equal = component >= 3 && component < 9
                                               ? std::abs(actual_value - expected_value) <= 1e-5f
                                               : actual_value == expected_value;
                        require(equal, location + ": " + components[component] +
                                           " changed during native encoding (expected " +
                                           std::to_string(expected_value) + ", read back " +
                                           std::to_string(actual_value) + ")");
                    }
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
ModelDocument open_studio_object(const AssetPackage &package, const std::filesystem::path &dump) {
    auto asset = import_asset_package(encode_asset_package(package));
    auto model = project_asset_studio(dump, {}, asset);
    model.independent_asset = sha256(encode_asset_package(package));
    return model;
}
AssetPackage new_studio_object(const ModelDocument &material_template, const ModelExchange &geometry,
                              const std::vector<std::size_t> &materials, const std::string &name) {
    require(material_template.project_asset, "Choose a static object material template");
    require(geometry.standalone && geometry.joints.empty(),
            "For a rigged asset, open a character template in Studio and use Blender > Import from Blender");
    auto source = material_template;
    auto object = Container::parse(source.sources.at(0).original, "SM");
    auto pack = ModelPack::parse(object.files.at(1));
    auto selected = source.resources.at(source.material_resources.at(0)).path.at(1);
    for (std::size_t i = pack.resources.size(); i-- > 0;)
        if (pack.resources[i].category == 0 && i != selected)
            pack.resources.erase(pack.resources.begin() + i);
    object.files[1] = pack_resources(pack.resources);
    for (std::size_t i = 2; i < object.files.size(); ++i)
        object.files[i].clear();
    ProjectAsset asset;
    asset.source = ProjectAsset::no_source;
    asset.name = name;
    asset.native_resource = object.write(TargetProfile::resource_alignment);
    auto scene = project_asset_resource_preview(asset.native_resource);
    for (auto &draw : scene.draws) {
        std::vector<std::uint32_t> faces;
        for (std::uint32_t i = 0; i < draw.indices.size()/3; ++i) faces.push_back(i);
        asset.faces.push_back(std::move(faces));
    }
    MaterialDocument edited(project_asset_studio(source.dump, {}, asset));
    edited.import_new_model(geometry, materials);
    return decode_asset_package(export_studio_object(edited, name));
}
Bytes export_studio_object(const MaterialDocument &document, const std::string &name) {
    require(document.model.project_asset, "Open a static object asset to send it to the library");
    require(!name.empty(), "Give the object a name");
    ProjectAsset asset;
    asset.source = ProjectAsset::no_source;
    asset.name = name;
    asset = save_project_asset_studio(asset, document);
    return export_asset_package(document.model.dump, {}, asset);
}
Bytes export_asset_package(const std::filesystem::path &dump, const MapResourceCatalog &catalog,
                           const ProjectAsset &asset) {
    auto resource = compile_project_asset(dump, catalog, asset);
    auto object = Container::parse(resource, "SM");
    put16(object.files.at(0), 0, 0);
    resource = object.write(TargetProfile::resource_alignment);
    AssetPackage package;
    auto value = [](const std::string &s) { return Bytes(s.begin(), s.end()); };
    package["type"] = value("static");
    package["name"] = value(asset.name);
    package["static-resource"] = resource;
    auto portable = asset;
    portable.native_resource = resource;
    portable.source = ProjectAsset::no_source;
    portable.pivot = {};
    portable.meshes.clear();
    portable.faces.clear();
    const auto scene = project_asset_resource_preview(resource);
    for (const auto &draw : scene.draws) {
        std::vector<std::uint32_t> faces;
        for (std::uint32_t f = 0; f < draw.indices.size()/3; ++f) faces.push_back(f);
        portable.faces.push_back(std::move(faces));
    }
    const auto pack = ModelPack::parse(object.files.at(1));
    std::ostringstream paths;
    std::size_t index = 0;
    for (std::size_t i = 0; i < pack.resources.size(); ++i)
        if (pack.resources[i].category == 0) {
            MaterialDocument document(project_asset_studio(dump, catalog, portable, index));
            add_package_model(package, document, index);
            paths << index++ << ' ' << i << '\n';
        }
    package["model-paths"] = value(paths.str());
    return encode_asset_package(package);
}
ProjectAsset import_asset_package(View bytes) {
    const std::string modern = "USUMSTUDIO_ASSET 2\n";
    if (bytes.size() >= modern.size() && text(bytes.first(modern.size())) == modern) {
        const auto package = decode_asset_package(bytes);
        require(text(package.at("type")) == "static", "Import rigged models and Pokemon in Studio; new Blender geometry needs game material assignments");
        ProjectAsset asset;
        asset.name = text(package.at("name"));
        asset.source = ProjectAsset::no_source;
        auto object = Container::parse(compile_static_package(package), "SM");
        for (auto slot : {1u, 10u, 19u})
            if (!object.files.at(slot).empty())
                object.files[slot] = pack_resources(ModelPack::parse(object.files[slot]).resources);
        asset.native_resource = object.write(TargetProfile::resource_alignment);
        const auto scene = project_asset_resource_preview(asset.native_resource);
        for (const auto &draw : scene.draws) {
            std::vector<std::uint32_t> faces;
            for (std::uint32_t f = 0; f < draw.indices.size()/3; ++f) faces.push_back(f);
            asset.faces.push_back(std::move(faces));
        }
        validate_project_asset(asset, {});
        return asset;
    }
    const auto prefix = text(bytes.first(std::min<std::size_t>(bytes.size(), 1024)));
    const auto split = prefix.find("\n\n");
    require(split != std::string::npos, "Invalid or incomplete asset package header");
    std::istringstream header(prefix.substr(0, split));
    std::string magic, word, digest;
    unsigned version = 0;
    std::size_t size = 0;
    ProjectAsset asset;
    asset.source = ProjectAsset::no_source;
    require(bool(header >> magic >> version) && magic == "USUMSTUDIO_ASSET" && version == 1,
            "Unsupported asset package version");
    require(bool(header >> word >> std::quoted(asset.name)) && word == "name" &&
                bool(header >> word >> size) && word == "bytes" && bool(header >> word >> digest) &&
                word == "sha256" && !(header >> word),
            "Invalid asset package metadata");
    require(size == bytes.size() - split - 2, "Asset package is truncated or has trailing data");
    const auto payload = bytes.subspan(split + 2);
    require(sha256(payload) == digest, "Asset package checksum failed");
    asset.native_resource.assign(payload.begin(), payload.end());
    const auto scene = project_asset_resource_preview(payload);
    for (const auto &draw : scene.draws) {
        std::vector<std::uint32_t> faces;
        for (std::uint32_t i = 0; i < draw.indices.size() / 3; ++i)
            faces.push_back(i);
        asset.faces.push_back(std::move(faces));
    }
    validate_project_asset(asset, {});
    return asset;
}
Bytes compile_project_asset(const std::filesystem::path &dump, const MapResourceCatalog &catalog,
                            const ProjectAsset &asset) {
    validate_project_asset(asset, catalog);
    if (!asset.native_resource.empty())
        return compile_owned_resource(asset);
    Archive archive(dump / GameProfile::field_archive(dump));
    auto templates = Container::parse(archive.decoded(catalog.area * TargetProfile::area_stride +
                                                      TargetProfile::static_resource_slot),
                                      "AS");
    return compile_project_resource(dump, catalog.entries.at(asset.source), asset, templates);
}
ModelDocument project_asset_studio(const std::filesystem::path &dump,
                                   const MapResourceCatalog &catalog, const ProjectAsset &asset,
                                   std::size_t selected_model) {
    auto bytes = compile_project_asset(dump, catalog, asset);
    auto object = Container::parse(bytes, "SM");
    auto pack = ModelPack::parse(object.files.at(1));
    ModelDocument model;
    model.kind = ModelAssetKind::ArchiveModel;
    model.project_asset = true;
    model.dump = dump;
    model.area = int(catalog.area);
    model.name = asset.name;
    model.scene = std::make_shared<Environment>();
    model.sources.push_back(
        {GameProfile::field_archive(dump), asset.id, "Project asset", sha256(bytes), bytes});
    std::size_t model_index = 0;
    for (std::size_t i = 0; i < pack.resources.size(); ++i) {
        const auto &r = pack.resources[i];
        const auto index = model.resources.size();
        model.resources.push_back({0,
                                   {1, i},
                                   r.category == 0   ? "Model"
                                   : r.category == 1 ? "Texture"
                                                     : "Shader",
                                   r.name});
        if (r.category == 0) {
            if (model_index++ == selected_model)
                model.material_resources.push_back(index);
        } else if (r.category == 1)
            model.texture_resources[model.texture_prefix + text(slice(r.bytes, 40, 64))] = index;
    }
    for (unsigned slot : {TargetProfile::static_loop_motion, TargetProfile::static_daily_motion}) {
        if (object.files.at(slot).empty())
            continue;
        AssetMotion motion;
        motion.name = slot == TargetProfile::static_daily_motion ? "Daily motion" : "Area loop";
        motion.daily = slot == TargetProfile::static_daily_motion;
        motion.resource = model.resources.size();
        model.resources.push_back({0, {slot}, "Motion", motion.name});
        model.motions.push_back(motion);
    }
    require(!model.material_resources.empty(), "Project asset has no native model");
    return reload_map_model(model, {});
}
ProjectAsset save_project_asset_studio(const ProjectAsset &asset,
                                       const MaterialDocument &document) {
    require(document.model.project_asset, "This Studio document is not a project asset");
    auto members = document.compiled_members();
    const auto &source = document.model.sources.at(0);
    auto found = members.find(source.member);
    auto bytes = found == members.end() ? source.original : found->second;
    auto object = Container::parse(bytes, "SM");
    for (auto slot : {1u, 10u, 19u})
        if (!object.files.at(slot).empty())
            object.files[slot] = pack_resources(ModelPack::parse(object.files[slot]).resources);
    bytes = object.write(TargetProfile::resource_alignment);
    auto preview = project_asset_resource_preview(bytes);
    auto result = asset;
    result.native_resource = std::move(bytes);
    result.pivot = {};
    result.meshes.clear();
    result.faces.clear();
    for (const auto &draw : preview.draws) {
        std::vector<std::uint32_t> faces;
        for (std::uint32_t i = 0; i < draw.indices.size() / 3; ++i)
            faces.push_back(i);
        result.faces.push_back(std::move(faces));
    }
    return result;
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
        Bytes bytes;
        unsigned id = 0;
        if (asset) {
            bytes = compile_project_asset(dump, document.catalog(), *asset);
        } else {
            const auto &entry = document.catalog().entries.at(instance.resource);
            require(entry.reusable_static(), "Choose a complete static model resource");
            read_map_source(dump, entry.source);
            if (entry.area == document.catalog().area) {
                models[instance.resource] = *entry.static_model;
                continue;
            }
            bytes = portable_static_resource(dump, entry);
            id = *entry.static_model;
        }
        auto object = Container::parse(bytes, "SM");
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
