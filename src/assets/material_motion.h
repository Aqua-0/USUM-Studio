#pragma once
#include "scene/animation.h"
namespace studio {
Bytes encode_material_tracks(const MaterialMotion &motion, MaterialTrack::Kind kind);
Bytes replace_material_motion(View original, const MaterialMotion &motion);
}
