#pragma once
#include <bgfx/bgfx.h>
namespace studio::RenderViews {
inline constexpr bgfx::ViewId edge_prepass = 0, scene = 1, scene_copy = 2, refraction = 3,
                              edge_map = 4, outlines = 5, picking = 6, pick_readback = 7,
                              selection_wire = 8, bloom = 9;
}
