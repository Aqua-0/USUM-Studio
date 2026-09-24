#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace studio {
inline std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
}
