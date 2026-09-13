#pragma once
#include <array>
#include <vector>
namespace studio {
using TextureUvSegment = std::array<std::array<float, 2>, 2>;
std::vector<TextureUvSegment> texture_uv_segments(std::array<float, 2> start,
                                                  std::array<float, 2> end, unsigned wrap_u,
                                                  unsigned wrap_v);
}
