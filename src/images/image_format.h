#pragma once
#include "formats/texture.h"
namespace studio {
struct NativeImageInfo {
    unsigned width = 0, height = 0, format = 0, flags = 0, storage_width = 0, storage_height = 0;
    std::size_t payload = 0, data_offset = 0;
    bool texture_resource = false;
};
bool is_native_image(View bytes);
NativeImageInfo native_image_info(View bytes);
const char *native_image_format_name(unsigned format);
TextureImage decode_native_image(View bytes);
void validate_image_replacement(View original, View replacement);
Bytes replace_native_image(View original, const TextureImage &replacement);
struct EmbeddedImage {
    std::string name;
    std::size_t offset = 0, size = 0;
};
std::vector<EmbeddedImage> embedded_images(View package);
}
