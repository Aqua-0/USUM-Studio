#pragma once
#include "formats/model.h"
namespace studio {
struct TerrainExportVertex {
    std::array<float, 3> position{}, normal{};
    std::array<float, 2> uv{};
    std::uint32_t color = 0xffffffff;
    std::array<float, 2> uv1{}, uv2{};
    std::array<float, 3> tangent{};
};
struct TerrainExportMesh {
    std::string name, base_texture, overlay_texture;
    std::vector<TerrainExportVertex> vertices;
    std::vector<std::uint16_t> indices;
};
// The preset blends texture 0/1 with vertex alpha; opaque ground disables cutout.
Bytes append_terrain_meshes(const Model &destination, const std::string &material_preset,
                            const Model &geometry_preset, const std::string &geometry_mesh,
                            const std::vector<TerrainExportMesh> &meshes,
                            bool replace_existing = false);
Bytes append_rigid_meshes(const Model &destination, const std::string &material,
                          const Model &geometry, const std::string &source_mesh,
                          const std::vector<TerrainExportMesh> &meshes, bool replace_geometry,
                          unsigned source_submesh = 0);
}
