#pragma once
#include "authoring/composition_document.h"
#include "field/collision_document.h"
namespace studio {
struct GroundExportResult {
    std::size_t member = 0, triangles = 0;
    Bytes original, compiled;
};
std::vector<CollisionFace> ground_collision_faces(const AuthoringGrid &, const GroundSurface &);
std::vector<CollisionState> composition_collision(const CompositionDocument &);
std::shared_ptr<Environment> authored_collision_scene(const Environment &context,
                                                      const std::vector<CollisionState> &);
GroundExportResult compile_ground_patch(const std::filesystem::path &dump,
                                        const CompositionDocument &, std::uint32_t attribute);
void export_ground_patch(const std::filesystem::path &dump, const CompositionDocument &,
                         std::uint32_t attribute, const std::filesystem::path &output);
}
