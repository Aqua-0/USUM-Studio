#pragma once
#include "formats/texture.h"
namespace studio {
TextureImage decode_texture_pixels(View bytes, unsigned width, unsigned height, unsigned format);
TextureImage decode_field_texture(View bytes);
Bytes texture_mip_chain(const TextureImage &image);
Bytes encode_texture_pixels(View original, const TextureImage &image, TextureFormat format);
std::size_t texture_level_size(unsigned width, unsigned height, TextureFormat format);
const char *texture_format_name(TextureFormat format);
}
