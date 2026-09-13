#pragma once
#include <bgfx/bgfx.h>
namespace studio::RenderViews {
inline constexpr bgfx::ViewId scene = 0, scene_copy = 1, refraction = 2, edge_map = 3, outlines = 4,
                              picking = 5, pick_readback = 6, selection_wire = 7, bloom = 8;
}
