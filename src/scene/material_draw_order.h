#pragma once
#include "scene/environment.h"
#include <algorithm>
#include <numeric>
#include <tuple>
namespace studio {
inline std::vector<std::size_t> material_draw_order(const std::vector<SceneDraw> &draws,
                                                   const std::vector<SceneMaterial> &materials,
                                                   std::size_t uploaded = std::size_t(-1)) {
    std::vector<std::size_t> order(std::min(draws.size(), uploaded));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        if ((draws[a].sky_part >= 0) != (draws[b].sky_part >= 0))
            return draws[a].sky_part >= 0;
        if (bool(draws[a].weather_mask) != bool(draws[b].weather_mask))
            return !draws[a].weather_mask;
        const auto &left = materials.at(draws[a].material);
        const auto &right = materials.at(draws[b].material);
        return std::tie(left.layer, left.priority) < std::tie(right.layer, right.priority);
    });
    return order;
}
}
