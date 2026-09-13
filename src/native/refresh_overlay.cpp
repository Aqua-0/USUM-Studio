#include "native/refresh_overlay.h"
#include "scene/environment.h"
#include <algorithm>
namespace studio {
namespace {
constexpr auto mask_flags =
    BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
constexpr auto flags = mask_flags | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
Bytes mask_pixels(const RefreshRegionMask &mask, unsigned x, unsigned y, unsigned width,
                  unsigned height, bool colored) {
    Bytes pixels(std::size_t(width) * height * 4, 255);
    std::array<std::array<std::uint8_t, 3>, 256> palette{};
    if (colored)
        for (unsigned id = 0; id < 256; ++id) {
            auto color = refresh_region_color(std::uint8_t(id));
            for (unsigned channel = 0; channel < 3; ++channel)
                palette[id][channel] = std::uint8_t(color[channel] * 255 + .5f);
        }
    for (unsigned row = 0; row < height; ++row)
        for (unsigned column = 0; column < width; ++column) {
            auto id = mask.ids[(std::size_t(y) + row) * mask.width + x + column];
            auto at = (std::size_t(row) * width + column) * 4;
            for (unsigned channel = 0; channel < 3; ++channel)
                pixels[at + channel] = colored ? palette[id][channel] : id;
        }
    return pixels;
}
void update_mask(bgfx::TextureHandle texture, const RefreshRegionMask &mask, unsigned x, unsigned y,
                 unsigned width, unsigned height, bool colored) {
    auto pixels = mask_pixels(mask, x, y, width, height, colored);
    bgfx::updateTexture2D(texture, 0, 0, std::uint16_t(x), std::uint16_t(y), std::uint16_t(width),
                          std::uint16_t(height), bgfx::copy(pixels.data(), narrow(pixels.size())));
}
}
RefreshOverlay::RefreshOverlay() {
    sampler_ = bgfx::createUniform("s_refreshMask", bgfx::UniformType::Sampler);
    palette_sampler_ = bgfx::createUniform("s_refreshPalette", bgfx::UniformType::Sampler);
    options_ = bgfx::createUniform("u_refresh", bgfx::UniformType::Vec4);
    Bytes colors(256 * 4);
    for (unsigned id = 0; id < 256; ++id) {
        auto color = refresh_region_color(std::uint8_t(id));
        for (unsigned c = 0; c < 4; ++c)
            colors[id * 4 + c] = std::uint8_t(color[c] * 255 + .5f);
    }
    palette_ = bgfx::createTexture2D(256, 1, false, 1, bgfx::TextureFormat::RGBA8, flags,
                                     bgfx::copy(colors.data(), narrow(colors.size())));
    const std::uint32_t empty = 0;
    empty_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, flags,
                                   bgfx::copy(&empty, 4));
    require(bgfx::isValid(palette_) && bgfx::isValid(empty_),
            "Refresh preview texture allocation failed");
}
RefreshOverlay::~RefreshOverlay() {
    clear();
    bgfx::destroy(palette_);
    bgfx::destroy(empty_);
    bgfx::destroy(sampler_);
    bgfx::destroy(palette_sampler_);
    bgfx::destroy(options_);
}
void RefreshOverlay::clear() {
    if (bgfx::isValid(preview_)) {
        bgfx::destroy(preview_);
        preview_ = BGFX_INVALID_HANDLE;
    }
    for (auto texture : textures_)
        bgfx::destroy(texture);
    textures_.clear();
    regions_.reset();
}
void RefreshOverlay::set_regions(std::shared_ptr<const RefreshRegionPack> regions,
                                 const std::string &prefix) {
    if (regions_ == regions && prefix_ == prefix)
        return;
    bool compatible =
        regions_ && regions && prefix_ == prefix && regions_->masks.size() == regions->masks.size();
    if (compatible)
        for (std::size_t i = 0; i < regions->masks.size(); ++i) {
            auto &a = regions_->masks[i];
            auto &b = regions->masks[i];
            if (a.width != b.width || a.height != b.height || a.texture != b.texture ||
                a.child != b.child) {
                compatible = false;
                break;
            }
        }
    if (compatible) {
        for (std::size_t i = 0; i < regions->masks.size(); ++i) {
            auto &before = regions_->masks[i];
            auto &after = regions->masks[i];
            if (before.ids == after.ids)
                continue;
            unsigned left = after.width, top = after.height, right = 0, bottom = 0;
            for (unsigned y = 0; y < after.height; ++y)
                for (unsigned x = 0; x < after.width; ++x)
                    if (before.ids[std::size_t(y) * after.width + x] !=
                        after.ids[std::size_t(y) * after.width + x]) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
            update_mask(textures_.at(i), after, left, top, right - left + 1, bottom - top + 1,
                        false);
            if (bgfx::isValid(preview_) && preview_mask_ == i)
                update_mask(preview_, after, left, top, right - left + 1, bottom - top + 1, true);
        }
        regions_ = std::move(regions);
        return;
    }
    clear();
    prefix_ = prefix;
    selected = -1;
    if (!regions)
        return;
    try {
        for (auto &mask : regions->masks) {
            auto texture = bgfx::createTexture2D(mask.width, mask.height, false, 1,
                                                 bgfx::TextureFormat::RGBA8, mask_flags);
            require(bgfx::isValid(texture), "Refresh mask allocation failed for " + mask.texture);
            textures_.push_back(texture);
            update_mask(texture, mask, 0, 0, mask.width, mask.height, false);
        }
    } catch (...) {
        clear();
        throw;
    }
    regions_ = std::move(regions);
}
bgfx::TextureHandle RefreshOverlay::preview_texture(std::size_t mask) {
    require(regions_ && mask < regions_->masks.size(), "Refresh preview mask is unavailable");
    if (bgfx::isValid(preview_) && preview_mask_ == mask)
        return preview_;
    if (bgfx::isValid(preview_))
        bgfx::destroy(preview_);
    preview_ = BGFX_INVALID_HANDLE;
    auto &source = regions_->masks[mask];
    preview_ = bgfx::createTexture2D(source.width, source.height, false, 1,
                                     bgfx::TextureFormat::RGBA8, flags);
    require(bgfx::isValid(preview_), "Refresh texture preview allocation failed");
    update_mask(preview_, source, 0, 0, source.width, source.height, true);
    preview_mask_ = mask;
    return preview_;
}
bool RefreshOverlay::bind(const SceneMaterial &material) {
    RefreshMaterialBinding binding;
    if (active())
        binding = bind_refresh_material(*regions_, material, prefix_);
    float options[4] = {active() ? (binding.mask >= 0 ? 1.f : 2.f) : 0.f, float(selected), 0, 0};
    bgfx::setUniform(options_, options);
    bgfx::setTexture(4, sampler_,
                     binding.mask >= 0 ? textures_.at(std::size_t(binding.mask)) : empty_,
                     mask_flags);
    bgfx::setTexture(5, palette_sampler_, palette_, flags);
    return !binding.excluded;
}
}
