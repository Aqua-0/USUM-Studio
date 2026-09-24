#pragma once
#include "core/binary.h"
namespace studio {
std::string sha256(View bytes);
std::string sha256_file(const std::filesystem::path &path);
}
