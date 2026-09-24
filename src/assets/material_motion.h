#pragma once
#include "scene/animation.h"
namespace studio {
struct AtlasFlipbook {
    unsigned columns = 4, rows = 4, first_tile = 0, tile_count = 8, hold_frames = 1;
    bool repeat = true, tile_uvs = false;
};
MaterialMotion make_atlas_flipbook(const MaterialMotion &motion, const std::string &material,
                                   unsigned slot, const AtlasFlipbook &atlas);
MaterialMotion key_material_uv_transform(const MaterialMotion &motion, const std::string &material,
                                         unsigned unit, unsigned frame,
                                         const std::array<float, 5> &values,
                                         const std::array<float, 5> &base);
Bytes encode_material_tracks(const MaterialMotion &motion, MaterialTrack::Kind kind);
Bytes replace_material_motion(View original, const MaterialMotion &motion);
}
