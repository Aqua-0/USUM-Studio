#pragma once
#include "core/binary.h"
#include <array>
namespace studio {
struct Container {
    std::array<std::uint8_t, 2> tag{};
    std::vector<Bytes> files;
    Bytes original;
    static Container parse(View bytes, const std::string &expected = "");
    Bytes write(std::size_t alignment = 1) const;
};
}
