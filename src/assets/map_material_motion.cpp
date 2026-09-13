#include "assets/material_document.h"
#include "assets/material_motion.h"
#include "core/digest.h"
#include <algorithm>
namespace studio {
void MaterialDocument::set_map_motion(std::size_t index, Bytes bytes) {
    require(model.area >= 0, "Expected a map material motion");
    auto &motion = model.motions.at(index);
    auto &link = model.resources.at(motion.resource);
    auto original = asset_resource(model.sources.at(link.source).original, link.path);
    auto before = decode_material_motion(original), after = decode_material_motion(bytes);
    require(replace_material_motion(original, after) == bytes,
            "Map motion changes must preserve the source's other sections");
    auto local = [&](const MaterialTrack &t) {
        return std::any_of(model.scene->materials.begin(), model.scene->materials.end(),
                           [&](auto &m) {
                               return m.name == t.material;
                           });
    };
    std::vector<MaterialTrack> old_foreign, new_foreign;
    for (auto &t : before.tracks)
        if (!local(t))
            old_foreign.push_back(t);
    for (auto &t : after.tracks) {
        if (!local(t)) {
            new_foreign.push_back(t);
            continue;
        }
        if (t.kind != MaterialTrack::Kind::ConstantColor) {
            auto material = std::find_if(model.scene->materials.begin(),
                                         model.scene->materials.end(), [&](auto &m) {
                                             return m.name == t.material;
                                         });
            require(t.slot < 3 && !material->texture_inputs[t.slot].empty(),
                    "Choose a texture unit already used by this material");
        }
        for (auto &key : t.textures)
            require(model.scene->textures.contains(model.texture_prefix + key.texture),
                    "Texture is not loaded with this map resource");
    }
    auto sort = [](auto &tracks) {
        std::sort(tracks.begin(), tracks.end(), [](auto &a, auto &b) {
            return std::tie(a.kind, a.material, a.slot) < std::tie(b.kind, b.material, b.slot);
        });
    };
    sort(old_foreign);
    sort(new_foreign);
    require(old_foreign == new_foreign,
            "This motion also animates other map materials; their tracks must remain unchanged");
    if (bytes == original)
        map_motion_edits_.erase(index);
    else
        map_motion_edits_[index] = std::move(bytes);
    touched_map_motions_.insert(index);
    synchronize_map_motions();
    ++revision_;
}
void MaterialDocument::synchronize_map_motions() {
    if (model.area < 0)
        return;
    for (auto index : touched_map_motions_) {
        auto &motion = model.motions.at(index);
        auto &link = model.resources.at(motion.resource);
        auto found = map_motion_edits_.find(index);
        motion.material = decode_material_motion(
            found == map_motion_edits_.end()
                ? asset_resource(model.sources.at(link.source).original, link.path)
                : found->second);
    }
    apply_map_motions(*model.scene);
}
void MaterialDocument::apply_map_motions(Environment &scene) const {
    for (auto index : touched_map_motions_) {
        auto &motion = model.motions.at(index);
        auto &link = model.resources.at(motion.resource);
        auto &source = model.sources.at(link.source);
        for (auto &animation : scene.material_animations) {
            if (!animation.source || animation.source->archive != source.archive ||
                animation.source->member != source.member || animation.source->path != link.path)
                continue;
            animation.motion = motion.material;
            animation.motion.looping = !animation.daily;
            animation.bindings.clear();
            for (unsigned t = 0; t < animation.motion.tracks.size(); ++t)
                for (unsigned m = 0; m < scene.materials.size(); ++m)
                    if (scene.materials[m].resource_scope == animation.texture_prefix &&
                        scene.materials[m].name == animation.motion.tracks[t].material)
                        animation.bindings.push_back({t, m});
        }
    }
}
Bytes MaterialDocument::merge_map_motion(std::size_t index, View destination) const {
    auto &link = model.resources.at(model.motions.at(index).resource);
    auto original = asset_resource(model.sources.at(link.source).original, link.path),
         current = asset_resource(destination, link.path);
    auto found = map_motion_edits_.find(index);
    auto desired = found == map_motion_edits_.end() ? original : found->second;
    auto wrapped = asset_resource(
        replace_asset_resource(model.sources.at(link.source).original, link.path, desired),
        link.path);
    require(current == original || current == desired || current == wrapped ||
                accepted_map_motions_[index].contains(sha256(current)),
            "This map motion has other changes; reload that version or choose a fresh override");
    return replace_asset_resource(destination, link.path, desired);
}
}
