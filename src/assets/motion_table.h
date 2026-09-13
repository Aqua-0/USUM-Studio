#pragma once
#include "core/binary.h"
#include <limits>
namespace studio {
inline constexpr std::size_t motion_table_path = std::numeric_limits<std::size_t>::max();
Bytes motion_table_resource(View bytes, std::size_t slot);
Bytes replace_motion_table_resource(View bytes, std::size_t slot, View replacement);
}
