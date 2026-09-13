#include "assets/material_document.h"
#include "assets/model_library.h"
#include "core/digest.h"
#include "scene/model_decoder.h"
#include <map>
#include <algorithm>
namespace studio {
ModelDocument reload_map_model(const ModelDocument &source,
                               const std::map<std::size_t, Bytes> &members) {
    auto result = source;
    const auto &model_link = result.resources.at(result.material_resources.front());
    const auto archive = result.sources.at(model_link.source).archive;
    for (auto &member : result.sources)
        if (member.archive == archive)
            if (auto replacement = members.find(member.member); replacement != members.end()) {
                member.original = replacement->second;
                member.hash = sha256(member.original);
            }
    ModelDecoder decoder;
    decoder.keep_skeleton = true;
    decoder.out.textures = source.scene->textures;
    for (const auto &resource : result.resources)
        if (resource.role == "Shader")
            decoder.shader(
                asset_resource(result.sources.at(resource.source).original, resource.path),
                result.texture_prefix);
    for (const auto &[name, index] : result.texture_resources) {
        const auto &resource = result.resources.at(index);
        decoder.out.textures[name] = decode_field_texture(
            asset_resource(result.sources.at(resource.source).original, resource.path));
    }
    const auto &member = result.sources.at(model_link.source);
    decoder.source = std::make_shared<SceneModelSource>(
        SceneModelSource{archive, member.member, model_link.path});
    decoder.skeletal_motions[result.texture_prefix] = {SkeletalMotion{}, false};
    decoder.placed_model(asset_resource(member.original, model_link.path), result.name,
                         result.texture_prefix);
    require(!decoder.out.draws.empty(), "Map model has no supported preview geometry");
    result.draw_resources.assign(decoder.out.draws.size(), result.material_resources.front());
    result.material_resources.assign(decoder.out.materials.size(),
                                     result.material_resources.front());
    result.native_meshes.clear();
    for (auto &motion : result.motions) {
        const auto &resource = result.resources.at(motion.resource);
        const auto &motion_source = result.sources.at(resource.source);
        auto bytes = asset_resource(motion_source.original, resource.path);
        motion.material = decode_material_motion(bytes);
        decoder.motion(bytes, motion.name, result.texture_prefix, motion.daily,
                       std::make_shared<SceneModelSource>(SceneModelSource{
                           motion_source.archive, motion_source.member, resource.path}));
    }
    for (auto &animation : decoder.out.material_animations)
        for (std::size_t track = 0; track < animation.motion.tracks.size(); ++track)
            for (std::size_t material = 0; material < decoder.out.materials.size(); ++material)
                if (animation.motion.tracks[track].material == decoder.out.materials[material].name)
                    animation.bindings.push_back({track, material});
    for (std::size_t i = 0; i < decoder.out.skeletons.size() && i < source.scene->skeletons.size();
         ++i) {
        auto &rig = decoder.out.skeletons[i];
        const auto &original = source.scene->skeletons[i];
        rig.motion = original.motion;
        rig.overlays = original.overlays;
        rig.daily = original.daily;
        rig.seconds_offset = original.seconds_offset;
        auto bind = [&](const SkeletalMotion &motion) {
            std::vector<int> tracks;
            for (const auto &joint : rig.joints) {
                auto found = std::find_if(motion.tracks.begin(), motion.tracks.end(),
                                          [&](const auto &track) {
                                              return track.name == joint.name;
                                          });
                tracks.push_back(found == motion.tracks.end() ? -1
                                                              : int(found - motion.tracks.begin()));
            }
            return tracks;
        };
        rig.tracks = bind(rig.motion);
        for (auto &overlay : rig.overlays)
            overlay.tracks = bind(overlay.motion);
    }
    decoder.out.visibility_animations = source.scene->visibility_animations;
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
    result.scene = std::make_shared<Environment>(std::move(decoder.out));
    result.scene->diagnostics.push_back("Edits affect all placements using this resource.");
    return result;
}
ModelDocument isolate_map_model(const Environment &scene, int selected,
                                const std::filesystem::path &dump, int area) {
    require(selected >= 0 && std::size_t(selected) < scene.draws.size(), "Select a map mesh first");
    auto &selected_draw = scene.draws[selected];
    require(selected_draw.player < 0, "Player models cannot be sent from Maps to Studio");
    require(bool(selected_draw.source), "This model has no editable source");
    if (selected_draw.character) {
        const auto &link = *selected_draw.source;
        require(link.path.size() >= 2 && link.path[link.path.size() - 2] == 0,
                "Invalid character model source");
        std::vector<std::size_t> container_path(link.path.begin(), link.path.end() - 2);
        auto doc =
            load_library_model(dump, scene.archive_sources.resolve(dump, link.archive),
                               ModelCategory::FieldCharacters, {link.member, selected_draw.name},
                               nullptr, "model/", nullptr, link.path.back(), container_path);
        doc.sources.front().archive = link.archive;
        doc.archive_sources = scene.archive_sources;
        doc.originating_map = area;
        return doc;
    }
    auto source = selected_draw.source;
    ModelDocument doc;
    doc.dump = dump;
    doc.archive_sources = scene.archive_sources;
    doc.area = area;
    doc.name = selected_draw.name;
    doc.texture_prefix = selected_draw.scope;
    doc.scene = std::make_shared<Environment>();
    auto &out = *doc.scene;
    Archive archive(doc.archive_sources.resolve(dump, source->archive));
    auto member = archive.decoded(source->member);
    doc.sources.push_back(
        {source->archive, source->member, "Map model", sha256(member), std::move(member)});
    doc.resources.push_back({0, source->path, "Model", doc.name});
    auto model = Model::parse(asset_resource(doc.sources[0].original, source->path));
    std::map<std::size_t, std::size_t> materials, rigs;
    out.lighting_tables = scene.lighting_tables;
    std::optional<ModelDecoder> source_decoder;
    auto unused_material = [&](const std::string &name) {
        if (!source_decoder) {
            source_decoder.emplace();
            auto &decoder = *source_decoder;
            for (auto &[key, link] : scene.shader_sources) {
                if (!key.starts_with(doc.texture_prefix) && !key.starts_with("terrain/"))
                    continue;
                std::size_t index = 0;
                for (; index < doc.sources.size(); ++index)
                    if (doc.sources[index].archive == link.archive &&
                        doc.sources[index].member == link.member)
                        break;
                if (index == doc.sources.size()) {
                    Archive shaders(doc.archive_sources.resolve(dump, link.archive));
                    auto bytes = shaders.decoded(link.member);
                    doc.sources.push_back(
                        {link.archive, link.member, "Shaders", sha256(bytes), std::move(bytes)});
                }
                auto bytes = asset_resource(doc.sources[index].original, link.path);
                auto shader = decode_material_shader(bytes);
                decoder.shaders[key] = std::move(shader);
            }
            decoder.model(asset_resource(doc.sources[0].original, source->path), doc.name,
                          doc.texture_prefix);
            auto table_offset = int(out.lighting_tables.size());
            for (auto &material : decoder.out.materials)
                for (auto &table : material.reflection_tables)
                    if (table >= 0)
                        table += table_offset;
            out.lighting_tables.insert(out.lighting_tables.end(),
                                       decoder.out.lighting_tables.begin(),
                                       decoder.out.lighting_tables.end());
        }
        auto &decoded = source_decoder->out.materials;
        auto found = std::find_if(decoded.begin(), decoded.end(), [&](const auto &material) {
            return material.name == name;
        });
        require(found != decoded.end(), "Map model material is missing from its source: " + name);
        return *found;
    };
    for (auto &name : model.names[2]) {
        auto found = std::find_if(scene.draws.begin(), scene.draws.end(), [&](const auto &draw) {
            return draw.source == source && scene.materials[draw.material].name == name;
        });
        if (found != scene.draws.end()) {
            materials[found->material] = out.materials.size();
            out.materials.push_back(scene.materials[found->material]);
        } else {
            out.materials.push_back(unused_material(name));
        }
        doc.material_resources.push_back(0);
    }
    for (auto &draw : scene.draws)
        if (draw.source == source) {
            auto d = draw;
            d.material = materials.at(draw.material);
            d.placement = -1;
            d.source = source;
            if (d.skeleton >= 0) {
                auto [it, added] = rigs.emplace(std::size_t(d.skeleton), out.skeletons.size());
                if (added)
                    out.skeletons.push_back(scene.skeletons.at(d.skeleton));
                d.skeleton = int(it->second);
            }
            out.draws.push_back(std::move(d));
            doc.draw_resources.push_back(0);
        }
    for (auto &m : out.materials)
        for (auto &texture : m.texture_inputs)
            if (!texture.empty() && scene.textures.contains(texture))
                out.textures.emplace(texture, scene.textures.at(texture));
    out.low = scene.low;
    out.high = scene.high;
    for (auto &[name, image] : scene.textures)
        if (name.starts_with(doc.texture_prefix))
            out.textures.try_emplace(name, image);
    for (auto &[name, image] : out.textures) {
        auto found = scene.texture_sources.find(name);
        if (found == scene.texture_sources.end())
            continue;
        auto &link = found->second;
        std::size_t source_index = 0;
        for (; source_index < doc.sources.size(); ++source_index)
            if (doc.sources[source_index].archive == link.archive &&
                doc.sources[source_index].member == link.member)
                break;
        if (source_index == doc.sources.size()) {
            Archive textures(doc.archive_sources.resolve(dump, link.archive));
            auto bytes = textures.decoded(link.member);
            doc.sources.push_back(
                {link.archive, link.member, "Textures", sha256(bytes), std::move(bytes)});
        }
        doc.texture_resources[name] = doc.resources.size();
        doc.resources.push_back({source_index, link.path, "Texture", name});
        if (auto edit = scene.texture_overrides.find(name); edit != scene.texture_overrides.end())
            out.texture_overrides[name] = edit->second;
    }
    std::set<std::string> shader_names;
    for (auto &material : out.materials)
        for (auto &name : {material.vertex_shader, material.fragment_shader})
            if (shader_names.insert(name).second) {
                auto found = scene.shader_sources.find(doc.texture_prefix + name);
                if (found == scene.shader_sources.end())
                    continue;
                auto &link = found->second;
                std::size_t source_index = 0;
                for (; source_index < doc.sources.size(); ++source_index)
                    if (doc.sources[source_index].archive == link.archive &&
                        doc.sources[source_index].member == link.member)
                        break;
                if (source_index == doc.sources.size()) {
                    Archive shaders(doc.archive_sources.resolve(dump, link.archive));
                    auto bytes = shaders.decoded(link.member);
                    doc.sources.push_back(
                        {link.archive, link.member, "Shaders", sha256(bytes), std::move(bytes)});
                }
                doc.resources.push_back({source_index, link.path, "Shader", name});
            }
    for (auto &animation : scene.material_animations) {
        if (animation.texture_prefix != doc.texture_prefix)
            continue;
        MaterialAnimation a = animation;
        a.bindings.clear();
        for (auto binding : animation.bindings)
            if (materials.contains(binding.material))
                a.bindings.push_back({binding.track, materials.at(binding.material)});
        if (animation.source) {
            auto &link = *animation.source;
            std::size_t source_index = 0;
            for (; source_index < doc.sources.size(); ++source_index)
                if (doc.sources[source_index].archive == link.archive &&
                    doc.sources[source_index].member == link.member)
                    break;
            if (source_index == doc.sources.size()) {
                Archive archive(doc.archive_sources.resolve(dump, link.archive));
                auto bytes = archive.decoded(link.member);
                doc.sources.push_back({link.archive, link.member, "Material motions", sha256(bytes),
                                       std::move(bytes)});
            }
            AssetMotion motion;
            motion.name = animation.name;
            motion.daily = animation.daily;
            motion.slot = unsigned(doc.motions.size());
            motion.resource = doc.resources.size();
            doc.resources.push_back({source_index, link.path, "Material motion", motion.name});
            motion.material = animation.motion;
            motion.material.looping =
                decode_material_motion(
                    asset_resource(doc.sources[source_index].original, link.path))
                    .looping;
            doc.motions.push_back(std::move(motion));
        }
        out.material_animations.push_back(std::move(a));
    }
    if (!doc.motions.empty())
        doc.motion = 0;
    for (auto &animation : scene.visibility_animations)
        if (animation.scope == doc.texture_prefix)
            out.visibility_animations.push_back(animation);
    out.diagnostics.push_back("Edits affect all placements using this resource.");
    return doc;
}
}
