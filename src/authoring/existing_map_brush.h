#pragma once
#include "assets/material_document.h"
#include "assets/mesh_geometry.h"
namespace studio {
std::optional<std::array<float, 3>> pick_map_surface(const Environment &scene,
                                                     std::array<float, 3> eye,
                                                     std::array<float, 3> direction, float ceiling);
enum class ExistingBrush { Raise, Lower, Smooth, Flatten };
struct ExistingBrushHit {
    std::array<float, 3> point;
    std::size_t mesh, face;
};
std::optional<ExistingBrushHit> pick_existing_mesh(const SkinnedModel &model,
                                                   const std::set<std::size_t> &meshes,
                                                   std::array<float, 3> eye,
                                                   std::array<float, 3> direction);
bool sculpt_existing_mesh(SkinnedModel &model, const std::set<std::size_t> &meshes,
                          std::array<float, 3> center, float radius, float amount,
                          ExistingBrush tool, float level);
MaterialFaces existing_brush_faces(const SkinnedModel &model, const std::set<std::size_t> &meshes,
                                   const ExistingBrushHit &hit, float radius);
FaceMaterialEdit paint_existing_faces(MaterialDocument &document, const MaterialFaces &faces,
                                      std::size_t material, float tile_size = 0,
                                      float rotation = 0);
}
