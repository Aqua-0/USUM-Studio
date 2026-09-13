#pragma once
#include "assets/refresh_regions.h"
#include <bgfx/bgfx.h>
#include <memory>
namespace studio {
class RefreshOverlay {
  public:
    RefreshOverlay();
    ~RefreshOverlay();
    RefreshOverlay(const RefreshOverlay &) = delete;
    RefreshOverlay &operator=(const RefreshOverlay &) = delete;
    void set_regions(std::shared_ptr<const RefreshRegionPack> regions, const std::string &prefix);
    bgfx::TextureHandle preview_texture(std::size_t mask);
    bool active() const {
        return enabled && regions_ && !textures_.empty();
    }
    bool bind(const SceneMaterial &material);
    bool enabled = false;
    int selected = -1;

  private:
    void clear();
    std::shared_ptr<const RefreshRegionPack> regions_;
    std::string prefix_;
    std::vector<bgfx::TextureHandle> textures_;
    bgfx::UniformHandle sampler_, palette_sampler_, options_;
    bgfx::TextureHandle palette_, empty_, preview_ = BGFX_INVALID_HANDLE;
    std::size_t preview_mask_ = 0;
};
}
