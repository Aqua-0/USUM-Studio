#pragma once
#include "formats/model.h"
#include <optional>
namespace studio {
enum class TextureFormat : unsigned {
    RGBA8 = 4,
    RGB8 = 3,
    RGB565 = 2,
    RGB5A1 = 0x17,
    RGBA4 = 0x16,
    LA8 = 0x23,
    L8 = 0x25,
    A8 = 0x26,
    LA4 = 0x27,
    L4 = 0x28,
    A4 = 0x29,
    ETC1 = 0x2a,
    ETC1A4 = 0x2b
};
struct TextureImage {
    std::uint16_t width = 0, height = 0;
    Bytes rgba;
    std::vector<Bytes> mipmaps;
    unsigned authored_levels = 0;
};
TextureImage read_tga(View bytes);
Bytes write_tga(const TextureImage &image);
Bytes encode_texture(View donor, const TextureImage &image, const std::string &name,
                     std::optional<TextureFormat> format = std::nullopt);
Bytes bind_texture(View pack, const TextureImage &image);
}
