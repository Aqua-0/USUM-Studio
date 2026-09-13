#include "authoring/ground_surface.h"
#include "core/digest.h"
#include "formats/archive.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <sstream>
#include <locale>
#include <tuple>

namespace studio {
SpatialPoint ground_vertex(const AuthoringGrid &grid, const GroundSurface &ground, int x, int z) {
    require(x >= 0 && z >= 0 && x <= grid.width && z <= grid.height,
            "Ground vertex is outside the surface");
    const auto index = std::size_t(z) * (std::size_t(grid.width) + 1) + x;
    const auto offset = ground.offsets.empty() ? std::array<float, 2>{} : ground.offsets.at(index);
    return {grid.origin[0] + x * grid.tile_size + offset[0], ground.heights.at(index),
            grid.origin[1] + z * grid.tile_size + offset[1]};
}
namespace {
float ground_area(SpatialPoint a, SpatialPoint b, SpatialPoint c) {
    return (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
}
}

std::vector<TerrainTriangle> terrain_triangles(const AuthoringGrid &grid,
                                               const GroundSurface &source) {
    auto result = source.triangles;
    if (!result.empty())
        return result;
    const auto stride = std::uint32_t(grid.width + 1);
    for (int z = 0; z < grid.height; ++z)
        for (int x = 0; x < grid.width; ++x) {
            const auto a = std::uint32_t(z) * stride + x, cell = std::uint32_t(z * grid.width + x);
            result.push_back({{a, a + stride, a + 1}, cell});
            result.push_back({{a + 1, a + stride, a + stride + 1}, cell});
        }
    return result;
}
namespace {
GroundSurface terrain_mesh(const AuthoringGrid &grid, const GroundSurface &source) {
    auto result = source;
    result.triangles = terrain_triangles(grid, source);
    return result;
}
bool terrain_cell_selected(const AuthoringGrid &grid, std::uint32_t cell, AuthoringTile first,
                           AuthoringTile last) {
    const int x = int(cell) % grid.width, z = int(cell) / grid.width;
    return x >= std::min(first.x, last.x) && x <= std::max(first.x, last.x) &&
           z >= std::min(first.z, last.z) && z <= std::max(first.z, last.z);
}
using TerrainEdge = std::pair<std::uint32_t, std::uint32_t>;
TerrainEdge terrain_edge(std::uint32_t a, std::uint32_t b) {
    return {std::min(a, b), std::max(a, b)};
}
void terrain_set_positions(const AuthoringGrid &grid, GroundSurface &ground,
                           const std::map<std::uint32_t, SpatialPoint> &targets) {
    auto positions = terrain_positions(grid, ground);
    for (std::uint32_t index = 0; index < positions.size(); ++index) {
        auto found = targets.find(index);
        if (index < ground.heights.size()) {
            if (found == targets.end())
                continue;
            const auto target = found->second;
            if (ground.offsets.empty())
                ground.offsets.resize(ground.heights.size());
            ground.heights[index] = target[1];
            ground.offsets[index] = {
                target[0] - grid.origin[0] - float(index % (grid.width + 1)) * grid.tile_size,
                target[2] - grid.origin[1] - float(index / (grid.width + 1)) * grid.tile_size};
            positions[index] = target;
        } else {
            auto &point = ground.points[index - ground.heights.size()];
            auto position = point.offset;
            for (unsigned i = 0; i < 3; ++i)
                for (unsigned a = 0; a < 3; ++a)
                    position[a] += positions[point.parents[i]][a] * point.weights[i];
            if (found != targets.end()) {
                for (unsigned a = 0; a < 3; ++a)
                    point.offset[a] += found->second[a] - position[a];
                position = found->second;
            }
            positions[index] = position;
        }
    }
}
}
std::vector<SpatialPoint> terrain_positions(const AuthoringGrid &grid,
                                            const GroundSurface &ground) {
    std::vector<SpatialPoint> result;
    for (int z = 0; z <= grid.height; ++z)
        for (int x = 0; x <= grid.width; ++x)
            result.push_back(ground_vertex(grid, ground, x, z));
    for (const auto &point : ground.points) {
        auto p = point.offset;
        for (unsigned i = 0; i < 3; ++i) {
            require(point.parents[i] < result.size(), "Terrain point has an invalid parent");
            for (unsigned a = 0; a < 3; ++a)
                p[a] += result[point.parents[i]][a] * point.weights[i];
        }
        result.push_back(p);
    }
    return result;
}
std::vector<std::array<float, 2>> terrain_coordinates(const AuthoringGrid &grid,
                                                      const GroundSurface &ground) {
    std::vector<std::array<float, 2>> result;
    for (int z = 0; z <= grid.height; ++z)
        for (int x = 0; x <= grid.width; ++x)
            result.push_back({float(x), float(z)});
    for (const auto &point : ground.points) {
        auto uv = point.texture_offset;
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned a = 0; a < 2; ++a)
                uv[a] += result.at(point.parents[i])[a] * point.weights[i];
        result.push_back(uv);
    }
    return result;
}
GroundSurface subdivide_terrain(const AuthoringGrid &grid, const GroundSurface &ground,
                                AuthoringTile first, AuthoringTile last,
                                TerrainSubdivision method) {
    ground.validate(grid);
    grid.center(first);
    grid.center(last);
    require(method == TerrainSubdivision::Even || method == TerrainSubdivision::LongestEdge ||
                method == TerrainSubdivision::FaceCenter,
            "Unknown subdivision method");
    auto next = terrain_mesh(grid, ground);
    const auto positions = terrain_positions(grid, ground);
    std::set<TerrainEdge> edges;
    const auto length = [&](SpatialPoint a, SpatialPoint b) {
        float result = 0;
        for (unsigned i = 0; i < 3; ++i)
            result += (a[i] - b[i]) * (a[i] - b[i]);
        return result;
    };
    std::size_t centers = 0;
    for (const auto &triangle : next.triangles)
        if (terrain_cell_selected(grid, triangle.cell, first, last)) {
            if (method == TerrainSubdivision::FaceCenter) {
                ++centers;
                continue;
            }
            unsigned longest = 0;
            for (unsigned i = 1; i < 3; ++i)
                if (length(positions[triangle.vertices[i]],
                           positions[triangle.vertices[(i + 1) % 3]]) >
                    length(positions[triangle.vertices[longest]],
                           positions[triangle.vertices[(longest + 1) % 3]]))
                    longest = i;
            for (unsigned i = 0; i < 3; ++i)
                if (method == TerrainSubdivision::Even || i == longest)
                    edges.insert(
                        terrain_edge(triangle.vertices[i], triangle.vertices[(i + 1) % 3]));
        }
    require(next.heights.size() + next.points.size() + edges.size() + centers <= 60000,
            "Subdivision exceeds 60,000 terrain points; select a smaller region or simplify first");
    std::map<TerrainEdge, std::uint32_t> midpoints;
    for (auto [a, b] : edges) {
        midpoints[{a, b}] = std::uint32_t(next.heights.size() + next.points.size());
        next.points.push_back({{a, b, a}, {.5f, .5f, 0}, {}});
    }
    std::vector<TerrainTriangle> result;
    for (const auto &original : next.triangles) {
        const auto v = original.vertices;
        const auto add = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
            result.push_back({{a, b, c}, original.cell});
        };
        if (method == TerrainSubdivision::FaceCenter &&
            terrain_cell_selected(grid, original.cell, first, last)) {
            const auto center = std::uint32_t(next.heights.size() + next.points.size());
            next.points.push_back({v, {1.f / 3, 1.f / 3, 1.f / 3}, {}});
            for (unsigned i = 0; i < 3; ++i)
                add(v[i], v[(i + 1) % 3], center);
            continue;
        }
        std::array<std::uint32_t, 3> middle{};
        unsigned mask = 0;
        for (unsigned i = 0; i < 3; ++i) {
            auto found = midpoints.find(terrain_edge(v[i], v[(i + 1) % 3]));
            if (found != midpoints.end()) {
                middle[i] = found->second;
                mask |= 1u << i;
            }
        }
        if (mask == 7) {
            add(v[0], middle[0], middle[2]);
            add(middle[0], v[1], middle[1]);
            add(middle[2], middle[1], v[2]);
            add(middle[0], middle[1], middle[2]);
        } else if (mask == 0)
            result.push_back(original);
        else if (mask == 1 || mask == 2 || mask == 4) {
            const unsigned i = mask == 1 ? 0 : mask == 2 ? 1 : 2;
            add(v[i], middle[i], v[(i + 2) % 3]);
            add(middle[i], v[(i + 1) % 3], v[(i + 2) % 3]);
        } else {
            unsigned i = 0;
            while (!(mask & (1u << i)) || !(mask & (1u << ((i + 1) % 3))))
                ++i;
            const auto a = v[i], b = v[(i + 1) % 3], c = v[(i + 2) % 3], u = middle[i],
                       w = middle[(i + 1) % 3];
            add(b, w, u);
            SpatialPoint pu{}, pw{};
            for (unsigned axis = 0; axis < 3; ++axis) {
                pu[axis] = (positions[a][axis] + positions[b][axis]) * .5f;
                pw[axis] = (positions[b][axis] + positions[c][axis]) * .5f;
            }
            if (length(positions[a], pw) <= length(pu, positions[c])) {
                add(a, u, w);
                add(a, w, c);
            } else {
                add(a, u, c);
                add(u, w, c);
            }
        }
    }
    next.triangles = std::move(result);
    next.validate(grid);
    return next;
}
GroundSurface move_terrain_points(const AuthoringGrid &grid, const GroundSurface &ground,
                                  const std::vector<std::uint32_t> &selected, SpatialPoint delta,
                                  float radius) {
    require(std::isfinite(radius) && radius >= 0, "Influence radius must be nonnegative");
    auto next = terrain_mesh(grid, ground);
    const auto positions = terrain_positions(grid, next);
    for (auto i : selected)
        require(i < positions.size(), "Selected terrain point no longer exists");
    std::map<std::uint32_t, SpatialPoint> targets;
    for (std::uint32_t index = 0; index < positions.size(); ++index) {
        float weight = 0;
        for (auto i : selected) {
            if (i == index) {
                weight = 1;
                break;
            }
            if (radius > 0) {
                float distance = 0;
                for (unsigned a = 0; a < 3; ++a)
                    distance += (positions[index][a] - positions[i][a]) *
                                (positions[index][a] - positions[i][a]);
                const float t = std::clamp(1 - std::sqrt(distance) / radius, 0.f, 1.f);
                weight = std::max(weight, t * t * (3 - 2 * t));
            }
        }
        auto target = positions[index];
        for (unsigned a = 0; a < 3; ++a)
            target[a] += delta[a] * weight;
        targets[index] = target;
    }
    terrain_set_positions(grid, next, targets);
    next.validate(grid);
    return next;
}
GroundSurface extrude_terrain_edge(const AuthoringGrid &grid, const GroundSurface &ground,
                                   std::uint32_t a, std::uint32_t b, SpatialPoint delta) {
    auto next = terrain_mesh(grid, ground);
    unsigned count = 0;
    std::uint32_t cell = 0, u = 0, v = 0;
    for (const auto &triangle : next.triangles)
        for (unsigned i = 0; i < 3; ++i)
            if (terrain_edge(triangle.vertices[i], triangle.vertices[(i + 1) % 3]) ==
                terrain_edge(a, b)) {
                ++count;
                cell = triangle.cell;
                u = triangle.vertices[i];
                v = triangle.vertices[(i + 1) % 3];
            }
    require(count == 1, "Choose two endpoints of an outer boundary edge");
    const auto p = std::uint32_t(next.heights.size() + next.points.size());
    next.points.push_back({{u, u, u}, {1, 0, 0}, delta});
    next.points.push_back({{v, v, v}, {1, 0, 0}, delta});
    const auto uv = terrain_coordinates(grid, ground);
    const float du = uv[v][0] - uv[u][0], dv = uv[v][1] - uv[u][1],
                n = std::max(.0001f, std::hypot(du, dv)),
                distance =
                    std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]) /
                    grid.tile_size;
    next.points[next.points.size() - 2].texture_offset = {-dv / n * distance, du / n * distance};
    next.points.back().texture_offset = next.points[next.points.size() - 2].texture_offset;
    next.triangles.push_back({{v, u, p}, cell});
    next.triangles.push_back({{v, p, p + 1}, cell});
    next.validate(grid);
    return next;
}
GroundSurface shape_terrain_selection(const AuthoringGrid &grid, const GroundSurface &ground,
                                      AuthoringTile first, AuthoringTile last, bool smooth,
                                      float value, bool lock_boundary) {
    grid.center(first);
    grid.center(last);
    require(std::isfinite(value), "Shape amount must be finite");
    auto next = terrain_mesh(grid, ground);
    const auto positions = terrain_positions(grid, next);
    std::set<std::uint32_t> selected, boundary;
    std::map<TerrainEdge, unsigned> edges;
    std::map<std::uint32_t, std::set<std::uint32_t>> neighbors;
    for (const auto &triangle : next.triangles)
        if (terrain_cell_selected(grid, triangle.cell, first, last))
            for (unsigned i = 0; i < 3; ++i) {
                const auto a = triangle.vertices[i], b = triangle.vertices[(i + 1) % 3];
                selected.insert(a);
                ++edges[terrain_edge(a, b)];
                neighbors[a].insert(b);
                neighbors[b].insert(a);
            }
    for (auto [edge, count] : edges)
        if (count == 1) {
            boundary.insert(edge.first);
            boundary.insert(edge.second);
        }
    std::map<std::uint32_t, SpatialPoint> targets;
    for (auto index : selected) {
        if (lock_boundary && boundary.contains(index))
            continue;
        auto target = positions[index];
        if (smooth) {
            float y = 0;
            for (auto other : neighbors[index])
                y += positions[other][1];
            target[1] +=
                (y / float(neighbors[index].size()) - target[1]) * std::clamp(value, 0.f, 1.f);
        } else
            target[1] = value;
        targets[index] = target;
    }
    terrain_set_positions(grid, next, targets);
    next.validate(grid);
    return next;
}
GroundSurface simplify_terrain_selection(const AuthoringGrid &grid, const GroundSurface &ground,
                                         AuthoringTile first, AuthoringTile last, float tolerance) {
    grid.center(first);
    grid.center(last);
    require(std::isfinite(tolerance) && tolerance >= 0,
            "Simplification tolerance must be nonnegative");
    auto next = terrain_mesh(grid, ground);
    const auto positions = terrain_positions(grid, next);
    const auto coordinates = terrain_coordinates(grid, next);
    std::vector<std::set<std::size_t>> adjacent(positions.size());
    std::set<std::size_t> removed;
    for (std::size_t i = 0; i < next.triangles.size(); ++i)
        for (auto point : next.triangles[i].vertices)
            adjacent[point].insert(i);
    for (std::size_t index = next.heights.size(); index < positions.size(); ++index) {
        std::vector<std::size_t> incident;
        std::map<std::uint32_t, std::uint32_t> ring;
        std::optional<std::uint32_t> cell;
        bool valid = true;
        for (auto i : adjacent[index]) {
            const auto &t = next.triangles[i];
            for (unsigned c = 0; c < 3; ++c)
                if (t.vertices[c] == index) {
                    if (!terrain_cell_selected(grid, t.cell, first, last) ||
                        (!ground.blends.empty() && !ground.blends[t.cell].texture.empty()) ||
                        (cell && *cell != t.cell)) {
                        valid = false;
                        break;
                    }
                    cell = t.cell;
                    incident.push_back(i);
                    if (!ring.emplace(t.vertices[(c + 1) % 3], t.vertices[(c + 2) % 3]).second)
                        valid = false;
                }
        }
        if (!valid || incident.size() < 3 || ring.size() != incident.size())
            continue;
        std::vector<std::uint32_t> polygon;
        auto v = ring.begin()->first;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            polygon.push_back(v);
            auto it = ring.find(v);
            if (it == ring.end()) {
                valid = false;
                break;
            }
            v = it->second;
        }
        if (!valid || v != polygon.front() ||
            std::set<std::uint32_t>(polygon.begin(), polygon.end()).size() != polygon.size())
            continue;
        bool convex = false;
        for (std::size_t rotation = 0; rotation < polygon.size(); ++rotation) {
            convex = true;
            for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
                if (ground_area(positions[polygon[0]], positions[polygon[i]],
                                positions[polygon[i + 1]]) <= .0001f)
                    convex = false;
            if (convex)
                break;
            std::rotate(polygon.begin(), polygon.begin() + 1, polygon.end());
        }
        if (!convex)
            continue;
        const auto a = positions[polygon[0]], b = positions[polygon[1]], c = positions[polygon[2]];
        SpatialPoint n{(b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
                       (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
                       (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])};
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (length < 1e-6f)
            continue;
        for (auto p : polygon) {
            float distance = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
                distance += (positions[p][axis] - a[axis]) * n[axis];
            if (std::abs(distance) > tolerance * length)
                valid = false;
        }
        float distance = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            distance += (positions[index][axis] - a[axis]) * n[axis];
        if (std::abs(distance) > tolerance * length)
            valid = false;
        // Only convex horizontal patches; retain cliffs, seams and silhouettes.
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
            if (ground_area(a, positions[polygon[i]], positions[polygon[i + 1]]) <= .0001f)
                valid = false;
        const float area = ground_area(a, b, c);
        for (auto p : polygon) {
            const float wa = ground_area(b, c, positions[p]) / area,
                        wb = ground_area(c, a, positions[p]) / area, wc = 1 - wa - wb;
            for (unsigned axis = 0; axis < 2; ++axis)
                if (std::abs(coordinates[p][axis] - wa * coordinates[polygon[0]][axis] -
                             wb * coordinates[polygon[1]][axis] -
                             wc * coordinates[polygon[2]][axis]) > .0001f)
                    valid = false;
        }
        const float wa = ground_area(b, c, positions[index]) / area,
                    wb = ground_area(c, a, positions[index]) / area, wc = 1 - wa - wb;
        for (unsigned axis = 0; axis < 2; ++axis)
            if (std::abs(coordinates[index][axis] - wa * coordinates[polygon[0]][axis] -
                         wb * coordinates[polygon[1]][axis] - wc * coordinates[polygon[2]][axis]) >
                .0001f)
                valid = false;
        if (!valid)
            continue;
        for (auto i : incident) {
            removed.insert(i);
            for (auto point : next.triangles[i].vertices)
                adjacent[point].erase(i);
        }
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            const TerrainTriangle face{{polygon[0], polygon[i], polygon[i + 1]}, *cell};
            const auto identity = next.triangles.size();
            next.triangles.push_back(face);
            for (auto point : face.vertices)
                adjacent[point].insert(identity);
        }
    }
    std::vector<TerrainTriangle> triangles;
    for (std::size_t i = 0; i < next.triangles.size(); ++i)
        if (!removed.contains(i))
            triangles.push_back(next.triangles[i]);
    next.triangles = std::move(triangles);
    next.validate(grid);
    return next;
}
std::optional<AuthoringTile> ground_tile_at(const AuthoringGrid &grid, const GroundSurface &ground,
                                            float x, float z, std::optional<float> height) {
    if (!ground.triangles.empty() && height) {
        const auto positions = terrain_positions(grid, ground);
        std::optional<AuthoringTile> result;
        float nearest = INFINITY;
        for (const auto &t : ground.triangles) {
            const auto a = positions[t.vertices[0]], b = positions[t.vertices[1]],
                       c = positions[t.vertices[2]];
            SpatialPoint u{}, v{}, w{};
            for (unsigned i = 0; i < 3; ++i) {
                u[i] = b[i] - a[i];
                v[i] = c[i] - a[i];
                w[i] = SpatialPoint{x, *height, z}[i] - a[i];
            }
            const auto dot = [](SpatialPoint a, SpatialPoint b) {
                return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
            };
            const float uu = dot(u, u), uv = dot(u, v), vv = dot(v, v), wu = dot(w, u),
                        wv = dot(w, v), d = uu * vv - uv * uv;
            if (d <= 0)
                continue;
            const float s = (wu * vv - wv * uv) / d, r = (wv * uu - wu * uv) / d;
            if (s < -.001f || r < -.001f || s + r > 1.001f)
                continue;
            float distance = 0;
            for (unsigned i = 0; i < 3; ++i)
                distance += (w[i] - s * u[i] - r * v[i]) * (w[i] - s * u[i] - r * v[i]);
            if (distance < nearest) {
                nearest = distance;
                result = AuthoringTile{int(t.cell) % grid.width, int(t.cell) / grid.width};
            }
        }
        return nearest < .01f ? result : std::nullopt;
    }
    const SpatialPoint point{x, 0, z};
    for (int row = grid.height - 1; row >= 0; --row)
        for (int column = grid.width - 1; column >= 0; --column) {
            const auto a = ground_vertex(grid, ground, column, row),
                       b = ground_vertex(grid, ground, column, row + 1),
                       c = ground_vertex(grid, ground, column + 1, row + 1),
                       d = ground_vertex(grid, ground, column + 1, row);
            const auto inside = [&](SpatialPoint p, SpatialPoint q, SpatialPoint r) {
                return ground_area(p, q, point) >= -.001f && ground_area(q, r, point) >= -.001f &&
                       ground_area(r, p, point) >= -.001f;
            };
            if (inside(a, b, d) || inside(d, b, c))
                return AuthoringTile{column, row};
        }
    return {};
}
GroundSurface move_ground_vertex(const AuthoringGrid &grid, const GroundSurface &ground,
                                 AuthoringTile vertex, SpatialPoint delta) {
    ground_vertex(grid, ground, vertex.x, vertex.z);
    for (float value : delta)
        require(std::isfinite(value), "Vertex movement must be finite");
    auto next = ground;
    const auto index = std::size_t(vertex.z) * (std::size_t(grid.width) + 1) + vertex.x;
    if (delta[0] != 0 || delta[2] != 0) {
        if (next.offsets.empty())
            next.offsets.resize(next.heights.size());
        next.offsets[index][0] += delta[0];
        next.offsets[index][1] += delta[2];
    }
    next.heights[index] += delta[1];
    next.validate(grid);
    return next;
}
void GroundSurface::validate(const AuthoringGrid &grid) const {
    grid.validate();
    require(points.size() + heights.size() <= 60000 && triangles.size() <= 120000,
            "Terrain mesh exceeds editor geometry limits");
    require(points.empty() || !triangles.empty(), "Terrain points need mesh faces");
    for (const auto &point : points) {
        for (float uv : point.texture_offset)
            require(std::isfinite(uv) && std::abs(uv) < 1e7f,
                    "Invalid terrain texture coordinates");
        float sum = 0;
        for (float weight : point.weights) {
            require(std::isfinite(weight) && weight >= 0 && weight <= 1,
                    "Invalid terrain interpolation");
            sum += weight;
        }
        require(std::abs(sum - 1) < .0001f, "Invalid terrain interpolation sum");
    }
    if (!triangles.empty()) {
        const auto positions = terrain_positions(grid, *this);
        for (const auto &point : positions)
            for (float value : point)
                require(std::isfinite(value) && std::abs(value) < 1e7f,
                        "Terrain point is outside the supported range");
        for (const auto &face : triangles) {
            require(face.cell < textures.size(), "Terrain face material region is missing");
            for (auto i : face.vertices)
                require(i < positions.size(), "Terrain triangle index is invalid");
            const auto a = positions[face.vertices[0]], b = positions[face.vertices[1]],
                       c = positions[face.vertices[2]];
            const float x = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
                        y = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
                        z = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            require(x * x + y * y + z * z > 1e-8f,
                    "Edit collapses a terrain triangle; keep its points apart");
        }
    }
    const auto cells = std::size_t(grid.width) * std::size_t(grid.height);
    require(cells <= 16384,
            "Ground editing currently supports up to 16,384 cells; reduce the width or height");
    require(heights.size() == (std::size_t(grid.width) + 1) * (std::size_t(grid.height) + 1) &&
                textures.size() == cells,
            "Ground sample counts do not match the grid");
    require(offsets.empty() || offsets.size() == heights.size(),
            "Ground vertex offsets do not match the grid");
    require(texture_scales.empty() || texture_scales.size() == cells,
            "Ground texture scales do not match the grid");
    for (float value : uv_origin)
        require(std::isfinite(value) && std::abs(value) < 1e7f, "Invalid ground UV origin");
    for (const auto &scale : texture_scales)
        for (float value : scale)
            require(std::isfinite(value) && value >= .01f && value <= 100,
                    "Texture size must be between 0.01 and 100 tiles");
    if (!offsets.empty())
        for (int z = 0; z <= grid.height; ++z)
            for (int x = 0; x <= grid.width; ++x) {
                const auto point = ground_vertex(grid, *this, x, z);
                for (float value : point)
                    require(std::isfinite(value) && std::abs(value) < 1e7f,
                            "Vertex is outside the supported map range");
            }
    if (triangles.empty() && !offsets.empty())
        for (int z = 0; z < grid.height; ++z)
            for (int x = 0; x < grid.width; ++x) {
                const auto a = ground_vertex(grid, *this, x, z),
                           b = ground_vertex(grid, *this, x, z + 1),
                           c = ground_vertex(grid, *this, x + 1, z + 1),
                           d = ground_vertex(grid, *this, x + 1, z);
                require(ground_area(a, b, d) > grid.tile_size * grid.tile_size * .0001f &&
                            ground_area(d, b, c) > grid.tile_size * grid.tile_size * .0001f,
                        "Move would collapse or fold a ground triangle; keep neighboring vertices "
                        "apart");
            }
    for (float height : heights)
        require(std::isfinite(height) && std::abs(height) < 1e7f,
                "Ground height is outside the supported map range");
    require(blends.empty() || blends.size() == cells, "Ground blend counts do not match the grid");
    for (std::size_t i = 0; i < blends.size(); ++i) {
        const auto &blend = blends[i];
        require(blend.texture.size() <= 4096 && (blend.texture.empty() || !textures[i].empty()),
                "Ground blends need a base texture and a valid source reference");
        for (float weight : blend.weights)
            require(std::isfinite(weight) && weight >= 0 && weight <= 1,
                    "Ground blend weights must be between zero and one");
    }
    for (const auto &texture : textures)
        require(texture.size() <= 4096, "Ground texture reference is too long");
}
GroundSurface transform_ground(const AuthoringGrid &grid, const GroundSurface &ground,
                               AuthoringTile first, AuthoringTile last, float elevation,
                               float x_degrees, float z_degrees) {
    ground.validate(grid);
    grid.center(first);
    grid.center(last);
    require(std::isfinite(elevation) && std::isfinite(x_degrees) && std::isfinite(z_degrees) &&
                std::abs(x_degrees) <= 75 && std::abs(z_degrees) <= 75,
            "Slope angles must be between -75 and 75 degrees and height must be finite");
    auto next = ground;
    const int x0 = std::min(first.x, last.x), x1 = std::max(first.x, last.x) + 1,
              z0 = std::min(first.z, last.z), z1 = std::max(first.z, last.z) + 1;
    float center_x = 0, center_z = 0;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const auto point = ground_vertex(grid, ground, x, z);
            center_x += point[0];
            center_z += point[2];
        }
    center_x /= float((x1 - x0 + 1) * (z1 - z0 + 1));
    center_z /= float((x1 - x0 + 1) * (z1 - z0 + 1));
    const float dx = std::tan(z_degrees * .01745329252f), dz = -std::tan(x_degrees * .01745329252f);
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const auto point = ground_vertex(grid, ground, x, z);
            next.heights[std::size_t(z) * (std::size_t(grid.width) + 1) + x] +=
                elevation + (point[0] - center_x) * dx + (point[2] - center_z) * dz;
        }
    next.validate(grid);
    return next;
}
GroundSurface brush_ground(const AuthoringGrid &grid, const GroundSurface &ground,
                           const GroundBrushStroke &stroke) {
    ground.validate(grid);
    require(std::isfinite(stroke.radius) && stroke.radius > 0 && std::isfinite(stroke.hardness) &&
                stroke.hardness >= 0 && stroke.hardness <= 1,
            "Brush radius must be positive and hardness between zero and one");
    require(std::isfinite(stroke.value) && !stroke.points.empty(),
            "Brush needs a finite value and a stroke path");
    require(stroke.mode >= GroundBrushMode::Raise && stroke.mode <= GroundBrushMode::Blend,
            "Unknown ground brush mode");
    if (stroke.mode == GroundBrushMode::Raise || stroke.mode == GroundBrushMode::Lower)
        require(stroke.value >= 0, "Brush height amount must not be negative");
    if (stroke.mode == GroundBrushMode::Blend)
        require(stroke.value >= 0 && stroke.value <= 1 && !stroke.texture.empty(),
                "Blend brush needs a texture and coverage between zero and one");
    for (const auto &point : stroke.points)
        for (float value : point)
            require(std::isfinite(value) && std::abs(value) < 1e7f,
                    "Brush point is outside the supported map range");
    int x0 = 0, z0 = 0, x1 = grid.width, z1 = grid.height;
    if (stroke.selection) {
        const auto a = stroke.selection->at(0), b = stroke.selection->at(1);
        grid.center(a);
        grid.center(b);
        x0 = std::min(a.x, b.x);
        z0 = std::min(a.z, b.z);
        x1 = std::max(a.x, b.x) + 1;
        z1 = std::max(a.z, b.z) + 1;
    }
    const auto weight = [&](SpatialPoint point) {
        double nearest = INFINITY;
        auto previous = stroke.points.front();
        for (const auto &next : stroke.points) {
            const double dx = double(next[0]) - previous[0], dz = double(next[1]) - previous[1],
                         length = dx * dx + dz * dz;
            const double t = length > 0 ? std::clamp(((double(point[0]) - previous[0]) * dx +
                                                      (double(point[2]) - previous[1]) * dz) /
                                                         length,
                                                     0., 1.)
                                        : 0;
            nearest = std::min(nearest, std::hypot(double(point[0]) - previous[0] - t * dx,
                                                   double(point[2]) - previous[1] - t * dz));
            previous = next;
        }
        const float distance = float(nearest / stroke.radius);
        if (distance >= 1)
            return 0.f;
        if (distance <= stroke.hardness)
            return 1.f;
        const float t = (1 - distance) / (1 - stroke.hardness);
        return t * t * (3 - 2 * t);
    };
    auto next = ground;
    if (stroke.mode <= GroundBrushMode::Level) {
        if (!ground.triangles.empty()) {
            const auto positions = terrain_positions(grid, ground);
            std::map<std::uint32_t, SpatialPoint> targets;
            std::set<std::uint32_t> selected;
            for (const auto &t : ground.triangles)
                if (!stroke.selection || terrain_cell_selected(grid, t.cell, (*stroke.selection)[0],
                                                               (*stroke.selection)[1]))
                    for (auto index : t.vertices)
                        selected.insert(index);
            for (std::uint32_t index = 0; index < positions.size(); ++index) {
                auto point = positions[index];
                const float w = selected.contains(index) ? weight(point) : 0;
                point[1] += w * (stroke.mode == GroundBrushMode::Level   ? stroke.value - point[1]
                                 : stroke.mode == GroundBrushMode::Lower ? -stroke.value
                                                                         : stroke.value);
                targets[index] = point;
            }
            terrain_set_positions(grid, next, targets);
            next.validate(grid);
            return next;
        }
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const auto i = std::size_t(z) * (std::size_t(grid.width) + 1) + x;
                const float w = weight(ground_vertex(grid, ground, x, z));
                next.heights[i] +=
                    w * (stroke.mode == GroundBrushMode::Level   ? stroke.value - ground.heights[i]
                         : stroke.mode == GroundBrushMode::Lower ? -stroke.value
                                                                 : stroke.value);
            }
    } else {
        for (int z = z0; z < z1; ++z)
            for (int x = x0; x < x1; ++x) {
                const auto i = std::size_t(z) * grid.width + x;
                const std::array<SpatialPoint, 4> points{ground_vertex(grid, ground, x, z),
                                                         ground_vertex(grid, ground, x, z + 1),
                                                         ground_vertex(grid, ground, x + 1, z + 1),
                                                         ground_vertex(grid, ground, x + 1, z)};
                if (stroke.mode == GroundBrushMode::Paint) {
                    SpatialPoint center{};
                    for (const auto &point : points)
                        for (unsigned axis = 0; axis < 3; ++axis)
                            center[axis] += point[axis] * .25f;
                    if (weight(center) <= 0)
                        continue;
                    next.textures[i] = stroke.texture;
                    if (!next.blends.empty())
                        next.blends[i] = {};
                } else {
                    std::array<float, 4> weights{};
                    for (unsigned corner = 0; corner < 4; ++corner)
                        weights[corner] = weight(points[corner]);
                    if (*std::max_element(weights.begin(), weights.end()) <= 0)
                        continue;
                    require(!ground.textures[i].empty(),
                            "Paint a base texture before blending this region");
                    if (next.blends.empty())
                        next.blends.resize(next.textures.size());
                    auto &blend = next.blends[i];
                    if (blend.texture != stroke.texture)
                        blend = {stroke.texture, {}};
                    for (unsigned corner = 0; corner < 4; ++corner)
                        blend.weights[corner] +=
                            (stroke.value - blend.weights[corner]) * weights[corner];
                }
            }
    }
    next.validate(grid);
    return next;
}
std::pair<SpatialPoint, float> ground_selection_influence(const AuthoringGrid &grid,
                                                          const GroundSurface &ground,
                                                          AuthoringTile first, AuthoringTile last) {
    grid.center(first);
    grid.center(last);
    const int x0 = std::min(first.x, last.x), x1 = std::max(first.x, last.x) + 1,
              z0 = std::min(first.z, last.z), z1 = std::max(first.z, last.z) + 1;
    SpatialPoint center{};
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const auto point = ground_vertex(grid, ground, x, z);
            for (unsigned axis = 0; axis < 3; ++axis)
                center[axis] += point[axis];
        }
    for (auto &value : center)
        value /= float((x1 - x0 + 1) * (z1 - z0 + 1));
    float radius = 0;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const auto point = ground_vertex(grid, ground, x, z);
            radius = std::max(radius, std::hypot(point[0] - center[0], point[2] - center[2]));
        }
    return {center, radius};
}
GroundSurface influence_ground(const AuthoringGrid &grid, const GroundSurface &ground,
                               AuthoringTile first, AuthoringTile last, float value,
                               float surrounding_cells) {
    require(std::isfinite(surrounding_cells) && surrounding_cells >= 0,
            "Neighbor influence must be finite and nonnegative");
    if (surrounding_cells == 0)
        return transform_ground(grid, ground, first, last, value, 0, 0);
    ground.validate(grid);
    const auto [center, radius] = ground_selection_influence(grid, ground, first, last);
    GroundBrushStroke stroke;
    stroke.mode = value < 0 ? GroundBrushMode::Lower : GroundBrushMode::Raise;
    stroke.value = std::abs(value);
    stroke.radius = radius + surrounding_cells * grid.tile_size;
    stroke.hardness = radius / stroke.radius;
    stroke.points = {{center[0], center[2]}};
    return brush_ground(grid, ground, stroke);
}
std::vector<GroundTexture> load_ground_textures(const std::filesystem::path &dump,
                                                const Environment &scene,
                                                std::atomic_bool *cancel) {
    std::set<std::string> names;
    for (const auto &draw : scene.draws)
        if (!draw.character && draw.sky_part < 0 && !draw.weather_mask) {
            const auto &material = scene.materials.at(draw.material);
            if (!material.texture.empty())
                names.insert(material.texture);
            for (const auto &texture : material.texture_inputs)
                if (!texture.empty())
                    names.insert(texture);
        }
    std::map<std::pair<std::filesystem::path, std::size_t>, std::string> hashes;
    std::vector<GroundTexture> result;
    std::set<std::string> keys;
    for (const auto &name : names) {
        require(!cancel || !cancel->load(), "Ground texture loading cancelled");
        auto found = scene.texture_sources.find(name);
        if (found == scene.texture_sources.end() || !scene.textures.contains(name))
            continue;
        const auto &source = found->second;
        auto [hash, fresh] = hashes.try_emplace({source.archive, source.member});
        if (fresh)
            hash->second = sha256(Archive(dump / source.archive).decoded(source.member));
        std::ostringstream key;
        key.imbue(std::locale::classic());
        key << source.archive.generic_string() << ' ' << source.member;
        for (auto part : source.path)
            key << '/' << part;
        key << ' ' << hash->second;
        if (keys.insert(key.str()).second)
            result.push_back({key.str(), name.substr(name.find_last_of('/') + 1), name});
    }
    std::stable_sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
        const auto &x = scene.textures.at(a.texture);
        const auto &y = scene.textures.at(b.texture);
        const auto ax = std::uint64_t(x.width) * x.height, ay = std::uint64_t(y.width) * y.height;
        return ax != ay ? ax > ay : a.name < b.name;
    });
    return result;
}
Environment ground_preview(const Environment &source, const AuthoringGrid &grid,
                           const GroundSurface &ground, const std::vector<GroundTexture> &palette) {
    ground.validate(grid);
    Environment result = source;
    result.draws.clear();
    result.materials.clear();
    result.material_animations.clear();
    result.visibility_animations.clear();
    result.spatial.regions.clear();
    result.static_placements = 0;
    result.character_placements = 0;
    result.terrain_blocks = 1;
    result.low = {grid.origin[0], INFINITY, grid.origin[1]};
    result.high = {grid.origin[0] + grid.width * grid.tile_size, -INFINITY,
                   grid.origin[1] + grid.height * grid.tile_size};
    SpatialRegion region;
    region.kind = SpatialKind::Ground;
    const auto stride = std::size_t(grid.width) + 1;
    std::vector<SceneVertex> vertices;
    vertices.reserve(ground.heights.size());
    result.low = {INFINITY, INFINITY, INFINITY};
    result.high = {-INFINITY, -INFINITY, -INFINITY};
    const auto mesh = terrain_mesh(grid, ground);
    const auto positions = terrain_positions(grid, ground);
    const auto coordinates = terrain_coordinates(grid, ground);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto point = positions[i];
        SceneVertex vertex{};
        vertex.x = point[0];
        vertex.y = point[1];
        vertex.z = point[2];
        vertex.u = coordinates[i][0] + ground.uv_origin[0];
        vertex.v = coordinates[i][1] + ground.uv_origin[1];
        vertex.tx = 1;
        vertices.push_back(vertex);
        region.vertices.push_back({point});
        for (unsigned axis = 0; axis < 3; ++axis) {
            result.low[axis] = std::min(result.low[axis], point[axis]);
            result.high[axis] = std::max(result.high[axis], point[axis]);
        }
    }
    for (const auto &face : mesh.triangles) {
        const auto triangle = face.vertices;
        const auto &p = vertices[triangle[0]];
        const auto &q = vertices[triangle[1]];
        const auto &r = vertices[triangle[2]];
        const SpatialPoint u{q.x - p.x, q.y - p.y, q.z - p.z}, v{r.x - p.x, r.y - p.y, r.z - p.z};
        const SpatialPoint normal{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                  u[0] * v[1] - u[1] * v[0]};
        for (auto index : triangle) {
            vertices[index].nx += normal[0];
            vertices[index].ny += normal[1];
            vertices[index].nz += normal[2];
        }
    }
    for (auto &vertex : vertices) {
        const float n =
            std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny + vertex.nz * vertex.nz);
        if (n > 1e-8f) {
            vertex.nx /= n;
            vertex.ny /= n;
            vertex.nz /= n;
        } else
            vertex.ny = 1;
    }
    struct Batch {
        std::size_t draw;
        std::map<std::pair<std::size_t, unsigned>, std::uint16_t> vertices;
    };
    std::map<std::tuple<std::string, std::string, float, float>, Batch> batches;
    std::map<std::pair<std::size_t, std::size_t>, float> coverage;
    for (const auto &face : mesh.triangles) {
        const auto cell = std::size_t(face.cell);
        const int x = int(cell) % grid.width, z = int(cell) / grid.width;
        const auto &key = ground.textures[cell];
        const GroundBlend blend = ground.blends.empty() ? GroundBlend{} : ground.blends[cell];
        const auto scale = ground.texture_scales.empty() ? std::array<float, 2>{1, 1}
                                                         : ground.texture_scales[cell];
        const auto batch_key = std::tuple{key, blend.texture, scale[0], scale[1]};
        auto found = batches.find(batch_key);
        if (found == batches.end()) {
            SceneMaterial material;
            material.name = "Authored ground";
            material.cull = 2;
            if (!key.empty()) {
                auto texture = std::find_if(palette.begin(), palette.end(), [&](const auto &item) {
                    return item.key == key;
                });
                require(
                    texture != palette.end() && source.textures.contains(texture->texture),
                    "A painted ground texture is missing or changed; open with the matching dump");
                material.texture = texture->texture;
                material.texture_inputs[0] = texture->texture;
                material.texture_count = 1;
                material.name = texture->name;
                material.inputs[0].wrap_u = material.inputs[0].wrap_v = 2;
            } else
                material.diffuse = {.65f, .7f, .65f, 1};
            material.combiner.present = true;
            material.combiner.lighting = true;
            for (auto &stage : material.combiner.stages) {
                stage.color_sources = {15, 15, 15, 0};
                stage.alpha_sources = {15, 15, 15, 0};
            }
            auto &base_stage = material.combiner.stages[0];
            base_stage.color_sources[0] = key.empty() ? 0.f : 3.f;
            base_stage.alpha_sources[0] = 14;
            base_stage.constant = {1, 1, 1, 1};
            auto &lighting = material.combiner.stages[1];
            lighting.color_sources = {15, 1, 0, 0};
            lighting.operation[0] = 1;
            if (!blend.texture.empty()) {
                auto overlay = std::find_if(palette.begin(), palette.end(), [&](const auto &item) {
                    return item.key == blend.texture;
                });
                require(
                    overlay != palette.end() && source.textures.contains(overlay->texture),
                    "A blended ground texture is missing or changed; open with the matching dump");
                material.texture_inputs[1] = overlay->texture;
                material.texture_count = 2;
                material.inputs[1].wrap_u = material.inputs[1].wrap_v = 2;
                material.name += " + " + overlay->name;
                auto &mix = material.combiner.stages[0];
                mix.color_sources = {4, 3, 0, 0};
                mix.color_operands = {0, 0, 2, 0};
                mix.operation[0] = 4;
                mix.alpha_sources = {14, 14, 14, 0};
                mix.constant = {1, 1, 1, 1};
            }
            SceneDraw draw;
            draw.name = "Authored ground / " + material.name;
            draw.mesh = draw.name;
            draw.material = result.materials.size();
            result.materials.push_back(std::move(material));
            result.draws.push_back(std::move(draw));
            found = batches.emplace(batch_key, Batch{result.draws.size() - 1, {}}).first;
        }
        auto &batch = found->second;
        auto &draw = result.draws[batch.draw];
        const auto a = std::size_t(z) * stride + x;
        const auto weight = [&](auto &&self, std::size_t index) -> float {
            auto id = std::pair{cell, index};
            auto found = coverage.find(id);
            if (found != coverage.end())
                return found->second;
            float value = 0;
            if (index < ground.heights.size()) {
                const unsigned corner = index == a                ? 0
                                        : index == a + stride     ? 1
                                        : index == a + stride + 1 ? 2
                                                                  : 3;
                value = blend.weights[corner];
            } else {
                const auto &point = ground.points[index - ground.heights.size()];
                for (unsigned i = 0; i < 3; ++i)
                    if (point.weights[i] != 0)
                        value += self(self, point.parents[i]) * point.weights[i];
            }
            coverage[id] = value;
            return value;
        };
        for (const auto index : face.vertices) {
            const unsigned alpha =
                blend.texture.empty()
                    ? 255
                    : unsigned(std::lround(std::clamp(weight(weight, index), 0.f, 1.f) * 255));
            auto [local, fresh] = batch.vertices.try_emplace(std::pair{index, alpha},
                                                             std::uint16_t(draw.vertices.size()));
            if (fresh) {
                require(draw.vertices.size() < 65536,
                        "Ground material exceeds the supported index range");
                auto vertex = vertices[index];
                vertex.u /= scale[0];
                vertex.v /= scale[1];
                vertex.color = (alpha << 24) | 0xffffffu;
                if (key.empty())
                    vertex.color = 0xffa6b3a6;
                draw.vertices.push_back(vertex);
            }
            draw.indices.push_back(local->second);
            region.triangles.push_back(std::uint32_t(index));
        }
    }
    std::set<std::string> used;
    for (const auto &material : result.materials)
        for (const auto &texture : material.texture_inputs)
            if (!texture.empty())
                used.insert(texture);
    for (auto it = result.textures.begin(); it != result.textures.end();)
        if (!used.contains(it->first))
            it = result.textures.erase(it);
        else
            ++it;
    result.spatial.regions.push_back(std::move(region));
    result.diagnostics.push_back(
        "Authored ground supports picking and grounding. Enable ground patch staging in Authoring "
        "Review to compile supported terrain and matching game collision.");
    return result;
}
}
