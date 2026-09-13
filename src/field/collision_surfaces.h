#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <cmath>
namespace studio {
inline constexpr std::array<const char *, 39> collision_surfaces = {"Tall grass",
                                                                    "Lawn grass",
                                                                    "Asphalt",
                                                                    "Stone pavement",
                                                                    "Yellow flowers",
                                                                    "Dirt",
                                                                    "Beach sand",
                                                                    "Shallow water",
                                                                    "Sea water",
                                                                    "Moss",
                                                                    "Rocky ground",
                                                                    "Suspension bridge",
                                                                    "Wood",
                                                                    "Carpet / rug",
                                                                    "Marble floor",
                                                                    "Vinyl floor",
                                                                    "Interior flooring",
                                                                    "Indoor concrete",
                                                                    "Indoor dirt",
                                                                    "Indoor wood",
                                                                    "Mudsdale rocky ground",
                                                                    "Fresh water",
                                                                    "Dry grass",
                                                                    "Volcanic soil",
                                                                    "Bamboo grove",
                                                                    "Desert sand",
                                                                    "Snow",
                                                                    "Metal",
                                                                    "Pasture grass",
                                                                    "Pier / dock",
                                                                    "Red flowers",
                                                                    "Short grass",
                                                                    "Long grass",
                                                                    "Large tree wood",
                                                                    "Glass",
                                                                    "Broken glass",
                                                                    "Ultra Wormhole floor",
                                                                    "Ice cave floor",
                                                                    "Grass variant (provisional)"};
inline std::array<float, 4> collision_surface_color(std::uint32_t id) {
    double hue = std::fmod(.31 + double(id) * .6180339887498949, 1.0);
    float saturation = id % 3 == 0 ? .65f : id % 3 == 1 ? .85f : .48f, value = id % 2 ? .95f : .78f;
    float h = float(hue) * 6, c = value * saturation, x = c * (1 - std::abs(std::fmod(h, 2.f) - 1)),
          m = value - c;
    std::array<float, 4> color{};
    switch (unsigned(h)) {
    case 0:
        color = {c, x, 0, 1};
        break;
    case 1:
        color = {x, c, 0, 1};
        break;
    case 2:
        color = {0, c, x, 1};
        break;
    case 3:
        color = {0, x, c, 1};
        break;
    case 4:
        color = {x, 0, c, 1};
        break;
    default:
        color = {c, 0, x, 1};
    }
    for (unsigned k = 0; k < 3; ++k)
        color[k] += m;
    return color;
}
inline std::string collision_surface_name(std::uint32_t id) {
    return id < collision_surfaces.size() ? collision_surfaces[id]
                                          : "Unknown surface " + std::to_string(id);
}
}
