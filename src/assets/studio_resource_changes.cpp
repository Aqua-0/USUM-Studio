#include "assets/material_document.h"
#include <algorithm>
#include <tuple>
namespace studio {
namespace {
bool same_vertex(const SceneVertex &a, const SceneVertex &b) {
    return std::tie(a.x, a.y, a.z, a.u, a.v, a.u1, a.v1, a.u2, a.v2, a.color, a.nx, a.ny, a.nz,
                    a.tx, a.ty, a.tz, a.joints,
                    a.weights) == std::tie(b.x, b.y, b.z, b.u, b.v, b.u1, b.v1, b.u2, b.v2, b.color,
                                           b.nx, b.ny, b.nz, b.tx, b.ty, b.tz, b.joints, b.weights);
}
bool same_mesh(const SceneDraw &a, const SceneDraw &b, const Environment &before,
               const Environment &after) {
    return a.palette == b.palette && a.indices == b.indices &&
           a.vertices.size() == b.vertices.size() &&
           before.materials.at(a.material).name == after.materials.at(b.material).name &&
           std::equal(a.vertices.begin(), a.vertices.end(), b.vertices.begin(), same_vertex);
}
bool same_joint(const Joint &a, const Joint &b) {
    return a.parent_name == b.parent_name && a.flags == b.flags && a.scale == b.scale &&
           a.rotation == b.rotation && a.translation == b.translation;
}
}
std::vector<StudioResourceChange> MaterialDocument::resource_changes() const {
    const MaterialDocument *baseline = this;
    while (baseline->structural_parent_)
        baseline = baseline->structural_parent_.get();
    auto before = studio_resources(baseline->model), after = studio_resources(model);
    std::map<std::string, const StudioResource *> old;
    for (auto &row : before)
        old[row.key] = &row;
    std::vector<StudioResourceChange> result;
    auto add = [&](const StudioResource &row, std::string detail, bool removed = false) {
        auto name = row.name;
        if (row.kind == StudioResourceKind::Mesh)
            name += " / part " + std::to_string(row.index + 1);
        result.push_back({row.kind, row.key, std::move(name), std::move(detail), removed});
    };
    for (auto &row : after) {
        auto found = old.find(row.key);
        if (found == old.end()) {
            add(row, "Added");
            continue;
        }
        const auto &original = *found->second;
        old.erase(found);
        bool modified = false;
        switch (row.kind) {
        case StudioResourceKind::Material:
            modified = edits_.at(row.index) != baseline->initial_.at(original.index);
            break;
        case StudioResourceKind::Texture:
            if (model.texture_resources.contains(row.name) &&
                baseline->model.texture_resources.contains(original.name)) {
                auto &link = baseline->model.resources.at(
                    baseline->model.texture_resources.at(original.name));
                auto bytes =
                    asset_resource(baseline->model.sources.at(link.source).original, link.path);
                modified = texture_resource(row.name) != bytes;
            }
            break;
        case StudioResourceKind::Mesh:
            if (baseline != this)
                modified = !same_mesh(baseline->model.scene->draws.at(original.index),
                                      model.scene->draws.at(row.index), *baseline->model.scene,
                                      *model.scene);
            break;
        case StudioResourceKind::Bone:
            if (baseline != this)
                modified =
                    !same_joint(baseline->model.scene->skeletons.at(original.skeleton)
                                    .joints.at(original.index),
                                model.scene->skeletons.at(row.skeleton).joints.at(row.index));
            break;
        case StudioResourceKind::Motion: {
            auto &motion = model.motions.at(row.index);
            auto &prior = baseline->model.motions.at(original.index);
            modified = motion.skeletal != prior.skeletal || motion.material != prior.material ||
                       motion.visibility != prior.visibility;
            if (model.area >= 0 && motion.error.empty()) {
                auto &link = baseline->model.resources.at(prior.resource);
                auto bytes =
                    asset_resource(baseline->model.sources.at(link.source).original, link.path);
                modified = motion.material != decode_material_motion(bytes);
            }
            break;
        }
        default:
            break;
        }
        if (modified)
            add(row, "Modified");
    }
    for (auto &[key, row] : old)
        add(*row, "Removed", true);
    auto other = [&](const char *key, const char *name, const char *detail) {
        result.push_back({StudioResourceKind::Other, key, name, detail});
    };
    if (!refresh_edits_.empty())
        other("refresh", "Refresh regions", "Modified region masks");
    if (feeding_edit_ || !camera_edit_.empty())
        other("refresh-camera", "Refresh settings", "Modified feeding or camera settings");
    bool rebuilt = false;
    if (baseline != this)
        for (auto &source : model.sources) {
            auto original = std::find_if(
                baseline->model.sources.begin(), baseline->model.sources.end(), [&](auto &s) {
                    return s.archive == source.archive && s.member == source.member &&
                           s.subfile == source.subfile;
                });
            rebuilt |=
                original == baseline->model.sources.end() || original->original != source.original;
        }
    if (rebuilt)
        other(
            "source-data", "Rebuilt source resources",
            "Includes structural or attachment data; individual resource changes are listed above");
    return result;
}
}
