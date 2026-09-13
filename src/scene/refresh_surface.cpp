#include "scene/refresh_surface.h"
#include <algorithm>
#include <cmath>
namespace studio {
namespace {
using Point = std::array<float, 3>;
Point subtract(Point a, Point b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Point cross(Point a, Point b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float dot(Point a, Point b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
}
RefreshSurface::RefreshSurface(const Environment &scene, const RefreshRegionPack &regions,
                               const std::string &prefix,
                               const std::vector<std::vector<Matrix>> &poses,
                               const std::vector<bool> &visible) {
    for (std::size_t index = 0; index < scene.draws.size(); ++index) {
        if (index >= visible.size() || !visible[index])
            continue;
        auto &draw = scene.draws[index];
        auto &material = scene.materials.at(draw.material);
        auto binding = bind_refresh_material(regions, material, prefix);
        if (binding.excluded)
            continue;
        Draw prepared{index,
                      binding.mask,
                      material.cull,
                      {INFINITY, INFINITY, INFINITY},
                      {-INFINITY, -INFINITY, -INFINITY},
                      {}};
        std::vector<Point> positions;
        positions.reserve(draw.vertices.size());
        for (auto &vertex : draw.vertices) {
            Point p{vertex.x, vertex.y, vertex.z};
            if (draw.skeleton >= 0) {
                p = {};
                for (unsigned weight = 0; weight < 4; ++weight)
                    if (vertex.weights[weight] > 0) {
                        auto &m = poses.at(std::size_t(draw.skeleton))
                                      .at(draw.palette.at(unsigned(vertex.joints[weight])));
                        for (unsigned k = 0; k < 3; ++k)
                            p[k] += vertex.weights[weight] *
                                    (m[k * 4] * vertex.x + m[k * 4 + 1] * vertex.y +
                                     m[k * 4 + 2] * vertex.z + m[k * 4 + 3]);
                    }
            }
            positions.push_back(p);
            for (unsigned k = 0; k < 3; ++k) {
                prepared.low[k] = std::min(prepared.low[k], p[k]);
                prepared.high[k] = std::max(prepared.high[k], p[k]);
            }
        }
        prepared.triangles.reserve(draw.indices.size() / 3);
        for (std::size_t i = 0; i + 2 < draw.indices.size(); i += 3) {
            auto a = draw.indices[i], b = draw.indices[i + 1], c = draw.indices[i + 2];
            auto &va = draw.vertices.at(a);
            auto &vb = draw.vertices.at(b);
            auto &vc = draw.vertices.at(c);
            prepared.triangles.push_back({positions.at(a),
                                          subtract(positions.at(b), positions.at(a)),
                                          subtract(positions.at(c), positions.at(a)),
                                          {va.u, va.v},
                                          {vb.u - va.u, vb.v - va.v},
                                          {vc.u - va.u, vc.v - va.v}});
        }
        draws_.push_back(std::move(prepared));
    }
}
std::optional<RefreshSurfaceHit> RefreshSurface::hit(const Point &origin,
                                                     const Point &direction) const {
    std::optional<RefreshSurfaceHit> result;
    for (auto &draw : draws_) {
        float near = 0, far = result ? result->distance : INFINITY;
        bool inside = true;
        for (unsigned k = 0; k < 3; ++k) {
            if (std::abs(direction[k]) < 1e-12f) {
                if (origin[k] < draw.low[k] || origin[k] > draw.high[k])
                    inside = false;
            } else {
                float a = (draw.low[k] - origin[k]) / direction[k],
                      b = (draw.high[k] - origin[k]) / direction[k];
                if (a > b)
                    std::swap(a, b);
                near = std::max(near, a);
                far = std::min(far, b);
            }
        }
        if (!inside || near > far)
            continue;
        for (auto &triangle : draw.triangles) {
            auto p = cross(direction, triangle.edge2);
            float determinant = dot(triangle.edge1, p);
            if (std::abs(determinant) < 1e-8f || (draw.cull == 1 && determinant > 0) ||
                (draw.cull == 2 && determinant < 0))
                continue;
            auto t = subtract(origin, triangle.origin);
            float u = dot(t, p) / determinant;
            if (u < 0 || u > 1)
                continue;
            auto q = cross(t, triangle.edge1);
            float v = dot(direction, q) / determinant;
            if (v < 0 || u + v > 1)
                continue;
            float distance = dot(triangle.edge2, q) / determinant;
            if (distance < 0 || (result && distance >= result->distance))
                continue;
            result = RefreshSurfaceHit{
                draw.index, draw.mask, triangle.uv[0] + u * triangle.uv1[0] + v * triangle.uv2[0],
                triangle.uv[1] + u * triangle.uv1[1] + v * triangle.uv2[1], distance};
        }
    }
    return result;
}
std::optional<RefreshSurfaceHit>
refresh_surface_hit(const Environment &scene, const RefreshRegionPack &regions,
                    const std::string &prefix, const std::vector<std::vector<Matrix>> &poses,
                    const std::vector<bool> &visible, const Point &origin, const Point &direction) {
    return RefreshSurface(scene, regions, prefix, poses, visible).hit(origin, direction);
}
std::vector<RefreshUvSegment> refresh_uv_segments(std::array<float, 2> start,
                                                  std::array<float, 2> end) {
    std::vector<double> cuts{0, 1};
    for (unsigned axis = 0; axis < 2; ++axis) {
        require(std::isfinite(start[axis]) && std::isfinite(end[axis]), "Invalid Refresh UV edge");
        double low = std::min(start[axis], end[axis]), high = std::max(start[axis], end[axis]);
        for (double boundary = std::floor(low) + 1; boundary < high; ++boundary)
            cuts.push_back((boundary - start[axis]) / (double(end[axis]) - start[axis]));
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    std::vector<RefreshUvSegment> segments;
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        RefreshUvSegment segment;
        for (unsigned axis = 0; axis < 2; ++axis) {
            double delta = double(end[axis]) - start[axis];
            double tile = std::floor(start[axis] + delta * (cuts[i - 1] + cuts[i]) * .5);
            segment[0][axis] = float(start[axis] + delta * cuts[i - 1] - tile);
            segment[1][axis] = float(start[axis] + delta * cuts[i] - tile);
        }
        if (segment[0] != segment[1])
            segments.push_back(segment);
    }
    return segments;
}
bool paint_refresh_disc(RefreshRegionMask &mask, float u, float v, unsigned radius,
                        std::uint8_t category) {
    require(mask.read_only_reason.empty(), mask.read_only_reason);
    require(known_refresh_region(category), "Unknown paint category");
    require(mask.width && mask.height && mask.ids.size() == std::size_t(mask.width) * mask.height,
            "Invalid Refresh mask size");
    require(std::isfinite(u) && std::isfinite(v) && radius >= 1 && radius <= 64,
            "Invalid Refresh brush");
    int cx = std::min(int((u - std::floor(u)) * mask.width), int(mask.width) - 1),
        cy = std::min(int((v - std::floor(v)) * mask.height), int(mask.height) - 1),
        r = int(radius);
    bool changed = false;
    for (int dy = 1 - r; dy < r; ++dy)
        for (int dx = 1 - r; dx < r; ++dx)
            if (dx * dx + dy * dy < r * r) {
                auto x = ((cx + dx) % mask.width + mask.width) % mask.width,
                     y = ((cy + dy) % mask.height + mask.height) % mask.height;
                auto &value = mask.ids[std::size_t(y) * mask.width + std::size_t(x)];
                if (value != category) {
                    value = category;
                    changed = true;
                }
            }
    return changed;
}
}
