#pragma once
#include "formats/container.h"
#include "formats/texture.h"
#include <map>
namespace studio {
void apply_clothing_colors(std::map<std::string, TextureImage> &textures, const Container &resource,
                           const std::array<std::array<float, 3>, 6> &colors,
                           const std::string &prefix = "");
}
