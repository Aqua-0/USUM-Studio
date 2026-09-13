#include "authoring/tile_grid.h"
#include <cmath>
#include <limits>

namespace studio {
void AuthoringGrid::validate() const {
    require(width > 0 && height > 0, "Tile grid dimensions must be positive");
    require(std::isfinite(tile_size) && tile_size > 0, "Tile size must be finite and positive");
    for (unsigned axis = 0; axis < 2; ++axis) {
        const double low = origin[axis],
                     high = low + double(axis == 0 ? width : height) * tile_size;
        require(std::isfinite(low) && std::isfinite(high) && std::abs(low) < 1e7 &&
                    std::abs(high) < 1e7,
                "Tile grid exceeds the supported map coordinate range");
        const auto first = float(low + tile_size), last = float(high - tile_size);
        require(first > origin[axis] && last < float(high),
                "Tile size is too small at this map position");
    }
}
std::optional<AuthoringTile> AuthoringGrid::tile_at(float x, float z) const {
    validate();
    if (!std::isfinite(x) || !std::isfinite(z))
        return {};
    const auto column = std::floor((double(x) - origin[0]) / tile_size),
               row = std::floor((double(z) - origin[1]) / tile_size);
    if (column < 0 || row < 0 || column >= width || row >= height)
        return {};
    return AuthoringTile{int(column), int(row)};
}
std::array<float, 2> AuthoringGrid::center(AuthoringTile tile) const {
    validate();
    require(tile.x >= 0 && tile.z >= 0 && tile.x < width && tile.z < height,
            "Selected tile is outside the authoring grid");
    return {float(origin[0] + (double(tile.x) + .5) * tile_size),
            float(origin[1] + (double(tile.z) + .5) * tile_size)};
}
std::optional<SpatialPoint> tile_ground_position(const AuthoringGrid &grid, AuthoringTile tile,
                                                 const SpatialScene &scene, float below_height) {
    require(std::isfinite(below_height), "Ground search height must be finite");
    const auto center = grid.center(tile);
    std::optional<SpatialPoint> result;
    for (const auto &region : scene.regions)
        if (region.kind == SpatialKind::Ground) {
            require(region.triangles.size() % 3 == 0, "Ground triangle list is incomplete");
            for (std::size_t i = 0; i < region.triangles.size(); i += 3) {
                const auto &a = region.vertices.at(region.triangles[i]).position;
                const auto &b = region.vertices.at(region.triangles[i + 1]).position;
                const auto &c = region.vertices.at(region.triangles[i + 2]).position;
                for (const auto *point : {&a, &b, &c})
                    for (float value : *point)
                        require(std::isfinite(value),
                                "Ground geometry contains a non-finite coordinate");
                const double bx = double(b[0]) - a[0], bz = double(b[2]) - a[2],
                             cx = double(c[0]) - a[0], cz = double(c[2]) - a[2];
                const double determinant = bx * cz - bz * cx;
                if (determinant == 0)
                    continue;
                const double x = double(center[0]) - a[0], z = double(center[1]) - a[2];
                const double u = (x * cz - z * cx) / determinant,
                             v = (bx * z - bz * x) / determinant;
                if (u < 0 || v < 0 || u + v > 1)
                    continue;
                const float y = float(a[1] + u * (double(b[1]) - a[1]) + v * (double(c[1]) - a[1]));
                if (y <= below_height && (!result || y > (*result)[1]))
                    result = SpatialPoint{center[0], y, center[1]};
            }
        }
    return result;
}
std::optional<SpatialPoint> pick_authoring_ground(const SpatialScene &scene, SpatialPoint origin,
                                                  SpatialPoint direction, float ceiling) {
    for (float value : origin)
        require(std::isfinite(value), "Invalid picking origin");
    for (float value : direction)
        require(std::isfinite(value), "Invalid picking direction");
    const auto cross = [](SpatialPoint a, SpatialPoint b) {
        return SpatialPoint{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                            a[0] * b[1] - a[1] * b[0]};
    };
    const auto sub = [](SpatialPoint a, SpatialPoint b) {
        return SpatialPoint{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    };
    const auto dot = [](SpatialPoint a, SpatialPoint b) {
        return double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2];
    };
    double nearest = std::numeric_limits<double>::infinity();
    for (const auto &region : scene.regions)
        if (region.kind == SpatialKind::Ground) {
            require(region.triangles.size() % 3 == 0, "Invalid ground triangle list");
            for (std::size_t i = 0; i < region.triangles.size(); i += 3) {
                const auto a = region.vertices.at(region.triangles[i]).position,
                           b = region.vertices.at(region.triangles[i + 1]).position,
                           c = region.vertices.at(region.triangles[i + 2]).position;
                const auto e = sub(b, a), f = sub(c, a), p = cross(direction, f);
                const auto determinant = dot(e, p);
                if (determinant == 0)
                    continue;
                const auto t = sub(origin, a);
                const double u = dot(t, p) / determinant;
                if (u < 0 || u > 1)
                    continue;
                const auto q = cross(t, e);
                const double v = dot(direction, q) / determinant;
                if (v < 0 || u + v > 1)
                    continue;
                const double distance = dot(f, q) / determinant;
                if (distance >= 0 && distance < nearest &&
                    origin[1] + direction[1] * distance <= ceiling)
                    nearest = distance;
            }
        }
    if (!std::isfinite(nearest))
        return {};
    return SpatialPoint{float(origin[0] + direction[0] * nearest),
                        float(origin[1] + direction[1] * nearest),
                        float(origin[2] + direction[2] * nearest)};
}

}
