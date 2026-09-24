#pragma once
#include <bgfx/bgfx.h>
#include "native/viewport_resolution.h"
#include <cstdint>
#include <vector>
struct ImDrawData;
namespace studio {
class ImGuiRenderer {
  public:
    ImGuiRenderer();
    ~ImGuiRenderer();
    static bool preview_3ds, preview_native_size;
    static ViewportResolution viewport_resolution(float width, float height);
    static std::uint64_t image_id(bgfx::TextureHandle texture, bool alpha_blend = true,
                                  bool nearest = false);
    void upload_font();
    void render(ImDrawData *data);

  private:
    struct OverflowBuffers {
        bgfx::DynamicVertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::DynamicIndexBufferHandle indices = BGFX_INVALID_HANDLE;
    };
    std::vector<OverflowBuffers> overflow_;
    bgfx::ProgramHandle program_;
    bgfx::TextureHandle font_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_;
    bgfx::VertexLayout layout_;
};
}
