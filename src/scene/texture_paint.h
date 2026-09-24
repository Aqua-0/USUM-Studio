#pragma once
#include "formats/texture.h"
#include <algorithm>
#include <cmath>
namespace studio {
inline bool paint_texture_segment(TextureImage &image, float ax, float ay, float bx, float by,
                                  float radius, const std::array<float, 4> &color,
                                  unsigned channels, float opacity, bool erase) {
    require(image.rgba.size() == std::size_t(image.width) * image.height * 4,
            "Invalid painting texture");
    require(std::isfinite(ax) && std::isfinite(ay) && std::isfinite(bx) && std::isfinite(by) &&
                std::isfinite(radius) && radius > 0 && channels <= 15 && std::isfinite(opacity) &&
                opacity >= 0 && opacity <= 1,
            "Invalid brush settings");
    for (auto value : color)
        require(std::isfinite(value), "Invalid brush color");
    unsigned affected = erase && channels == 15 ? 8 : channels;
    int left = int(std::clamp(std::floor(std::min(ax, bx) - radius), 0.f, float(image.width))),
        right = int(std::clamp(std::ceil(std::max(ax, bx) + radius), 0.f, float(image.width))),
        top = int(std::clamp(std::floor(std::min(ay, by) - radius), 0.f, float(image.height))),
        bottom = int(std::clamp(std::ceil(std::max(ay, by) + radius), 0.f, float(image.height)));
    float dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
    bool changed = false;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) {
            float px = x + .5f - ax, py = y + .5f - ay;
            float t = length > 0 ? std::clamp((px * dx + py * dy) / length, 0.f, 1.f) : 0;
            px -= dx * t;
            py -= dy * t;
            if (px * px + py * py > radius * radius)
                continue;
            auto offset = (std::size_t(y) * image.width + x) * 4;
            for (unsigned channel = 0; channel < 4; ++channel) {
                if (!(affected & (1u << channel)))
                    continue;
                float target = erase ? 0.f : std::clamp(color[channel], 0.f, 1.f) * 255.f;
                auto value = std::uint8_t(
                    std::lround(image.rgba[offset + channel] * (1 - opacity) + target * opacity));
                changed |= image.rgba[offset + channel] != value;
                image.rgba[offset + channel] = value;
            }
        }
    if (changed)
        image.mipmaps.clear();
    return changed;
}
}
