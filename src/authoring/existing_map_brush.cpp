#include "authoring/existing_map_brush.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace studio {
namespace {
using Point = std::array<float, 3>;
Point sub(Point a, Point b) {
    for (unsigned i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
float dot(Point a, Point b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point cross(Point a, Point b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float distance(Point a, Point b) {
    auto d = sub(a, b);
    return std::sqrt(dot(d, d));
}
}
std::optional<Point> pick_map_surface(const Environment &scene, Point eye, Point direction,
                                      float ceiling) {
    float nearest = std::numeric_limits<float>::max();
    std::optional<Point> result;
    for (const auto &draw : scene.draws) {
        if (draw.character || draw.player >= 0 || draw.sky_part >= 0 || draw.projected_shadow)
            continue;
        for (std::size_t f = 0; f + 2 < draw.indices.size(); f += 3) {
            auto position = [&](unsigned corner) {
                const auto &v = draw.vertices.at(draw.indices[f + corner]);
                Point p{v.x, v.y, v.z};
                if (draw.placement >= 0) {
                    const auto &m = scene.placement_transforms.at(draw.placement);
                    auto local = p;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        p[axis] = m[axis * 4] * local[0] + m[axis * 4 + 1] * local[1] +
                                  m[axis * 4 + 2] * local[2] + m[axis * 4 + 3];
                }
                return p;
            };
            auto a = position(0), e1 = sub(position(1), a), e2 = sub(position(2), a);
            auto p = cross(direction, e2);
            auto determinant = dot(e1, p);
            if (std::abs(determinant) < 1e-8f)
                continue;
            auto t = sub(eye, a), q = cross(t, e1);
            auto u = dot(t, p) / determinant, v = dot(direction, q) / determinant,
                 depth = dot(e2, q) / determinant;
            if (u < 0 || v < 0 || u + v > 1 || depth < 0 || depth >= nearest)
                continue;
            auto hit = eye;
            for (unsigned axis = 0; axis < 3; ++axis)
                hit[axis] += direction[axis] * depth;
            if (hit[1] > ceiling)
                continue;
            result = hit;
            nearest = depth;
        }
    }
    return result;
}
std::optional<ExistingBrushHit> pick_existing_mesh(const SkinnedModel &model,
                                                   const std::set<std::size_t> &meshes, Point eye,
                                                   Point direction) {
    float nearest = std::numeric_limits<float>::max();
    std::optional<ExistingBrushHit> result;
    for (auto m : meshes) {
        const auto &mesh = model.meshes.at(m);
        for (std::size_t f = 0; f < mesh.indices.size() / 3; ++f) {
            auto a = mesh.vertices.at(mesh.indices[f * 3]).position;
            auto e1 = sub(mesh.vertices.at(mesh.indices[f * 3 + 1]).position, a);
            auto e2 = sub(mesh.vertices.at(mesh.indices[f * 3 + 2]).position, a);
            auto p = cross(direction, e2);
            auto determinant = dot(e1, p);
            if (std::abs(determinant) < 1e-8f)
                continue;
            auto t = sub(eye, a);
            auto u = dot(t, p) / determinant;
            auto q = cross(t, e1);
            auto v = dot(direction, q) / determinant;
            auto depth = dot(e2, q) / determinant;
            if (u < 0 || v < 0 || u + v > 1 || depth < 0 || depth >= nearest)
                continue;
            nearest = depth;
            auto point = eye;
            for (unsigned i = 0; i < 3; ++i)
                point[i] += direction[i] * depth;
            result = ExistingBrushHit{point, m, f};
        }
    }
    return result;
}
bool sculpt_existing_mesh(SkinnedModel &model, const std::set<std::size_t> &meshes, Point center,
                          float radius, float amount, ExistingBrush tool, float level) {
    require(std::isfinite(radius) && radius > 0 && std::isfinite(amount) && amount >= 0 &&
                std::isfinite(level),
            "Invalid terrain brush settings");
    std::map<Point, std::pair<double, unsigned>> neighbors;
    if (tool == ExistingBrush::Smooth)
        for (auto m : meshes) {
            const auto &mesh = model.meshes.at(m);
            for (std::size_t f = 0; f < mesh.indices.size(); f += 3)
                for (unsigned i = 0; i < 3; ++i) {
                    auto &entry = neighbors[mesh.vertices.at(mesh.indices[f + i]).position];
                    for (unsigned j = 0; j < 3; ++j)
                        if (j != i) {
                            entry.first += mesh.vertices.at(mesh.indices[f + j]).position[1];
                            ++entry.second;
                        }
                }
        }
    bool changed = false;
    for (auto m : meshes)
        for (auto &vertex : model.meshes.at(m).vertices) {
            auto d = distance(vertex.position, center);
            if (d >= radius)
                continue;
            auto falloff = 1 - d / radius;
            falloff *= falloff;
            float delta = 0;
            if (tool == ExistingBrush::Raise || tool == ExistingBrush::Lower)
                delta = amount * falloff * (tool == ExistingBrush::Lower ? -1.f : 1.f);
            else {
                auto target = level;
                if (tool == ExistingBrush::Smooth) {
                    auto found = neighbors.find(vertex.position);
                    if (found == neighbors.end() || !found->second.second)
                        continue;
                    target = float(found->second.first / found->second.second);
                }
                delta = (target - vertex.position[1]) * std::clamp(amount * falloff, 0.f, 1.f);
            }
            require(std::isfinite(vertex.position[1] + delta) &&
                        std::abs(vertex.position[1] + delta) < 1e7f,
                    "Brush moved terrain outside the supported range");
            vertex.position[1] += delta;
            changed |= delta != 0;
        }
    return changed;
}
MaterialFaces existing_brush_faces(const SkinnedModel &model, const std::set<std::size_t> &meshes,
                                   const ExistingBrushHit &hit, float radius) {
    MaterialFaces result;
    for (auto m : meshes) {
        const auto &mesh = model.meshes.at(m);
        for (std::size_t f = 0; f < mesh.indices.size() / 3; ++f) {
            Point center{};
            bool inside = false;
            for (unsigned j = 0; j < 3; ++j) {
                auto p = mesh.vertices.at(mesh.indices[f * 3 + j]).position;
                inside |= distance(p, hit.point) <= radius;
                for (unsigned k = 0; k < 3; ++k)
                    center[k] += p[k] / 3;
            }
            if (inside || distance(center, hit.point) <= radius)
                result[m].insert(f);
        }
    }
    if (meshes.contains(hit.mesh))
        result[hit.mesh].insert(hit.face);
    return result;
}
FaceMaterialEdit paint_existing_faces(MaterialDocument &document, const MaterialFaces &faces,
                                      std::size_t material, float tile_size, float rotation) {
    require(std::isfinite(tile_size) && tile_size >= 0 && std::isfinite(rotation),
            "Invalid tile mapping");
    auto edited = document;
    auto assigned = edited.assign_material_faces(faces, material);
    if (tile_size > 0) {
        auto exchange = edited.model_exchange();
        auto uv = edited.model.scene->materials.at(material).inputs[0].source;
        require(uv >= 0 && uv < 3, "Choose a material using mesh UV coordinates for tile mapping");
        for (auto &[draw, selected] : assigned.selected) {
            auto native =
                edited.model.native_meshes.empty() ? draw : edited.model.native_meshes.at(draw);
            auto &mesh = exchange.meshes.at(native);
            for (auto face : selected)
                for (unsigned corner = 0; corner < 3; ++corner) {
                    auto offset = face * 3 + corner;
                    auto vertex = mesh.vertices.at(mesh.indices.at(offset));
                    auto x = vertex.channels[0][0] / tile_size,
                         z = vertex.channels[0][2] / tile_size;
                    vertex.channels[4 + uv][0] = std::cos(rotation) * x - std::sin(rotation) * z;
                    vertex.channels[4 + uv][1] = std::sin(rotation) * x + std::cos(rotation) * z;
                    if (vertex.channels[4 + uv] ==
                        mesh.vertices.at(mesh.indices[offset]).channels[4 + uv])
                        continue;
                    require(mesh.vertices.size() < 65535,
                            "Tile painting exceeds this mesh's vertex limit; paint fewer faces");
                    mesh.indices[offset] = std::uint16_t(mesh.vertices.size());
                    mesh.vertices.push_back(vertex);
                }
            auto vertices = mesh.vertices;
            std::map<std::uint16_t, std::uint16_t> used;
            mesh.vertices.clear();
            for (auto &index : mesh.indices) {
                auto [it, inserted] = used.emplace(index, std::uint16_t(mesh.vertices.size()));
                if (inserted)
                    mesh.vertices.push_back(vertices.at(index));
                index = it->second;
            }
        }
        edited.import_model_exchange(exchange);
    }
    document.import_native_members(edited.compiled_members());
    return assigned;
}
}
