#pragma once
#include "scene/environment.h"
#include "assets/refresh_regions.h"
#include <optional>
namespace studio {
struct RefreshSurfaceHit {
    std::size_t draw = 0;
    int mask = -1;
    float u = 0, v = 0, distance = 0;
};
class RefreshSurface {
  public:
    RefreshSurface(const Environment &scene, const RefreshRegionPack &regions,
                   const std::string &prefix, const std::vector<std::vector<Matrix>> &poses,
                   const std::vector<bool> &visible);
    std::optional<RefreshSurfaceHit> hit(const std::array<float, 3> &origin,
                                         const std::array<float, 3> &direction) const;

  private:
    struct Triangle {
        std::array<float, 3> origin, edge1, edge2;
        std::array<float, 2> uv, uv1, uv2;
    };
    struct Draw {
        std::size_t index;
        int mask;
        unsigned cull;
        std::array<float, 3> low, high;
        std::vector<Triangle> triangles;
    };
    std::vector<Draw> draws_;
};
std::optional<RefreshSurfaceHit>
refresh_surface_hit(const Environment &scene, const RefreshRegionPack &regions,
                    const std::string &prefix, const std::vector<std::vector<Matrix>> &poses,
                    const std::vector<bool> &visible, const std::array<float, 3> &origin,
                    const std::array<float, 3> &direction);
using RefreshUvSegment = std::array<std::array<float, 2>, 2>;
std::vector<RefreshUvSegment> refresh_uv_segments(std::array<float, 2> start,
                                                  std::array<float, 2> end);
bool paint_refresh_disc(RefreshRegionMask &mask, float u, float v, unsigned radius,
                        std::uint8_t category);
}
