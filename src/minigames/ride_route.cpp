#include "minigames/ride_route.h"
#include <cmath>
namespace studio {
double RideRoute::offset(std::int64_t node, unsigned axis) const {
    if (!curved || node < 3)
        return 0;
    auto value = std::uint32_t(node) * 0x9e3779b9u + seed + axis * 0x85ebca6bu;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return (double(value) / 4294967295.0 * 2 - 1) * (axis ? 1800 : 3000);
}
RideRouteFrame RideRoute::frame(double distance) const {
    constexpr double length = 12000;
    auto node = std::int64_t(std::floor(distance / length));
    double t = distance / length - double(node);
    double blend = t * t * t * (t * (t * 6 - 15) + 10);
    double derivative = 30 * t * t * (t - 1) * (t - 1) / length;
    RideRouteFrame result;
    double slope[2];
    for (unsigned axis = 0; axis < 2; ++axis) {
        double a = offset(node, axis), b = offset(node + 1, axis);
        result.center[axis] = a + (b - a) * blend;
        slope[axis] = (b - a) * derivative;
    }
    result.center[2] = -distance;
    auto normalize = [](std::array<float, 3> value) {
        float magnitude =
            std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
        for (auto &v : value)
            v /= magnitude;
        return value;
    };
    result.back = normalize({-float(slope[0]), -float(slope[1]), 1});
    result.right = normalize({result.back[2], 0, -result.back[0]});
    const auto &b = result.back;
    const auto &r = result.right;
    result.up = {b[1] * r[2] - b[2] * r[1], b[2] * r[0] - b[0] * r[2], b[0] * r[1] - b[1] * r[0]};
    return result;
}
std::array<float, 3> RideRoute::position(double distance, float x, float y, double origin) const {
    auto at = frame(distance), base = frame(origin);
    std::array<float, 3> result;
    for (unsigned i = 0; i < 3; ++i)
        result[i] = float(at.center[i] - base.center[i]) + at.right[i] * x + at.up[i] * y;
    return result;
}
}
