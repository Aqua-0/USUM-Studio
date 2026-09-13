#include "assets/pokemon_shadow.h"
#include "assets/skeleton_edit.h"
#include "assets/material_document.h"
#include <cmath>
#include <algorithm>
namespace studio {
namespace {
bool same_joint(const Joint &a, const Joint &b) {
    return a.name == b.name && a.parent == b.parent && a.flags == b.flags && a.scale == b.scale &&
           a.rotation == b.rotation && a.translation == b.translation;
}
bool same_skeleton(const std::vector<Joint> &a, const std::vector<Joint> &b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), same_joint);
}
}
void validate_pokemon_shadow(View main, View shadow) {
    auto body = SkinnedModel::parse(main), cast = SkinnedModel::parse(shadow);
    require(cast.joints.size() <= body.joints.size(),
            "Shadow has bones missing from the main model");
    for (std::size_t i = 0; i < cast.joints.size(); ++i)
        require(body.joints[i].name == cast.joints[i].name &&
                    body.joints[i].parent == cast.joints[i].parent,
                "Shadow bone layout differs from the main model: " + cast.joints[i].name);
}
Bytes update_pokemon_shadow(View original, View replacement, bool editing_shadow) {
    auto before = Container::parse(original, "PC"), after = Container::parse(replacement, "PC");
    if (after.files.size() < 2 || after.files[1].empty())
        return {replacement.begin(), replacement.end()};
    auto body = SkinnedModel::parse(after.files[0]);
    if (editing_shadow) {
        require(same_skeleton(SkinnedModel::parse(before.files[0]).joints, body.joints) &&
                    same_skeleton(SkinnedModel::parse(before.files[1]).joints,
                                  SkinnedModel::parse(after.files[1]).joints),
                "Edit shared bones on the main model, then stage and reload the shadow");
        validate_pokemon_shadow(after.files[0], after.files[1]);
        return {replacement.begin(), replacement.end()};
    }
    auto previous = SkinnedModel::parse(before.files[0]);
    if (same_skeleton(previous.joints, body.joints))
        return {replacement.begin(), replacement.end()};
    validate_pokemon_shadow(before.files[0], before.files[1]);
    auto joints = body.joints;
    auto shadow = SkinnedModel::parse(after.files[1]);
    for (std::size_t i = 0; i < shadow.joints.size(); ++i)
        joints[i].flags = shadow.joints[i].flags;
    auto updated = replace_skeleton(after.files[1], joints);
    validate_pokemon_shadow(after.files[0], updated);
    return replace_asset_resource(replacement, {1}, updated);
}
}
