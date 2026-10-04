#include "assets/studio_resources.h"
#include <algorithm>
#include <set>
namespace studio {
const char *studio_resource_kind_name(StudioResourceKind kind) {
    switch (kind) {
    case StudioResourceKind::Mesh:
        return "Mesh";
    case StudioResourceKind::Material:
        return "Material";
    case StudioResourceKind::Texture:
        return "Texture";
    case StudioResourceKind::Bone:
        return "Bone";
    case StudioResourceKind::Motion:
        return "Motion";
    default:
        return "Model data";
    }
}
void StudioResourceHistory::visit(const std::string &key) {
    if (key.empty() || key == current())
        return;
    if (!entries_.empty())
        entries_.resize(position_ + 1);
    entries_.push_back(key);
    if (entries_.size() > 100)
        entries_.erase(entries_.begin());
    position_ = entries_.size() - 1;
}
std::string StudioResourceHistory::back() {
    if (can_back())
        --position_;
    return current();
}
std::string StudioResourceHistory::forward() {
    if (can_forward())
        ++position_;
    return current();
}
std::vector<StudioResource> studio_resources(const ModelDocument &model) {
    std::vector<StudioResource> rows;
    if (!model.scene)
        return rows;
    auto &scene = *model.scene;
    std::map<std::string, unsigned> occurrences;
    auto add = [&](StudioResourceKind kind, const std::string &name, int index, std::string detail,
                   int skeleton = -1, std::string identity = {}) {
        if (identity.empty())
            identity = name;
        std::string base = std::to_string(int(kind)) + "/" + identity;
        auto ordinal = occurrences[base]++;
        rows.push_back(
            {kind, base + "/" + std::to_string(ordinal), name, std::move(detail), index, skeleton});
        return rows.size() - 1;
    };
    std::vector<std::size_t> meshes, materials, motions;
    std::map<std::string, std::size_t> textures;
    std::map<std::pair<int, int>, std::size_t> bones;
    for (unsigned i = 0; i < scene.draws.size(); ++i) {
        const auto &d = scene.draws[i];
        meshes.push_back(add(StudioResourceKind::Mesh, d.mesh, int(i),
                             std::to_string(d.vertices.size()) + " vertices / " +
                                 std::to_string(d.indices.size() / 3) + " triangles"));
    }
    for (unsigned i = 0; i < scene.materials.size(); ++i)
        materials.push_back(add(StudioResourceKind::Material, scene.materials[i].name, int(i),
                                "Material settings"));
    for (auto &[name, image] : scene.textures) {
        textures[name] =
            add(StudioResourceKind::Texture, name, -1,
                std::to_string(image.width) + " x " + std::to_string(image.height), -1, name);
    }
    for (unsigned rig = 0; rig < scene.skeletons.size(); ++rig)
        for (unsigned i = 0; i < scene.skeletons[rig].joints.size(); ++i) {
            const auto &j = scene.skeletons[rig].joints[i];
            bones[{int(rig), int(i)}] =
                add(StudioResourceKind::Bone, j.name, int(i),
                    j.parent_name.empty() ? "Root bone" : "Parent: " + j.parent_name, int(rig),
                    std::to_string(rig) + "/" + j.name);
        }
    for (unsigned i = 0; i < model.motions.size(); ++i) {
        const auto &m = model.motions[i];
        std::string detail = std::to_string(unsigned(m.material.frames)) + " frames";
        if (!m.skeletal.tracks.empty())
            detail += " / bones";
        if (!m.material.tracks.empty())
            detail += " / materials";
        if (!m.visibility.tracks.empty())
            detail += " / visibility";
        if (!m.error.empty())
            detail = m.error;
        motions.push_back(
            add(StudioResourceKind::Motion, m.name, int(i), detail, -1,
                std::to_string(m.group) + "/" + std::to_string(m.slot) + "/" + m.name));
    }
    auto link = [&](std::size_t from, std::size_t to) {
        auto &a = rows[from], &b = rows[to];
        if (std::find(a.uses.begin(), a.uses.end(), b.key) == a.uses.end())
            a.uses.push_back(b.key);
        if (std::find(b.used_by.begin(), b.used_by.end(), a.key) == b.used_by.end())
            b.used_by.push_back(a.key);
    };
    for (unsigned i = 0; i < meshes.size(); ++i) {
        auto &d = scene.draws[i];
        if (d.material < materials.size())
            link(meshes[i], materials[d.material]);
        for (auto bone : d.palette)
            if (auto found = bones.find({d.skeleton, int(bone)}); found != bones.end())
                link(meshes[i], found->second);
    }
    for (unsigned i = 0; i < materials.size(); ++i)
        for (auto &name : scene.materials[i].texture_inputs)
            if (textures.contains(name))
                link(materials[i], textures.at(name));
    for (auto &[pair, row] : bones) {
        auto parent = scene.skeletons[pair.first].joints[pair.second].parent;
        if (auto found = bones.find({pair.first, parent}); found != bones.end())
            link(row, found->second);
    }
    for (unsigned i = 0; i < motions.size(); ++i) {
        auto &m = model.motions[i];
        for (auto &t : m.material.tracks) {
            for (unsigned mat = 0; mat < materials.size(); ++mat)
                if (scene.materials[mat].name == t.material)
                    link(motions[i], materials[mat]);
            for (auto &key : t.textures) {
                auto name = model.texture_prefix + key.texture;
                if (textures.contains(name))
                    link(motions[i], textures.at(name));
                else if (textures.contains(key.texture))
                    link(motions[i], textures.at(key.texture));
            }
        }
        for (auto &t : m.visibility.tracks)
            for (unsigned mesh = 0; mesh < meshes.size(); ++mesh)
                if (scene.draws[mesh].mesh == t.mesh)
                    link(motions[i], meshes[mesh]);
        for (auto &t : m.skeletal.tracks)
            for (auto &[pair, row] : bones)
                if (rows[row].name == t.name)
                    link(motions[i], row);
    }
    return rows;
}
}
