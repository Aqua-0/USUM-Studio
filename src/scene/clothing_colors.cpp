#include "scene/clothing_colors.h"
#include "scene/environment.h"
#include <algorithm>
namespace studio {
void apply_clothing_colors(std::map<std::string, TextureImage> &textures, const Container &resource,
                           const std::array<std::array<float, 3>, 6> &colors,
                           const std::string &prefix) {
    std::map<std::string, std::vector<std::pair<TextureImage, std::array<float, 3>>>> masks;
    for (unsigned i = 3; i < resource.files.size(); ++i) {
        auto &b = resource.files[i];
        if (b.empty())
            continue;
        require(b.size() >= 2, "Truncated outfit mask");
        auto n = b[b.size() - 2], type = b.back();
        require(type < 6 && n <= b.size() - 2, "Invalid outfit mask footer");
        auto name = prefix + text(slice(b, b.size() - 2 - n, n));
        auto found = textures.find(name);
        if (found == textures.end())
            found = std::find_if(textures.begin(), textures.end(), [&](const auto &t) {
                return t.first.starts_with(name);
            });
        require(found != textures.end(), "Outfit mask references missing texture");
        auto mask = decode_field_texture(b);
        require(mask.width == found->second.width && mask.height == found->second.height,
                "Outfit mask dimensions differ");
        masks[found->first].push_back({std::move(mask), colors[type]});
    }
    for (auto &[name, layers] : masks) {
        auto &image = textures.at(name);
        for (std::size_t p = 0; p < image.width * image.height; ++p)
            for (unsigned k = 0; k < 3; ++k) {
                float factor = 1;
                for (auto &[mask, tint] : layers)
                    factor += (tint[k] - factor) * float(mask.rgba[p * 4 + 3]) / 255;
                image.rgba[p * 4 + k] =
                    std::uint8_t(std::clamp(float(image.rgba[p * 4 + k]) * factor, 0.f, 255.f));
            }
        image.rgba.resize(image.width * image.height * 4);
        image.mipmaps.clear();
        image.authored_levels = 1;
    }
}
}
