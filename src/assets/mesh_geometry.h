#pragma once
#include "formats/skinning.h"
#include <set>
#include <map>
#include <optional>
namespace studio {
Bytes replace_mesh_geometry(View original, const SkinnedModel &replacement);
void set_vertex_weight(SkinnedModel &model, std::size_t mesh, const std::set<std::size_t> &vertices,
                       unsigned bone, float weight);
void transform_vertices(SkinMesh &mesh, const std::set<std::size_t> &vertices,
                        std::array<float, 3> translation, std::array<float, 3> rotation,
                        std::array<float, 3> scale, std::optional<std::array<float, 3>> pivot = {});
struct WeightTransferResult {
    std::size_t transferred = 0, outside_distance = 0;
};
WeightTransferResult
transfer_surface_weights(SkinnedModel &model, const std::set<std::size_t> &sources,
                         const std::map<std::size_t, std::set<std::size_t>> &targets,
                         float max_distance);
void rebuild_mesh_normals(SkinMesh &mesh);
}
