#pragma once
#include "scene/lighting.h"
#include <bgfx/bgfx.h>
namespace studio {
class PostProcess {
  public:
    explicit PostProcess(const std::filesystem::path &shaders);
    ~PostProcess();
    void outlines(bgfx::FrameBufferHandle target, bgfx::TextureHandle edges, unsigned width,
                  unsigned height, float thickness);
    bgfx::TextureHandle render(bgfx::TextureHandle color, unsigned width, unsigned height,
                               const BloomSettings &bloom, bgfx::TextureHandle mask,
                               std::array<float, 2> mask_scale);

  private:
    bgfx::ProgramHandle program_;
    bgfx::UniformHandle source_, blur_, options_, weights_, mask_, mask_mapping_;
    bgfx::VertexBufferHandle triangle_;
    std::array<bgfx::FrameBufferHandle, 3> targets_{
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    unsigned width_ = 0, height_ = 0, reduce_ = 0;
};
}
