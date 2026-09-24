#pragma once
#include "authoring/resource_catalog.h"
#include "authoring/tile_grid.h"

namespace studio {
struct GroundBlend {
    std::string texture;
    std::array<float, 4> weights{};
    bool operator==(const GroundBlend &) const = default;
};
struct TerrainPoint {
    std::array<std::uint32_t, 3> parents{};
    std::array<float, 3> weights{1, 0, 0};
    SpatialPoint offset{};
    std::array<float, 2> texture_offset{};
    bool operator==(const TerrainPoint &) const = default;
};
struct TerrainTriangle {
    std::array<std::uint32_t, 3> vertices{};
    std::uint32_t cell = 0;
    bool operator==(const TerrainTriangle &) const = default;
};
struct GroundSurface {
    std::vector<TerrainPoint> points;
    std::vector<TerrainTriangle> triangles;
    std::vector<float> heights;
    std::vector<std::string> textures;
    std::vector<GroundBlend> blends;
    std::vector<std::array<float, 2>> offsets, texture_scales;
    std::array<float, 2> uv_origin{};
    bool operator==(const GroundSurface &) const = default;
    void validate(const AuthoringGrid &grid) const;
};
std::vector<TerrainTriangle> terrain_triangles(const AuthoringGrid &, const GroundSurface &);
std::vector<SpatialPoint> terrain_positions(const AuthoringGrid &, const GroundSurface &);
std::vector<std::array<float, 2>> terrain_coordinates(const AuthoringGrid &, const GroundSurface &);
enum class TerrainSubdivision { Even, LongestEdge, FaceCenter };
GroundSurface subdivide_terrain(const AuthoringGrid &, const GroundSurface &, AuthoringTile,
                                AuthoringTile,
                                TerrainSubdivision method = TerrainSubdivision::Even);
GroundSurface move_terrain_points(const AuthoringGrid &, const GroundSurface &,
                                  const std::vector<std::uint32_t> &, SpatialPoint,
                                  float radius = 0);
GroundSurface extrude_terrain_edge(const AuthoringGrid &, const GroundSurface &, std::uint32_t,
                                   std::uint32_t, SpatialPoint);
GroundSurface shape_terrain_selection(const AuthoringGrid &, const GroundSurface &, AuthoringTile,
                                      AuthoringTile, bool, float, bool);
GroundSurface simplify_terrain_selection(const AuthoringGrid &, const GroundSurface &,
                                         AuthoringTile, AuthoringTile, float);
enum class GroundBrushMode { Raise, Lower, Level, Paint, Blend };
struct GroundBrushStroke {
    GroundBrushMode mode = GroundBrushMode::Raise;
    float radius = 100, hardness = .25f, value = 25;
    std::string texture;
    std::vector<std::array<float, 2>> points;
    std::optional<std::array<AuthoringTile, 2>> selection;
};
std::pair<SpatialPoint, float> ground_selection_influence(const AuthoringGrid &grid,
                                                          const GroundSurface &ground,
                                                          AuthoringTile first, AuthoringTile last);
GroundSurface influence_ground(const AuthoringGrid &grid, const GroundSurface &ground,
                               AuthoringTile first, AuthoringTile last, float value,
                               float surrounding_cells);
GroundSurface brush_ground(const AuthoringGrid &grid, const GroundSurface &ground,
                           const GroundBrushStroke &stroke);
struct GroundTexture {
    std::string key, name, texture;
};
SpatialPoint ground_vertex(const AuthoringGrid &grid, const GroundSurface &ground, int x, int z);
std::optional<AuthoringTile> ground_tile_at(const AuthoringGrid &grid, const GroundSurface &ground,
                                            float x, float z, std::optional<float> height = {});
GroundSurface move_ground_vertex(const AuthoringGrid &grid, const GroundSurface &ground,
                                 AuthoringTile vertex, SpatialPoint delta);
GroundSurface transform_ground(const AuthoringGrid &grid, const GroundSurface &ground,
                               AuthoringTile first, AuthoringTile last, float elevation,
                               float x_degrees, float z_degrees);
std::vector<GroundTexture> load_ground_textures(const std::filesystem::path &dump,
                                                const Environment &scene,
                                                std::atomic_bool *cancel = nullptr);
Bytes ground_texture_resource(const std::filesystem::path &dump, const std::string &key);
void merge_ground_textures(Environment &destination, std::vector<GroundTexture> &palette,
                           const Environment &source, const std::vector<GroundTexture> &textures);
void restore_ground_textures(const std::filesystem::path &dump, Environment &scene,
                             const GroundSurface &ground, std::vector<GroundTexture> &palette,
                             std::atomic_bool *cancel = nullptr);
Environment ground_preview(const Environment &source, const AuthoringGrid &grid,
                           const GroundSurface &ground, const std::vector<GroundTexture> &palette);
}
