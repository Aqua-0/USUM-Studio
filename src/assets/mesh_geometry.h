#pragma once
#include "formats/skinning.h"
#include <set>
#include <optional>
namespace studio {
Bytes replace_mesh_geometry(View original, const SkinnedModel &replacement);
void set_vertex_weight(SkinnedModel &model, std::size_t mesh, const std::set<std::size_t> &vertices,
                       unsigned bone, float weight);
void transform_vertices(SkinMesh &mesh, const std::set<std::size_t> &vertices,
                        std::array<float, 3> translation, std::array<float, 3> rotation,
                        std::array<float, 3> scale, std::optional<std::array<float, 3>> pivot = {});
void rebuild_mesh_normals(SkinMesh &mesh);
}
