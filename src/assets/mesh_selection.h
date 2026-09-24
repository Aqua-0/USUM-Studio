#pragma once
#include "formats/skinning.h"
#include "scene/skeleton.h"
#include <set>
#include <map>
#include <cmath>
#include <optional>
namespace studio {
using MeshVertexSelection = std::map<std::size_t, std::set<std::size_t>>;
using MeshInfluences = std::map<std::size_t, std::map<std::size_t, float>>;
MeshInfluences mesh_selection_influences(const SkinnedModel &model,
                                         const MeshVertexSelection &selected,
                                         const std::set<std::size_t> &enabled, float radius);
void transform_mesh_influences(SkinnedModel &model, const MeshVertexSelection &selected,
                               const MeshInfluences &influences, std::array<float, 3> translation,
                               std::array<float, 3> rotation, std::array<float, 3> scale);
void flatten_mesh_influences(SkinnedModel &model, const MeshVertexSelection &selected,
                             const MeshInfluences &influences, unsigned axis);
SceneSkeleton bone_test_skeleton(const SceneSkeleton &source, unsigned bone, unsigned axis,
                                 float degrees);
void transform_mesh_selection(SkinnedModel &model, const MeshVertexSelection &selected,
                              std::array<float, 3> translation, std::array<float, 3> rotation,
                              std::array<float, 3> scale);
void flatten_mesh_selection(SkinnedModel &model, const MeshVertexSelection &selected,
                            unsigned axis);
using MeshEdge = std::array<std::size_t, 2>;
std::vector<MeshEdge> mesh_edges(const SkinMesh &mesh);
std::set<std::size_t> mesh_selection_vertices(const SkinMesh &mesh, unsigned mode,
                                              const std::set<std::size_t> &elements, bool seams);
void flatten_mesh_selection(SkinMesh &mesh, const std::set<std::size_t> &vertices, unsigned axis);
void paint_vertex_weight(SkinVertex &vertex, unsigned influences, unsigned bone, float target,
                         float amount, int fallback = -1);
struct MeshProjection {
    float x = 0, y = 0, z = 0, inverse_w = 0;
};
struct MeshSurfacePixel {
    float depth = INFINITY;
    int mesh = -1, face = -1;
    std::array<float, 3> weights{};
    float depth_dx = 0, depth_dy = 0;
};
class MeshSurface {
  public:
    unsigned width = 0, height = 0;
    std::vector<std::vector<MeshProjection>> points;
    std::vector<MeshSurfacePixel> pixels;
    void build(const SkinnedModel &model, const std::vector<bool> &visible, const float *view,
               const float *projection, unsigned w, unsigned h,
               const std::vector<Matrix> &poses = {}, const std::vector<unsigned> &culls = {});
    std::optional<MeshSurfacePixel> pick_face(const SkinnedModel &model, float x, float y,
        const std::set<std::size_t> &editable, const std::vector<unsigned> &culls, bool through) const;
    const MeshSurfacePixel *at(float x, float y) const;
    bool visible(const MeshProjection &point) const;
};
}
