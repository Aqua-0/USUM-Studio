#pragma once
#include "formats/skinning.h"
namespace studio {
Bytes replace_skeleton(View original, const std::vector<Joint> &joints);
}
