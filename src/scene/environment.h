#pragma once
#include "field/archive_sources.h"
#include "formats/texture.h"
#include "scene/lighting.h"
#include "scene/skeleton.h"
#include "scene/particles.h"
#include "scene/spatial.h"
#include "field/map_catalog.h"
#include <atomic>
#include <map>
namespace studio {
struct SceneVertex {
    float x, y, z, u, v, u1 = 0, v1 = 0, u2 = 0, v2 = 0;
    std::uint32_t color = 0xffffffff;
    float nx = 0, ny = 0, nz = 0, tx = 0, ty = 0, tz = 0;
    std::array<float, 4> joints{}, weights{};
};
struct SceneTexture {
    std::array<float, 5> transform{1, 1, 0, 0, 0};
    std::array<float, 4> row_u{1, 0, 0, 0}, row_v{0, 1, 0, 0};
    std::array<MaterialColor, 3> projection{};
    std::array<float, 2> projection_offset{};
    unsigned source = 0, wrap_u = 0, wrap_v = 0, mag_filter = 1, min_filter = 3;
};
struct SceneMaterial {
    std::string name, texture, vertex_shader, fragment_shader, resource_scope, shader_origin;
    std::array<unsigned, 6> constant_assignments{};
    std::array<std::string, 3> texture_inputs{};
    bool unsupported_mapping = false;
    std::array<SceneTexture, 3> inputs{};
    MaterialCombiner combiner, authored_combiner;
    CombinerSettings authored_combiners;
    std::optional<CombinerSettings> combiner_edit;
    MaterialBump bump;
    bool fog_enabled = false;
    int fog_slot = 0;
    int light_set = 0;
    bool fragment_lighting = true;
    bool generated_lighting_color = false, object_space_normals = false;
    bool height_tint = false, screen_refraction = false;
    MaterialColor vertex_parameters{};
    std::array<bool, 4> lighting_channels{};
    MaterialColor rim_phong{};
    unsigned edge_type = 2, edge_id = 0;
    bool id_edge_enabled = false;
    int edge_alpha_mask = -1;
    std::array<int, 3> reflection_tables{-1, -1, -1};
    std::array<MaterialColor, 3> reflection_inputs{};
    MaterialColor emission{}, ambient{1, 1, 1, 1}, diffuse{1, 1, 1, 1}, specular0{}, specular1{};
    std::uint32_t depth_state = 0x1f51, blend_state = 0x01010000, blend_color = 0;
    std::uint32_t stencil_test = 0, stencil_operations = 0;
    bool stencil_write = true;
    std::int32_t layer = 0, priority = 0;
    unsigned texture_count = 0, cull = 0, alpha_function = 1, alpha_reference = 0;
};
struct SceneModelSource {
    std::filesystem::path archive;
    std::size_t member = 0;
    std::vector<std::size_t> path;
};
struct SceneDraw {
    std::shared_ptr<const SceneModelSource> source;
    int placement = -1;
    bool projected_shadow = false;
    std::array<float, 3> shadow_projection{0, 0, .5f};
    int player = -1;
    int sky_part = -1;
    unsigned weather_mask = 0;
    std::array<float, 2> weather_scale{1, 1};
    std::array<MaterialColor, 2> object_basis{MaterialColor{1, 0, 0, 0}, MaterialColor{0, 0, 1, 0}};
    std::string mesh, scope;
    bool character = false, conditional = false, preview_only = false;
    std::string name;
    std::vector<SceneVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::size_t material = 0;
    int skeleton = -1;
    std::vector<std::uint8_t> palette;
};
struct Environment {
    ArchiveSources archive_sources;
    SpatialScene spatial;
    Bytes placement_source;
    std::vector<Matrix> placement_transforms;
    std::array<bool, 2> player_available{};
    std::vector<WeatherParticles> weather_particles;
    std::vector<MapLocation> locations;
    std::optional<std::array<float, 3>> start_position(int zone = -1) const {
        for (auto &location : locations)
            if ((zone < 0 || location.zone == zone) && location.start)
                return location.start;
        return {};
    }
    std::vector<VisibilityAnimation> visibility_animations;
    std::size_t character_placements = 0;
    std::vector<SceneSkeleton> skeletons;
    std::vector<MaterialAnimation> material_animations;
    std::vector<LightingContext> lighting_contexts;
    std::vector<LightingTable> lighting_tables;
    std::vector<SceneDraw> draws;
    std::vector<SceneMaterial> materials;
    std::map<std::string, TextureImage> textures;
    std::map<std::string, SceneModelSource> texture_sources, shader_sources;
    std::map<std::string, std::shared_ptr<const Bytes>> texture_overrides;
    std::vector<std::string> diagnostics;
    std::array<float, 3> low{}, high{};
    std::size_t terrain_blocks = 0, static_placements = 0, shader_resources = 0, source_bytes = 0;
    double load_ms = 0;
    std::string report() const;
};
std::vector<bool> evaluate_visibility(const Environment &scene, double seconds, float hour,
                                      bool enabled = true);
void update_texture_transform(SceneTexture &input);
void evaluate_material_animations(const Environment &scene, std::vector<SceneMaterial> &materials,
                                  double seconds, float hour, bool enabled = true);
TextureImage decode_field_texture(View bytes);
Bytes texture_mip_chain(const TextureImage &image);
void complete_normals(SceneDraw &draw);
Environment load_environment(const std::filesystem::path &dump, std::size_t area,
                             std::atomic_bool *cancel = nullptr,
                             const ArchiveSources &archives = {},
                             const std::map<std::size_t, Bytes> &working_members = {});
}
