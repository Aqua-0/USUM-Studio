#pragma once
#include "scene/spatial.h"
#include <optional>
#include <limits>

namespace studio {
struct AuthoringTile {
    int x = 0, z = 0;
    bool operator==(const AuthoringTile &) const = default;
};
struct AuthoringGrid {
    std::array<float, 2> origin{};
    float tile_size = 100;
    int width = 1, height = 1;
    bool operator==(const AuthoringGrid &) const = default;
    void validate() const;
    std::optional<AuthoringTile> tile_at(float x, float z) const;
    std::array<float, 2> center(AuthoringTile tile) const;
};
std::optional<SpatialPoint>
pick_authoring_ground(const SpatialScene &scene, SpatialPoint origin, SpatialPoint direction,
                      float ceiling = std::numeric_limits<float>::infinity());
std::optional<SpatialPoint> tile_ground_position(const AuthoringGrid &grid, AuthoringTile tile,
                                                 const SpatialScene &scene, float below_height);
}
