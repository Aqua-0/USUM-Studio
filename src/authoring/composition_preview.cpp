#include "authoring/composition_document.h"
#include <cmath>
#include "scene/model_decoder.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <limits>

namespace studio {
AuthoredVertex authored_vertex(const SceneVertex &v) {
    return {{{v.x, v.y, v.z, v.nx, v.ny, v.nz, v.tx, v.ty, v.tz, v.u, v.v, v.u1, v.v1, v.u2, v.v2}},
            v.color};
}
SceneVertex scene_vertex(const AuthoredVertex &p) {
    const auto &v = p.values;
    SceneVertex result{v[0],  v[1],    v[2], v[9], v[10], v[11], v[12], v[13],
                       v[14], p.color, v[3], v[4], v[5],  v[6],  v[7],  v[8]};
    return result;
}
void validate_authored_mesh(const AuthoredMesh &mesh) {
    require(mesh.vertices.size() <= 65536 && mesh.indices.size() % 3 == 0 &&
                mesh.indices.size() <= 12000000,
            "Edited section exceeds the supported mesh size or has incomplete triangles");
    for (const auto &v : mesh.vertices)
        for (float value : v.values)
            require(std::isfinite(value) && std::abs(value) < 1e7f,
                    "Edited mesh contains an invalid position, normal or UV");
    for (auto i : mesh.indices)
        require(i < mesh.vertices.size(), "Edited mesh triangle references a missing vertex");
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto &a = mesh.vertices[mesh.indices[i]].values;
        const auto &b = mesh.vertices[mesh.indices[i + 1]].values;
        const auto &c = mesh.vertices[mesh.indices[i + 2]].values;
        double area = 0;
        for (unsigned j = 0; j < 3; ++j) {
            const auto k = (j + 1) % 3, l = (j + 2) % 3;
            const double n = (double(b[k]) - a[k]) * (double(c[l]) - a[l]) -
                             (double(b[l]) - a[l]) * (double(c[k]) - a[k]);
            area += n * n;
        }
        require(area > 1e-16,
                "Edited mesh contains a collapsed triangle; remove degenerate faces in Blender");
    }
}
Environment project_asset_resource_preview(View resource) {
    auto object = Container::parse(resource, "SM");
    require(object.files.size() == 29, "Invalid project asset resource");
    auto pack = ModelPack::parse(object.files.at(1));
    ModelDecoder decoder;
    for (const auto &r : pack.resources)
        if (r.category == 3 || r.category == 4)
            decoder.shader(r.bytes, "resource/");
    for (const auto &r : pack.resources)
        if (r.category == 1)
            decoder.texture(r.bytes, "resource/");
    for (std::size_t i = 0; i < pack.resources.size(); ++i) {
        const auto &r = pack.resources[i];
        if (r.category != 0)
            continue;
        require(Model::parse(r.bytes).bones == 0, "Project props must remain unskinned");
        decoder.source = std::make_shared<SceneModelSource>(SceneModelSource{{}, 0, {1, i}});
        decoder.placed_model(r.bytes, r.name, "resource/");
    }
    require(!decoder.out.draws.empty(), "Project asset has no geometry");
    for (unsigned slot : {TargetProfile::static_loop_motion, TargetProfile::static_daily_motion})
        if (!object.files.at(slot).empty())
            decoder.motion(object.files[slot], "Asset motion", "resource/",
                           slot == TargetProfile::static_daily_motion);
    for (auto &animation : decoder.out.material_animations)
        for (std::size_t t = 0; t < animation.motion.tracks.size(); ++t)
            for (std::size_t m = 0; m < decoder.out.materials.size(); ++m)
                if (animation.motion.tracks[t].material == decoder.out.materials[m].name)
                    animation.bindings.push_back({t, m});
    return std::move(decoder.out);
}
Environment project_asset_geometry(const Environment &original, const ProjectAsset &asset) {
    const auto source = asset.native_resource.empty()
                            ? original
                            : project_asset_resource_preview(asset.native_resource);
    if (asset.meshes.empty())
        return source;
    require(asset.meshes.size() == source.draws.size(),
            "Edited object material sections no longer match the source");
    auto result = source;
    for (std::size_t i = 0; i < asset.meshes.size(); ++i) {
        const auto &mesh = asset.meshes[i];
        validate_authored_mesh(mesh);
        auto &draw = result.draws[i];
        draw.vertices.clear();
        draw.indices.clear();
        for (const auto &v : mesh.vertices)
            draw.vertices.push_back(scene_vertex(v));
        for (auto index : mesh.indices)
            draw.indices.push_back(std::uint16_t(index));
    }
    return result;
}
std::string export_object_exchange(const Environment &source, const ProjectAsset &asset,
                                   const std::string &signature,
                                   const std::vector<std::string> &textures) {
    const auto geometry = project_asset_geometry(source, asset);
    require(asset.faces.size() == geometry.draws.size(), "Object source layout changed");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "USUMSTUDIO_OBJECT 1\nsignature " << std::quoted(signature) << "\nsections "
        << asset.faces.size() << '\n';
    for (std::size_t i = 0; i < asset.faces.size(); ++i) {
        const auto &draw = geometry.draws[i];
        out << "section " << i << ' ' << std::quoted(draw.mesh) << ' '
            << std::quoted(i < textures.size() ? textures[i] : "") << ' ' << draw.vertices.size()
            << ' ' << asset.faces[i].size() << '\n';
        for (const auto &v : draw.vertices) {
            auto p = authored_vertex(v);
            for (unsigned a = 0; a < 3; ++a)
                p.values[a] -= asset.pivot[a];
            for (float x : p.values)
                out << x << ' ';
            out << p.color << '\n';
        }
        for (auto face : asset.faces[i]) {
            require(face < draw.indices.size() / 3, "Object face no longer exists");
            out << draw.indices[face * 3] << ' ' << draw.indices[face * 3 + 1] << ' '
                << draw.indices[face * 3 + 2] << '\n';
        }
    }
    out << "end\n";
    return out.str();
}
ProjectAsset import_object_exchange(const std::string &text, const ProjectAsset &baseline,
                                    const std::string &signature) {
    require(text.size() <= 32 * 1024 * 1024, "Object exchange exceeds 32 MiB");
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    std::string word, token;
    unsigned version = 0;
    std::size_t count = 0;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_OBJECT" && version == 1,
            "Unsupported object exchange file");
    require(bool(in >> word >> std::quoted(token)) && word == "signature" && token == signature,
            "Object belongs to a different export; export this object again before importing");
    require(bool(in >> word >> count) && word == "sections" && count == baseline.faces.size(),
            "Edited material sections do not match the exported object");
    auto result = baseline;
    result.meshes.assign(count, {});
    result.faces.assign(count, {});
    std::size_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t section = 0, vertices = 0, faces = 0;
        std::string name, texture;
        require(bool(in >> word >> section >> std::quoted(name) >> std::quoted(texture) >>
                     vertices >> faces) &&
                    word == "section" && section == i && vertices <= 65536 &&
                    faces <= 4000000 - total,
                "Invalid edited object section");
        total += faces;
        auto &mesh = result.meshes[i];
        mesh.vertices.resize(vertices);
        mesh.indices.resize(faces * 3);
        for (auto &v : mesh.vertices) {
            for (auto &x : v.values)
                require(bool(in >> x), "Invalid edited vertex");
            require(bool(in >> v.color), "Invalid edited vertex color");
            for (unsigned a = 0; a < 3; ++a)
                v.values[a] += baseline.pivot[a];
        }
        for (auto &index : mesh.indices)
            require(bool(in >> index), "Invalid edited triangle");
        validate_authored_mesh(mesh);
        for (std::size_t f = 0; f < faces; ++f)
            result.faces[i].push_back(std::uint32_t(f));
    }
    require(total > 0, "Keep at least one object face");
    require(bool(in >> word) && word == "end" && !(in >> word),
            "Unexpected or missing object exchange data");
    return result;
}

void validate_project_asset(const ProjectAsset &asset, const MapResourceCatalog &catalog) {
    require((asset.source == ProjectAsset::no_source && !asset.native_resource.empty()) ||
                (asset.source < catalog.entries.size() &&
                 catalog.entries[asset.source].geometry_complete),
            "Project asset source is missing or unsupported");
    require(asset.name.find_first_not_of(" \t") != std::string::npos && asset.name.size() <= 120 &&
                asset.name.find_first_of("\r\n") == std::string::npos,
            "Asset name must contain 1 to 120 characters on one line");
    for (float value : asset.pivot)
        require(std::isfinite(value) && std::abs(value) < 1e7f,
                "Asset pivot is outside the supported map range");
    require(!asset.faces.empty() && asset.faces.size() <= 65536, "Invalid asset mesh sections");
    std::size_t count = 0;
    for (const auto &faces : asset.faces) {
        require(std::is_sorted(faces.begin(), faces.end()) &&
                    std::adjacent_find(faces.begin(), faces.end()) == faces.end(),
                "Asset faces must be unique and ordered");
        count += faces.size();
    }
    require(asset.meshes.empty() || asset.meshes.size() == asset.faces.size(),
            "Edited asset sections do not match its face selections");
    for (std::size_t i = 0; i < asset.meshes.size(); ++i) {
        validate_authored_mesh(asset.meshes[i]);
        for (auto f : asset.faces[i])
            require(f < asset.meshes[i].indices.size() / 3, "Edited asset face is missing");
    }
    require(count > 0 && count <= 4000000,
            "Keep at least one face; extraction supports up to four million faces per asset");
}
ProjectAsset import_new_project_asset(const ModelExchange &model, const ProjectAsset &baseline,
                                      const std::vector<std::size_t> &materials) {
    require(model.standalone && model.joints.empty(),
            "Export an unrigged model for a static map asset");
    require(materials.size() == model.meshes.size(),
            "Assign a game material to each imported part");
    auto result = baseline;
    result.meshes.assign(baseline.faces.size(), {});
    result.faces.assign(baseline.faces.size(), {});
    result.pivot = {};
    for (std::size_t i = 0; i < model.meshes.size(); ++i) {
        require(materials[i] < result.meshes.size(), "Choose an available map material");
        const auto &part = model.meshes[i];
        auto &mesh = result.meshes[materials[i]];
        auto offset = narrow(mesh.vertices.size());
        require(offset + part.vertices.size() <= 65536,
                "This material exceeds 65536 vertices; simplify it or assign parts to separate "
                "game materials");
        for (const auto &v : part.vertices) {
            for (auto weight : v.weights)
                require(weight == 0, "Static assets cannot contain bone weights");
            AuthoredVertex vertex;
            unsigned index = 0;
            for (unsigned c : {0u, 1u, 2u, 4u, 5u, 6u}) {
                require(part.formats[c][0] == 3, "New model attributes must use floating point");
                for (unsigned k = 0; k < (c < 3 ? 3u : 2u); ++k)
                    vertex.values[index++] = v.channels[c][k];
            }
            vertex.color = 0;
            for (unsigned k = 0; k < 4; ++k)
                vertex.color |= unsigned(std::round(std::clamp(v.channels[3][k], 0.f, 1.f) * 255))
                                << (8 * k);
            mesh.vertices.push_back(vertex);
        }
        for (auto index : part.indices)
            mesh.indices.push_back(offset + index);
    }
    for (std::size_t i = 0; i < result.meshes.size(); ++i) {
        validate_authored_mesh(result.meshes[i]);
        for (std::size_t face = 0; face < result.meshes[i].indices.size() / 3; ++face)
            result.faces[i].push_back(narrow(face));
    }
    return result;
}
ProjectAsset begin_project_asset(std::size_t source, const Environment &scene) {
    ProjectAsset asset;
    asset.source = source;
    asset.name = "Extracted object";
    asset.faces.resize(scene.draws.size());
    for (std::size_t i = 0; i < scene.draws.size(); ++i) {
        const auto &draw = scene.draws[i];
        require(draw.skeleton < 0 && !draw.character && draw.placement < 0,
                "Extraction currently supports static, unskinned resource geometry");
        require(draw.indices.size() % 3 == 0, "Resource has incomplete triangles");
        for (std::size_t face = 0; face < draw.indices.size() / 3; ++face)
            asset.faces[i].push_back(std::uint32_t(face));
    }
    return asset;
}
Environment project_asset_preview(const Environment &original_source, const ProjectAsset &asset) {
    const auto source = project_asset_geometry(original_source, asset);
    require(asset.faces.size() == source.draws.size(),
            "Project asset mesh layout changed; reopen with the original source");
    Environment result = source;
    result.draws.clear();
    result.spatial.regions.clear();
    result.placement_source.clear();
    result.placement_transforms.clear();
    result.skeletons.clear();
    result.visibility_animations.clear();
    result.weather_particles.clear();
    result.locations.clear();
    result.static_placements = result.character_placements = 0;
    for (float value : asset.pivot)
        require(std::isfinite(value) && std::abs(value) < 1e7f, "Invalid project asset pivot");
    result.low = {INFINITY, INFINITY, INFINITY};
    result.high = {-INFINITY, -INFINITY, -INFINITY};
    for (std::size_t i = 0; i < asset.faces.size(); ++i) {
        if (asset.faces[i].empty())
            continue;
        const auto &original = source.draws[i];
        require(original.skeleton < 0 && !original.character && original.placement < 0,
                "Extraction currently supports static, unskinned resource geometry");
        require(original.material < source.materials.size(), "Asset material is missing");
        auto draw = original;
        draw.vertices.clear();
        draw.indices.clear();
        std::map<std::uint16_t, std::uint16_t> vertices;
        for (auto face : asset.faces[i]) {
            require(std::size_t(face) < original.indices.size() / 3,
                    "Project asset face is missing from its source");
            for (unsigned corner = 0; corner < 3; ++corner) {
                const auto index = original.indices[std::size_t(face) * 3 + corner];
                require(index < original.vertices.size(), "Source triangle index is invalid");
                auto [mapped, fresh] =
                    vertices.try_emplace(index, std::uint16_t(draw.vertices.size()));
                if (fresh) {
                    require(draw.vertices.size() < 65536, "Asset mesh exceeds its index range");
                    auto vertex = original.vertices[index];
                    vertex.x -= asset.pivot[0];
                    vertex.y -= asset.pivot[1];
                    vertex.z -= asset.pivot[2];
                    const SpatialPoint point{vertex.x, vertex.y, vertex.z};
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        require(std::isfinite(point[axis]) && std::abs(point[axis]) < 1e7f,
                                "Extracted vertex is outside the supported range");
                        result.low[axis] = std::min(result.low[axis], point[axis]);
                        result.high[axis] = std::max(result.high[axis], point[axis]);
                    }
                    draw.vertices.push_back(vertex);
                }
                draw.indices.push_back(mapped->second);
            }
        }
        draw.name = asset.name + " / " + draw.mesh;
        result.draws.push_back(std::move(draw));
    }
    require(!result.draws.empty(), "Keep at least one face before saving an asset");
    return result;
}
void validate_object_collision(const ObjectCollisionBox &box) {
    for (auto v : box.offset)
        require(std::isfinite(v) && std::abs(v) < 1e7f,
                "Collision offset must be finite and within the map range");
    for (auto v : box.size)
        require(std::isfinite(v) && v > 0 && v < 1e6f,
                "Collision dimensions must be positive and smaller than 1,000,000 units");
}
ObjectCollisionBox approximate_object_collision(const Environment &model) {
    bool found = false;
    SpatialPoint low{}, high{};
    for (const auto &draw : model.draws)
        for (auto index : draw.indices) {
            require(index < draw.vertices.size(), "Object has an invalid triangle index");
            const auto &v = draw.vertices[index];
            SpatialPoint p{v.x, v.y, v.z};
            for (auto x : p)
                require(std::isfinite(x), "Object has invalid geometry");
            if (!found) {
                low = high = p;
                found = true;
            } else
                for (unsigned a = 0; a < 3; ++a) {
                    low[a] = std::min(low[a], p[a]);
                    high[a] = std::max(high[a], p[a]);
                }
        }
    require(found, "Object has no visible triangles to fit");
    ObjectCollisionBox box;
    box.offset = {(low[0] + high[0]) * .5f, low[1], (low[2] + high[2]) * .5f};
    for (unsigned a = 0; a < 3; ++a)
        box.size[a] = std::max(1.f, high[a] - low[a]);
    validate_object_collision(box);
    return box;
}
std::array<SpatialPoint, 8> object_collision_corners(const CompositionInstance &instance) {
    require(instance.collision.has_value(), "Object has no collision box");
    const auto &box = *instance.collision;
    validate_object_collision(box);
    auto matrix = composition_transform(instance.transform);
    std::array<SpatialPoint, 8> corners{};
    for (unsigned i = 0; i < 8; ++i) {
        SpatialPoint p{box.offset[0] + ((i & 1) ? 1 : -1) * box.size[0] * .5f,
                       box.offset[1] + ((i & 2) ? box.size[1] : 0),
                       box.offset[2] + ((i & 4) ? 1 : -1) * box.size[2] * .5f};
        for (unsigned a = 0; a < 3; ++a)
            corners[i][a] = matrix[a * 4] * p[0] + matrix[a * 4 + 1] * p[1] +
                            matrix[a * 4 + 2] * p[2] + matrix[a * 4 + 3];
    }
    return corners;
}
Matrix composition_transform(const PlacementState &state) {
    for (float value : state.position)
        require(std::isfinite(value) && std::abs(value) < 1e7f, "Invalid placement position");
    require(std::isfinite(state.turn) && std::abs(state.turn) <= 360000,
            "Invalid placement heading");
    const float c = std::cos(state.turn * .01745329252f), s = std::sin(state.turn * .01745329252f);
    return {c,  0, s, state.position[0], 0, 1, 0, state.position[1],
            -s, 0, c, state.position[2], 0, 0, 0, 1};
}
Environment compose_map_preview(const Environment &base,
                                const std::vector<CompositionInstance> &instances,
                                const std::map<std::size_t, Environment> &resources) {
    Environment result = base;
    struct Binding {
        std::size_t material = 0, skeleton = 0;
        std::string scope;
    };
    std::map<std::size_t, Binding> bindings;
    for (const auto &instance : instances) {
        const auto found = resources.find(instance.resource);
        require(found != resources.end(), "A placed resource has not finished loading");
        const auto &asset = found->second;
        auto [binding, added] = bindings.try_emplace(
            instance.resource, Binding{result.materials.size(), result.skeletons.size(),
                                       "composition/" + std::to_string(instance.resource) + "/"});
        const auto &map = binding->second;
        if (added) {
            const auto table_offset = result.lighting_tables.size();
            result.lighting_tables.insert(result.lighting_tables.end(),
                                          asset.lighting_tables.begin(),
                                          asset.lighting_tables.end());
            for (auto material : asset.materials) {
                material.resource_scope = map.scope + material.resource_scope;
                if (!material.texture.empty())
                    material.texture = map.scope + material.texture;
                for (auto &texture : material.texture_inputs)
                    if (!texture.empty())
                        texture = map.scope + texture;
                for (auto &table : material.reflection_tables)
                    if (table >= 0)
                        table += int(table_offset);
                result.materials.push_back(std::move(material));
            }
            for (const auto &[name, texture] : asset.textures)
                result.textures.emplace(map.scope + name, texture);
            for (const auto &[name, source] : asset.texture_sources)
                result.texture_sources.emplace(map.scope + name, source);
            for (const auto &[name, source] : asset.shader_sources)
                result.shader_sources.emplace(map.scope + name, source);
            for (const auto &[name, texture] : asset.texture_overrides)
                result.texture_overrides.emplace(map.scope + name, texture);
            result.skeletons.insert(result.skeletons.end(), asset.skeletons.begin(),
                                    asset.skeletons.end());
            for (auto animation : asset.material_animations) {
                animation.texture_prefix = map.scope + animation.texture_prefix;
                for (auto &target : animation.bindings)
                    target.material += map.material;
                result.material_animations.push_back(std::move(animation));
            }
            for (auto animation : asset.visibility_animations) {
                animation.scope = map.scope + animation.scope;
                result.visibility_animations.push_back(std::move(animation));
            }
            for (const auto &message : asset.diagnostics)
                result.diagnostics.push_back(message);
        }
        const auto &state = instance.transform;
        for (float value : state.position)
            require(std::isfinite(value) && std::abs(value) < 1e7f, "Invalid placement position");
        require(std::isfinite(state.turn), "Invalid placement heading");
        const float c = std::cos(state.turn * .01745329252f),
                    s = std::sin(state.turn * .01745329252f);
        const Matrix transform{c,  0, s, state.position[0], 0, 1, 0, state.position[1],
                               -s, 0, c, state.position[2], 0, 0, 0, 1};
        const int placement = int(result.placement_transforms.size());
        result.placement_transforms.push_back(transform);
        std::map<const SceneModelSource *, std::shared_ptr<const SceneModelSource>> sources;
        for (auto draw : asset.draws) {
            if (draw.source) {
                auto [source, fresh] = sources.try_emplace(draw.source.get());
                if (fresh)
                    source->second = std::make_shared<SceneModelSource>(*draw.source);
                draw.source = source->second;
            }
            draw.placement = placement;
            draw.scope = map.scope + draw.scope;
            draw.material += map.material;
            if (draw.skeleton >= 0)
                draw.skeleton += int(map.skeleton);
            draw.name = "Placed asset " + std::to_string(instance.id) + " / " + draw.name;
            for (const auto &vertex : draw.vertices) {
                const std::array<float, 3> point{c * vertex.x + s * vertex.z + state.position[0],
                                                 vertex.y + state.position[1],
                                                 -s * vertex.x + c * vertex.z + state.position[2]};
                for (unsigned axis = 0; axis < 3; ++axis) {
                    result.low[axis] = std::min(result.low[axis], point[axis]);
                    result.high[axis] = std::max(result.high[axis], point[axis]);
                }
            }
            result.draws.push_back(std::move(draw));
        }
    }
    result.static_placements += instances.size();
    if (std::any_of(instances.begin(), instances.end(), [](const auto &i) {
            return !i.collision;
        }))
        result.diagnostics.push_back(
            "Some added objects have no collision box. Fit collision in Objects when needed.");
    return result;
}
}
