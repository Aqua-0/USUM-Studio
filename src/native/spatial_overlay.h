#pragma once
#include "scene/environment.h"
#include "native/camera.h"
#include <bgfx/bgfx.h>
#include <memory>
namespace studio {
class SpatialOverlay {
  public:
    explicit SpatialOverlay(const std::filesystem::path &shaders);
    ~SpatialOverlay();
    void invalidate_geometry() {
        clear();
    }
    void set_scene(std::shared_ptr<const Environment> scene);
    void render(bgfx::ViewId view, bool picking, unsigned first_id);
    bool controls(ViewportCamera &camera, int zone, bool details = false);
    std::array<bool, unsigned(SpatialKind::Count)> enabled{}, locked{};
    int destination_zone = -1;
    unsigned destination_event = 0;
    bool xray = false, filled = true, pick_overlays = false, focus = false, edges = true,
         selected_only = false, attribute_colors = true;
    float opacity = .18f;
    int selected = -1;

  private:
    struct Buffers {
        bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle triangles = BGFX_INVALID_HANDLE, lines = BGFX_INVALID_HANDLE;
    };
    void clear();
    void upload();
    void camera_guides(const CameraSetting &setting, const CameraSetting *defaults);
    std::shared_ptr<const Environment> scene_;
    std::vector<Buffers> buffers_;
    bgfx::ProgramHandle program_;
    bgfx::UniformHandle color_, params_;
    bgfx::VertexLayout layout_;
    char search_[128]{};
    bool guides_ = true;
    int kind_filter_ = 0;
    int interaction_guide_selection_ = -1;
    SpatialPoint anchor_{};
    int anchor_selection_ = -2;
    int zone_ = -1;
    float guide_length_ = 300;
    std::vector<SpatialVertex> guide_vertices_;
    std::vector<SpatialVertex> interaction_guides_;
};
}
