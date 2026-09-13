#pragma once
#include <algorithm>
#include <cmath>
namespace studio {
struct ViewportResolution {
    unsigned width, height;
    float display_width, display_height, offset_x, offset_y;
    static ViewportResolution fit(float available_width, float available_height, float scale_x,
                                  float scale_y, bool preview_3ds, bool native_size = false) {
        available_width = std::max(available_width, 1.f);
        available_height = std::max(available_height, 1.f);
        if (!preview_3ds)
            return {unsigned(std::clamp(available_width * scale_x, 1.f, 4096.f)),
                    unsigned(std::clamp(available_height * scale_y, 1.f, 4096.f)),
                    available_width,
                    available_height,
                    0,
                    0};
        float scale =
            std::min(available_width * scale_x / 400.f, available_height * scale_y / 240.f);
        if (native_size)
            scale = 1;
        else if (scale >= 1)
            scale = std::floor(scale);
        float display_width = 400 * scale / scale_x;
        float display_height = 240 * scale / scale_y;
        return {400,
                240,
                display_width,
                display_height,
                std::max(0.f, (available_width - display_width) * .5f),
                std::max(0.f, (available_height - display_height) * .5f)};
    }
    unsigned pixel_x(float local_x) const {
        return unsigned(std::clamp(local_x / display_width * width, 0.f, float(width - 1)));
    }
    unsigned pixel_y(float local_y) const {
        return unsigned(std::clamp(local_y / display_height * height, 0.f, float(height - 1)));
    }
};
}
