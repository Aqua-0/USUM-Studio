#pragma once
#include "formats/container.h"
#include <array>
#include <string_view>
namespace studio {
std::string_view refresh_region_label(std::uint8_t id);
std::string_view refresh_material_mode_label(std::uint8_t mode);
bool known_refresh_region(std::uint8_t id);
struct RefreshMaterialRule {
    std::string material;
    std::uint8_t mode = 0;
};
struct RefreshRegionMask {
    std::size_t child = 0;
    std::string texture;
    std::uint16_t width = 0, height = 0, format = 0;
    std::string read_only_reason;
    Bytes ids; // Top-down rows, matching decoded scene textures.
    std::array<std::size_t, 256> counts{};
};
struct RefreshRegionPack {
    std::vector<RefreshMaterialRule> materials;
    std::vector<RefreshRegionMask> masks;
};
struct SceneMaterial;
struct RefreshMaterialBinding {
    int mask = -1;
    bool excluded = false;
    std::string reason;
};
RefreshMaterialBinding bind_refresh_material(const RefreshRegionPack &pack,
                                             const SceneMaterial &material,
                                             const std::string &prefix);
std::array<float, 4> refresh_region_color(std::uint8_t id);
RefreshRegionPack decode_refresh_regions(View source);
Bytes replace_refresh_region_pixels(View source, std::size_t child, View ids);
}
