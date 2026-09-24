#pragma once
#include "formats/texture.h"
namespace studio {
inline TextureImage texture_channel_preview(const TextureImage &image, unsigned channel) {
    require(channel < 4, "Invalid texture channel");
    TextureImage result = image;
    result.mipmaps.clear();
    result.authored_levels = 1;
    for (std::size_t i = 0; i < result.rgba.size(); i += 4) {
        auto value = image.rgba[i + channel];
        result.rgba[i] = result.rgba[i + 1] = result.rgba[i + 2] = value;
        result.rgba[i + 3] = 255;
    }
    return result;
}
inline TextureImage replace_texture_channels(const TextureImage &current,
                                             const TextureImage &imported, unsigned mask,
                                             int source_channel = -1) {
    require(mask && mask <= 15 && source_channel >= -1 && source_channel < 4,
            "Choose valid replacement channels");
    require(current.width == imported.width && current.height == imported.height,
            "Channel replacement requires a PNG with the current texture dimensions");
    require(current.rgba.size() == imported.rgba.size() &&
                current.rgba.size() == std::size_t(current.width) * current.height * 4,
            "Invalid texture pixel data");
    TextureImage result = current;
    result.mipmaps.clear();
    for (std::size_t i = 0; i < result.rgba.size(); i += 4)
        for (unsigned channel = 0; channel < 4; ++channel)
            if (mask & (1u << channel))
                result.rgba[i + channel] =
                    imported.rgba[i + (source_channel < 0 ? channel : unsigned(source_channel))];
    return result;
}
}
