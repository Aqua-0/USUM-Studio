#pragma once
#include <array>
#include <cstdint>
namespace studio {
struct RideRouteFrame {
    std::array<double, 3> center{};
    std::array<float, 3> right{1, 0, 0}, up{0, 1, 0}, back{0, 0, 1};
};
class RideRoute {
  public:
    std::uint32_t seed = 1;
    bool curved = true;
    RideRouteFrame frame(double distance) const;
    std::array<float, 3> position(double distance, float x, float y, double origin) const;

  private:
    double offset(std::int64_t node, unsigned axis) const;
};
}
