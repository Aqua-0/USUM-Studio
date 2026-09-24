#pragma once
#include "scene/environment.h"
#include "scene/player.h"
#include "native/post_process.h"
#include "native/particle_renderer.h"
#include "native/spatial_overlay.h"
#include "native/refresh_overlay.h"
#include <bgfx/bgfx.h>
#include <memory>
#include <optional>
#include <set>
#include <algorithm>
namespace studio {
class EnvironmentRenderer {
  public:
    SpatialOverlay spatial;
    RefreshOverlay refresh;
    PlayerController player;
    EnvironmentRenderer(const std::filesystem::path &shaders);
    ~EnvironmentRenderer();
    void set_scene(std::shared_ptr<const Environment> scene);
    void refresh_materials() {
        if (scene_)
            materials_ = scene_->materials;
    }
    void refresh_textures();
    void reload_scene_buffers();
    std::map<std::size_t, Matrix> preview_transforms;
    void preview_vertices(std::size_t draw, const std::vector<SceneVertex> &vertices);
    void select_draw(int draw);
    void select_draws(const std::set<int> &draws);
    bool draw_visible(std::size_t draw) const {
        return draw >= hidden_draws_.size() || !hidden_draws_[draw];
    }
    void set_draw_visible(std::size_t draw, bool visible);
    void show_all_draws();
    std::uint32_t background_color = 0x17232bffu;
    bool highlight_object = true, wireframe = false, outlines = false;
    bool retain_frame_during_upload = false;
    bool pica_texture_precision = false;
    float outline_width = 1.f;
    void invalidate_selection_readback() {
        ++scene_generation_;
    }
    void upload_step(std::size_t budget = 4 * 1024 * 1024);
    bgfx::TextureHandle render(unsigned width, unsigned height, const float *view,
                               const float *projection, bool vertex_colors, bool cutaway,
                               float cut_height, bool raw_materials, bool picking = false);
    bool request_pick(unsigned x, unsigned y, unsigned width, unsigned height, const float *view,
                      const float *projection, bool vertex_colors, bool cutaway, float cut_height,
                      bool raw_materials);
    std::optional<int> poll_pick(std::uint32_t frame);
    int picked_refresh_region() const {
        return picked_refresh_region_;
    }
    std::size_t uploaded_draws() const {
        return std::count_if(draws_.begin(), draws_.end(), [](const auto &draw) {return bgfx::isValid(draw.vertices) && bgfx::isValid(draw.indices);});
    }
    bool ready() const;
    struct Lighting {
        bool enabled = false, normal_maps = true, game = true, soft = false,
             camera_relative = false;
        unsigned context = 0, motion = 0;
        float hour = 12;
        float ambient = .35f, strength = .8f, highlights = .35f;
        std::array<float, 3> direction{.35f, .9f, .2f};
    } lighting;
    struct Playback {
        bool enabled = true, skeletal = true, materials = true;
        double seconds = 0;
    } playback;
    bgfx::TextureHandle texture(const std::string &name) const {
        auto it = textures_.find(name);
        return it == textures_.end() ? bgfx::TextureHandle{bgfx::kInvalidHandle} : it->second;
    }
    const std::vector<SceneMaterial> &materials() const {
        return materials_;
    }
    std::size_t texture_bytes = 0, geometry_bytes = 0;

  private:
    void clear();
    std::vector<bool> hidden_draws_;
    int selected_draw_ = -1;
    std::vector<bool> highlighted_;
    bgfx::UniformHandle selection_color_, edge_options_, texture_precision_;
    bgfx::FrameBufferHandle edge_target_ = BGFX_INVALID_HANDLE;
    PostProcess post_;
    ParticleRenderer particles_;
    struct Draw {
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle edges = BGFX_INVALID_HANDLE;
    };
    std::shared_ptr<const Environment> scene_;
    std::vector<Draw> draws_;
    std::set<std::size_t> previewed_draws_;
    std::vector<SceneMaterial> materials_;
    std::map<std::string, bgfx::TextureHandle> textures_;
    std::vector<bgfx::TextureHandle> bloom_masks_;
    bgfx::ProgramHandle program_;
    std::array<bgfx::UniformHandle, 3> samplers_;
    std::array<bgfx::UniformHandle, 7> combiners_;
    bgfx::UniformHandle pick_color_, skin_options_, bone_rows_, normal_rows_, fog_params_,
        fog_color_, fog_depth_;

  public:
    unsigned weather_effect = 0, sky_type = 0;
    bool sky_enabled = true;
    bool geometry_enabled = true;
    bool particles_enabled = true;
    std::array<float, 3> particle_origin{};
    std::size_t particle_count() const {
        return particles_.particle_count;
    }
    bool fog_enabled = true, bloom_enabled = false, characters_enabled = true,
         conditional_characters = true, visibility_enabled = true;

  private:
    bgfx::UniformHandle object_basis_, projection_rows_, projection_options_, projection_offsets_,
        vertex_effect_, vertex_lighting_, rim_phong_, vertex_light_direction_, uv_u_, uv_v_,
        buffer_, preview_, cutaway_, light_, light_direction_, surface_, texture_options_, bump_,
        game_light_, game_ambient_, game_directions_, game_colors_, lookup_sampler_, lookup_inputs_;
    bgfx::VertexLayout layout_;
    bgfx::TextureHandle scene_copy_ = BGFX_INVALID_HANDLE, white_,
                        lookup_texture_ = BGFX_INVALID_HANDLE;
    unsigned lookup_height_ = 1;
    bgfx::TextureHandle complete_frame_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
    unsigned width_ = 0, height_ = 0, pick_width_ = 0, pick_height_ = 0;
    bgfx::FrameBufferHandle pick_target_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle pick_readback_ = BGFX_INVALID_HANDLE;
    std::array<std::uint8_t, 4> pick_pixel_{};
    std::uint32_t pick_ready_ = 0, scene_generation_ = 0, pick_generation_ = 0;
    bool pick_pending_ = false, pick_refresh_ = false;
    int picked_refresh_region_ = -1;
};
}
