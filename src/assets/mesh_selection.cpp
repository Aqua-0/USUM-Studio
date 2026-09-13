#include "assets/mesh_selection.h"
#include "assets/mesh_geometry.h"
#include <algorithm>
#include <cmath>
#include <map>
namespace studio {
namespace {
SkinMesh gather(const SkinnedModel &model, const MeshVertexSelection &selected) {
    SkinMesh group;
    for (auto &[m, vertices] : selected)
        for (auto i : vertices)
            group.vertices.push_back(model.meshes.at(m).vertices.at(i));
    return group;
}
void scatter(SkinnedModel &model, const MeshVertexSelection &selected, const SkinMesh &group) {
    std::size_t index = 0;
    for (auto &[m, vertices] : selected)
        for (auto i : vertices)
            model.meshes.at(m).vertices.at(i) = group.vertices.at(index++);
}
std::set<std::size_t> all_vertices(const SkinMesh &mesh) {
    std::set<std::size_t> all;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
        all.insert(i);
    return all;
}
}
void transform_mesh_selection(SkinnedModel &model, const MeshVertexSelection &selected,
                              std::array<float, 3> translation, std::array<float, 3> rotation,
                              std::array<float, 3> scale) {
    auto group = gather(model, selected);
    transform_vertices(group, all_vertices(group), translation, rotation, scale);
    scatter(model, selected, group);
}
void flatten_mesh_selection(SkinnedModel &model, const MeshVertexSelection &selected,
                            unsigned axis) {
    auto group = gather(model, selected);
    flatten_mesh_selection(group, all_vertices(group), axis);
    scatter(model, selected, group);
}
namespace {
std::array<float, 3> selection_center(const SkinnedModel &model,
                                      const MeshVertexSelection &selected) {
    std::array<double, 3> sum{};
    std::size_t count = 0;
    for (auto &[m, vertices] : selected)
        for (auto i : vertices) {
            auto &v = model.meshes.at(m).vertices.at(i);
            for (unsigned k = 0; k < 3; ++k)
                sum[k] += v.position[k];
            ++count;
        }
    require(count > 0, "Select geometry first");
    return {float(sum[0] / count), float(sum[1] / count), float(sum[2] / count)};
}
}
MeshInfluences mesh_selection_influences(const SkinnedModel &model,
                                         const MeshVertexSelection &selected,
                                         const std::set<std::size_t> &enabled, float radius) {
    require(std::isfinite(radius) && radius >= 0,
            "Influence radius must be finite and nonnegative");
    std::vector<std::array<float, 3>> seeds;
    for (auto &[m, vertices] : selected)
        if (enabled.contains(m))
            for (auto i : vertices)
                seeds.push_back(model.meshes.at(m).vertices.at(i).position);
    MeshInfluences out;
    for (auto m : enabled) {
        auto &mesh = model.meshes.at(m);
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
            float weight = 0;
            if (selected.contains(m) && selected.at(m).contains(i))
                weight = 1;
            else if (radius > 0)
                for (auto p : seeds) {
                    auto v = mesh.vertices[i].position;
                    float distance = std::hypot(v[0] - p[0], v[1] - p[1], v[2] - p[2]);
                    float t = std::clamp(1 - distance / radius, 0.f, 1.f);
                    weight = std::max(weight, t * t * (3 - 2 * t));
                }
            if (weight > 0)
                out[m][i] = weight;
        }
    }
    return out;
}
void transform_mesh_influences(SkinnedModel &model, const MeshVertexSelection &selected,
                               const MeshInfluences &influences, std::array<float, 3> translation,
                               std::array<float, 3> rotation, std::array<float, 3> scale) {
    auto pivot = selection_center(model, selected);
    MeshVertexSelection affected;
    for (auto &[m, vertices] : influences)
        for (auto [i, w] : vertices) {
            require(std::isfinite(w) && w > 0 && w <= 1, "Invalid geometry influence");
            affected[m].insert(i);
        }
    auto group = gather(model, affected);
    auto original = group;
    transform_vertices(group, all_vertices(group), translation, rotation, scale, pivot);
    std::size_t index = 0;
    for (auto &[m, vertices] : influences)
        for (auto [i, w] : vertices) {
            auto &a = original.vertices[index];
            auto &b = group.vertices[index++];
            for (unsigned k = 0; k < 3; ++k) {
                b.position[k] = a.position[k] + (b.position[k] - a.position[k]) * w;
                b.normal[k] = a.normal[k] + (b.normal[k] - a.normal[k]) * w;
                b.tangent[k] = a.tangent[k] + (b.tangent[k] - a.tangent[k]) * w;
            }
            auto normalize = [](auto &value, const auto &source) {
                float length = std::hypot(value[0], value[1], value[2]),
                      old = std::hypot(source[0], source[1], source[2]);
                if (length > 1e-8f)
                    for (auto &x : value)
                        x *= old / length;
            };
            normalize(b.normal, a.normal);
            normalize(b.tangent, a.tangent);
        }
    scatter(model, affected, group);
}
void flatten_mesh_influences(SkinnedModel &model, const MeshVertexSelection &selected,
                             const MeshInfluences &influences, unsigned axis) {
    require(axis < 3, "Invalid flatten axis");
    auto center = selection_center(model, selected);
    auto next = model;
    for (auto &[m, vertices] : influences)
        for (auto [i, w] : vertices) {
            require(std::isfinite(w) && w > 0 && w <= 1, "Invalid geometry influence");
            auto &value = next.meshes.at(m).vertices.at(i).position[axis];
            value += (center[axis] - value) * w;
        }
    model = std::move(next);
}
SceneSkeleton bone_test_skeleton(const SceneSkeleton &source, unsigned bone, unsigned axis,
                                 float degrees) {
    require(bone < source.joints.size() && axis < 3 && std::isfinite(degrees),
            "Invalid bone test pose");
    auto result = source;
    result.motion = {};
    result.tracks.clear();
    result.overlays.clear();
    result.joints[bone].rotation[axis] += degrees * .01745329252f;
    return result;
}
std::vector<MeshEdge> mesh_edges(const SkinMesh &mesh) {
    std::set<MeshEdge> unique;
    for (unsigned t = 0; t < mesh.indices.size(); t += 3)
        for (unsigned k = 0; k < 3; ++k) {
            auto a = std::size_t(mesh.indices[t + k]),
                 b = std::size_t(mesh.indices[t + (k + 1) % 3]);
            if (a != b)
                unique.insert({std::min(a, b), std::max(a, b)});
        }
    return {unique.begin(), unique.end()};
}
std::set<std::size_t> mesh_selection_vertices(const SkinMesh &mesh, unsigned mode,
                                              const std::set<std::size_t> &elements, bool seams) {
    require(mode < 3, "Unknown mesh selection mode");
    std::set<std::size_t> out;
    auto edges = mode == 1 ? mesh_edges(mesh) : std::vector<MeshEdge>{};
    for (auto i : elements) {
        if (mode == 0) {
            require(i < mesh.vertices.size(), "Vertex selection is out of range");
            out.insert(i);
        } else if (mode == 1) {
            auto edge = edges.at(i);
            out.insert(edge.begin(), edge.end());
        } else {
            require(i < mesh.indices.size() / 3, "Face selection is out of range");
            for (unsigned k = 0; k < 3; ++k)
                out.insert(mesh.indices[i * 3 + k]);
        }
    }
    if (seams) {
        std::set<std::array<float, 3>> positions;
        for (auto i : out)
            positions.insert(mesh.vertices[i].position);
        for (unsigned i = 0; i < mesh.vertices.size(); ++i)
            if (positions.contains(mesh.vertices[i].position))
                out.insert(i);
    }
    return out;
}
void flatten_mesh_selection(SkinMesh &mesh, const std::set<std::size_t> &vertices, unsigned axis) {
    require(axis < 3 && !vertices.empty(), "Choose an axis and select geometry to flatten");
    double center = 0;
    for (auto i : vertices)
        center += mesh.vertices.at(i).position[axis];
    center /= double(vertices.size());
    for (auto i : vertices)
        mesh.vertices.at(i).position[axis] = float(center);
}
void paint_vertex_weight(SkinVertex &vertex, unsigned influences, unsigned bone, float target,
                         float amount, int fallback) {
    require(influences >= 2 && influences <= 4 && vertex.index_offset && vertex.weight_offset,
            "Weight painting requires a mesh with per-vertex blended weights; use whole-mesh "
            "attachment for shared or rigid skinning");
    require(bone < 255 && std::isfinite(target) && target >= 0 && target <= 1 &&
                std::isfinite(amount) && amount >= 0 && amount <= 1,
            "Invalid weight brush parameters");
    require(fallback == -1 || (fallback >= 0 && fallback < 255 && unsigned(fallback) != bone),
            "Choose a different bone to receive removed weight");
    if (amount == 0)
        return;
    float current = 0;
    std::map<unsigned, float> others;
    for (unsigned k = 0; k < 4; ++k)
        if (vertex.weights[k] > 0) {
            if (vertex.joints[k] == bone)
                current += vertex.weights[k];
            else
                others[vertex.joints[k]] += vertex.weights[k];
        }
    current = std::clamp(current, 0.f, 1.f);
    float weight = std::clamp(current + (target - current) * amount, 0.f, 1.f);
    if (weight == current)
        return;
    if (weight < 1 && others.empty() && fallback >= 0)
        others[unsigned(fallback)] = 1;
    std::vector<std::pair<unsigned, float>> ordered(others.begin(), others.end());
    std::sort(ordered.begin(), ordered.end(), [](auto a, auto b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    unsigned capacity = std::min({influences, vertex.index_elements, vertex.weight_elements});
    require(capacity >= 2, "This vertex cannot store blended weights");
    if (ordered.size() > capacity - (weight > 0 ? 1 : 0))
        ordered.resize(capacity - (weight > 0 ? 1 : 0));
    float sum = 0;
    for (auto [id, w] : ordered)
        sum += w;
    if (weight < 1 && sum == 0)
        return;
    SkinVertex next = vertex;
    next.joints = {};
    next.weights = {};
    unsigned k = 0;
    if (weight > 0) {
        next.joints[k] = std::uint16_t(bone);
        next.weights[k++] = weight;
    }
    if (weight < 1)
        for (auto [id, w] : ordered) {
            next.joints[k] = std::uint16_t(id);
            next.weights[k++] = w * (1 - weight) / sum;
        }
    vertex = next;
}
void MeshSurface::build(const SkinnedModel &model, const std::vector<bool> &shown,
                        const float *view, const float *projection, unsigned w, unsigned h,
                        const std::vector<Matrix> &poses, const std::vector<unsigned> &culls) {
    require(w > 0 && h > 0 && w <= 2048 && h <= 2048, "Invalid geometry overlay size");
    width = w;
    height = h;
    pixels.assign(std::size_t(w) * h, {});
    points.clear();
    points.resize(model.meshes.size());
    for (unsigned m = 0; m < model.meshes.size(); ++m) {
        auto &mesh = model.meshes[m];
        auto &projected = points[m];
        projected.resize(mesh.vertices.size());
        if (m >= shown.size() || !shown[m])
            continue;
        for (unsigned i = 0; i < mesh.vertices.size(); ++i) {
            auto &v = mesh.vertices[i];
            auto p = v.position;
            if (!poses.empty()) {
                p = {};
                for (unsigned k = 0; k < 4; ++k)
                    if (v.weights[k] > 0) {
                        auto &matrix = poses.at(v.joints[k]);
                        for (unsigned r = 0; r < 3; ++r)
                            p[r] +=
                                v.weights[k] *
                                (matrix[r * 4] * v.position[0] + matrix[r * 4 + 1] * v.position[1] +
                                 matrix[r * 4 + 2] * v.position[2] + matrix[r * 4 + 3]);
                    }
            }
            float a[4]{}, b[4]{};
            for (unsigned r = 0; r < 4; ++r)
                a[r] = view[r] * p[0] + view[4 + r] * p[1] + view[8 + r] * p[2] + view[12 + r];
            for (unsigned r = 0; r < 4; ++r)
                for (unsigned c = 0; c < 4; ++c)
                    b[r] += projection[c * 4 + r] * a[c];
            if (b[3] > .001f)
                projected[i] = {(b[0] / b[3] + 1) * .5f, (1 - b[1] / b[3]) * .5f, b[2] / b[3],
                                1 / b[3]};
        }
        for (unsigned f = 0; f < mesh.indices.size() / 3; ++f) {
            auto a = projected[mesh.indices[f * 3]], b = projected[mesh.indices[f * 3 + 1]],
                 c = projected[mesh.indices[f * 3 + 2]];
            if (!a.inverse_w || !b.inverse_w || !c.inverse_w)
                continue;
            float denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
            if (std::abs(denominator) < 1e-12f ||
                (m < culls.size() &&
                 ((culls[m] == 1 && denominator < 0) || (culls[m] == 2 && denominator > 0))))
                continue;
            int low_x =
                    int(std::clamp(std::floor(std::min({a.x, b.x, c.x}) * w), 0.f, float(w - 1))),
                high_x =
                    int(std::clamp(std::ceil(std::max({a.x, b.x, c.x}) * w), 0.f, float(w - 1))),
                low_y =
                    int(std::clamp(std::floor(std::min({a.y, b.y, c.y}) * h), 0.f, float(h - 1))),
                high_y =
                    int(std::clamp(std::ceil(std::max({a.y, b.y, c.y}) * h), 0.f, float(h - 1)));
            for (int y = low_y; y <= high_y; ++y)
                for (int x = low_x; x <= high_x; ++x) {
                    float px = (x + .5f) / w, py = (y + .5f) / h,
                          u = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / denominator,
                          v = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / denominator,
                          t = 1 - u - v;
                    if (u < 0 || v < 0 || t < 0)
                        continue;
                    float depth = u * a.z + v * b.z + t * c.z;
                    auto &pixel = pixels[std::size_t(y) * w + x];
                    if (depth >= pixel.depth)
                        continue;
                    float sum = u * a.inverse_w + v * b.inverse_w + t * c.inverse_w;
                    pixel = {depth,
                             int(m),
                             int(f),
                             {u * a.inverse_w / sum, v * b.inverse_w / sum, t * c.inverse_w / sum},
                             ((b.y - c.y) * (a.z - c.z) + (c.y - a.y) * (b.z - c.z)) / denominator,
                             ((c.x - b.x) * (a.z - c.z) + (a.x - c.x) * (b.z - c.z)) / denominator};
                }
        }
    }
}
const MeshSurfacePixel *MeshSurface::at(float x, float y) const {
    if (x < 0 || y < 0 || x >= 1 || y >= 1 || !width || !height)
        return nullptr;
    return &pixels[std::size_t(y * height) * width + unsigned(x * width)];
}
bool MeshSurface::visible(const MeshProjection &point) const {
    if (point.inverse_w <= 0 || point.x < 0 || point.y < 0 || point.x >= 1 || point.y >= 1 ||
        !width || !height)
        return false;
    int x = int(point.x * width), y = int(point.y * height);
    float depth = INFINITY;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            int px = x + dx, py = y + dy;
            if (px >= 0 && py >= 0 && px < int(width) && py < int(height)) {
                auto &pixel = pixels[std::size_t(py) * width + px];
                if (pixel.mesh >= 0)
                    depth = std::min(depth, pixel.depth +
                                                pixel.depth_dx * (point.x - (px + .5f) / width) +
                                                pixel.depth_dy * (point.y - (py + .5f) / height));
            }
        }
    return std::isfinite(depth) && point.z <= depth + .00008f;
}
}
