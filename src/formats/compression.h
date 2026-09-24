#pragma once
#include "core/binary.h"
namespace studio {
Bytes decompress(View bytes, std::size_t *consumed = nullptr);
Bytes compress(View bytes);
}
