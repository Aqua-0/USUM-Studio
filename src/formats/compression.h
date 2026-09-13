#pragma once
#include "core/binary.h"
namespace studio {
Bytes decompress(View bytes);
Bytes compress(View bytes);
}
