#include "assets/mesh_geometry.h"
#include <algorithm>
#include <cmath>
#include <map>
namespace studio {
namespace {
using Point = std::array<float, 3>;
void finite(Point p) {
    for (auto x : p)
        require(std::isfinite(x) && std::abs(x) <= 1e7f,
                "Geometry must contain finite coordinates within the renderer's supported range");
}
void fixed_attribute(Bytes &out, const std::array<std::size_t, 3> &offsets,
                     std::array<unsigned, 4> values) {
    for (auto offset : offsets)
        require(offset > 0, "This shared skin attribute cannot be rewritten");
    std::array<std::uint32_t, 4> packed{};
    for (unsigned i = 0; i < 4; ++i)
        if (values[i]) {
            int exponent = 0;
            float f = std::frexp(float(values[i]), &exponent);
            packed[i] = (std::uint32_t(exponent + 62) << 16) | std::uint32_t((f * 2 - 1) * 65536);
        }
    put32(out, offsets[0], (packed[3] << 8) | (packed[2] >> 16));
    put32(out, offsets[1], (packed[2] << 16) | (packed[1] >> 8));
    put32(out, offsets[2], (packed[1] << 24) | packed[0]);
}
std::array<unsigned, 4> quantize(const SkinVertex &v, unsigned joint_count) {
    float total = 0;
    std::array<unsigned, 4> out{};
    std::array<float, 4> fraction{};
    unsigned used = 0;
    for (unsigned k = 0; k < 4; ++k) {
        float w = v.weights[k];
        require(std::isfinite(w) && w >= 0 && w <= 1, "Bone weights must be between zero and one");
        if (w > 0)
            require(v.joints[k] < joint_count, "Skin weights reference a missing bone");
        total += w;
        float raw = w * 255;
        out[k] = unsigned(std::floor(raw));
        fraction[k] = raw - float(out[k]);
        used += out[k];
    }
    require(std::abs(total - 1) < 1e-5f, "Vertex bone weights must sum to one");
    require(used <= 255, "Invalid normalized bone weights");
    while (used < 255) {
        auto i = std::size_t(std::max_element(fraction.begin(), fraction.end()) - fraction.begin());
        ++out[i];
        fraction[i] = -1;
        ++used;
    }
    return out;
}
}
Bytes replace_mesh_geometry(View original, const SkinnedModel &replacement) {
    auto source = SkinnedModel::parse(original);
    require(source.meshes.size() == replacement.meshes.size() &&
                source.joints.size() == replacement.joints.size(),
            "Geometry editing preserves the existing mesh and skeleton structure");
    bool reordered = false;
    for (std::size_t i = 0; i < source.meshes.size(); ++i)
        reordered |= source.meshes[i].palette_offset != replacement.meshes[i].palette_offset;
    if (reordered) {
        auto native = replacement;
        std::set<std::size_t> used;
        for (std::size_t i = 0; i < source.meshes.size(); ++i) {
            auto found =
                std::find_if(replacement.meshes.begin(), replacement.meshes.end(), [&](auto &mesh) {
                    return mesh.palette_offset == source.meshes[i].palette_offset;
                });
            require(found != replacement.meshes.end() && used.insert(found->palette_offset).second,
                    "Native mesh mapping is incomplete");
            native.meshes[i] = *found;
        }
        return replace_mesh_geometry(original, native);
    }
    for (unsigned i = 0; i < source.joints.size(); ++i) {
        auto &a = source.joints[i];
        auto &b = replacement.joints[i];
        require(a.name == b.name && a.parent == b.parent && a.scale == b.scale &&
                    a.rotation == b.rotation && a.translation == b.translation,
                "Geometry editing does not change the skeleton bind pose");
    }
    Bytes out(original.begin(), original.end());
    bool moved = false;
    std::map<std::size_t, std::pair<Point, Point>> bounds;
    for (unsigned m = 0; m < source.meshes.size(); ++m) {
        auto &before = source.meshes[m];
        auto &after = replacement.meshes[m];
        require(before.name == after.name && before.vertices.size() == after.vertices.size() &&
                    before.indices == after.indices,
                "Geometry editing preserves vertex counts and triangles");
        auto palette = before.palette;
        std::set<unsigned> needed;
        for (auto &v : after.vertices)
            for (unsigned k = 0; k < 4; ++k)
                if (v.weights[k] > 0)
                    needed.insert(v.joints[k]);
        bool skin_changed = false;
        for (unsigned i = 0; i < before.vertices.size(); ++i)
            skin_changed |= before.vertices[i].weights != after.vertices[i].weights ||
                            before.vertices[i].joints != after.vertices[i].joints;
        std::array<unsigned, 4> shared_indices{}, shared_weights{};
        for (unsigned i = 0; i < before.vertices.size(); ++i) {
            auto &a = before.vertices[i];
            auto &b = after.vertices[i];
            finite(b.position);
            finite(b.normal);
            if (a.position != b.position) {
                moved = true;
                for (unsigned k = 0; k < 3; ++k)
                    put_float(out, a.position_offset + k * 4, b.position[k]);
            }
            auto write_direction = [&](Point old, Point value, std::size_t offset, unsigned format,
                                       unsigned elements) {
                if (old == value)
                    return;
                finite(value);
                require(offset && elements >= 3,
                        "This mesh does not have an editable per-vertex direction");
                for (unsigned k = 0; k < 3; ++k) {
                    if (format == 3)
                        put_float(out, offset + k * 4, value[k]);
                    else {
                        float limit = format == 2 ? 32767.f : format == 1 ? 255.f : 127.f;
                        require(
                            value[k] >= -limit && value[k] <= limit &&
                                (format != 1 || value[k] >= 0),
                            "The new direction cannot be stored in this mesh's attribute format");
                        int n = int(std::round(value[k]));
                        if (format == 2)
                            put16(out, offset + k * 2, std::uint16_t(n));
                        else
                            out[offset + k] = std::uint8_t(n);
                    }
                }
            };
            write_direction(a.normal, b.normal, a.normal_offset, a.normal_format,
                            a.normal_elements);
            write_direction(a.tangent, b.tangent, a.tangent_offset, a.tangent_format,
                            a.tangent_elements);
            if (!skin_changed)
                continue;
            auto weights = quantize(b, unsigned(source.joints.size()));
            std::array<unsigned, 4> indices{};
            unsigned count = 0;
            for (unsigned k = 0; k < 4; ++k)
                if (weights[k]) {
                    ++count;
                    require(k < before.influences && (!a.index_offset || k < a.index_elements) &&
                                (!a.weight_offset || k < a.weight_elements),
                            "This mesh cannot store that many bone influences; remove an influence "
                            "or attach at 100 percent");
                    auto it = std::find(palette.begin(), palette.end(), b.joints[k]);
                    if (it == palette.end()) {
                        if (palette.size() < 31) {
                            palette.push_back(std::uint8_t(b.joints[k]));
                            it = palette.end() - 1;
                        } else {
                            it = std::find_if(palette.begin(), palette.end(), [&](auto id) {
                                return !needed.contains(id);
                            });
                            require(it != palette.end(),
                                    "The edited mesh needs more than 31 palette bones; remove an "
                                    "unused attachment or choose an existing bone");
                            *it = std::uint8_t(b.joints[k]);
                        }
                    }
                    indices[k] = unsigned(it - palette.begin());
                }
            require(count <= before.influences, "Too many bone influences for this mesh");
            if (a.rigid)
                require(weights[0] == 255,
                        "This mesh uses rigid attachments; choose 100 percent on one bone");
            if (a.index_offset) {
                if (a.joints != b.joints || a.weights != b.weights)
                    for (unsigned k = 0; k < a.index_elements; ++k)
                        out[a.index_offset + k] = std::uint8_t(indices[k]);
            } else {
                if (i)
                    require(indices == shared_indices,
                            "Bone indices are shared by this mesh; select the entire mesh to "
                            "change its attachment");
                shared_indices = indices;
            }
            if (a.weight_offset) {
                if (a.weights != b.weights)
                    for (unsigned k = 0; k < a.weight_elements; ++k)
                        out[a.weight_offset + k] = std::uint8_t(weights[k]);
            } else {
                if (i)
                    require(
                        weights == shared_weights,
                        "Weights are shared by this mesh; select the entire mesh to change them");
                shared_weights = weights;
            }
        }
        if (skin_changed) {
            out[before.palette_offset] = std::uint8_t(palette.size());
            std::copy(palette.begin(), palette.end(),
                      out.begin() + std::ptrdiff_t(before.palette_offset + 1));
            if (!before.vertices[0].index_offset)
                fixed_attribute(out, before.fixed_indices, shared_indices);
            if (!before.vertices[0].weight_offset)
                fixed_attribute(out, before.fixed_weights, shared_weights);
        }
        auto &box = bounds[before.section_offset];
        box.first = {INFINITY, INFINITY, INFINITY};
        box.second = {-INFINITY, -INFINITY, -INFINITY};
    }
    if (moved) {
        Point low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
        for (unsigned i = 0; i < replacement.meshes.size(); ++i) {
            auto &mesh = replacement.meshes[i];
            auto &box = bounds.at(source.meshes[i].section_offset);
            for (auto &v : mesh.vertices)
                for (unsigned k = 0; k < 3; ++k) {
                    box.first[k] = std::min(box.first[k], v.position[k]);
                    box.second[k] = std::max(box.second[k], v.position[k]);
                    low[k] = std::min(low[k], v.position[k]);
                    high[k] = std::max(high[k], v.position[k]);
                }
        }
        auto expand = [&](std::size_t at, Point lo, Point hi) {
            for (unsigned k = 0; k < 3; ++k) {
                put_float(out, at + k * 4, std::min(f32(out, at + k * 4), lo[k]));
                put_float(out, at + 16 + k * 4, std::max(f32(out, at + 16 + k * 4), hi[k]));
            }
        };
        expand(source.model.bounds_offset, low, high);
        for (auto &[section, box] : bounds)
            expand(section + 88, box.first, box.second);
    }
    auto checked = SkinnedModel::parse(out);
    for (unsigned m = 0; m < checked.meshes.size(); ++m)
        for (unsigned i = 0; i < checked.meshes[m].vertices.size(); ++i) {
            auto &a = checked.meshes[m].vertices[i];
            auto &b = replacement.meshes[m].vertices[i];
            require(a.position == b.position, "Encoded vertex position differs");
            if (source.meshes[m].influences == 0) {
                require(std::all_of(b.weights.begin(), b.weights.end(),
                                    [](float w) {
                                        return w == 0;
                                    }),
                        "This mesh has no skinning channels");
                continue;
            }
            auto expected = quantize(b, unsigned(checked.joints.size()));
            std::map<unsigned, unsigned> actual, weights;
            for (unsigned k = 0; k < 4; ++k) {
                if (a.weights[k] > 0)
                    actual[a.joints[k]] += unsigned(std::round(a.weights[k] * 255));
                if (expected[k])
                    weights[b.joints[k]] += expected[k];
            }
            require(actual == weights,
                    "This mesh's skin command layout cannot store the requested attachment");
        }
    return out;
}
void set_vertex_weight(SkinnedModel &model, std::size_t mesh_index,
                       const std::set<std::size_t> &selected, unsigned bone, float weight) {
    require(bone < model.joints.size() && std::isfinite(weight) && weight >= 0 && weight <= 1,
            "Choose a bone and a weight between zero and one");
    auto next = model.meshes.at(mesh_index);
    require(!selected.empty(), "Select vertices first");
    for (auto index : selected) {
        auto &v = next.vertices.at(index);
        std::map<unsigned, float> weights;
        float remaining = 0;
        for (unsigned k = 0; k < 4; ++k)
            if (v.weights[k] > 0 && v.joints[k] != bone) {
                weights[v.joints[k]] += v.weights[k];
                remaining += v.weights[k];
            }
        require(weight == 1 || remaining > 0, "This vertex has no other bone to receive the "
                                              "remaining weight; attach it to another bone first");
        for (auto &[id, w] : weights)
            w = weight == 1 ? 0 : w * (1 - weight) / remaining;
        weights[bone] = weight;
        std::vector<std::pair<unsigned, float>> entries;
        for (auto entry : weights)
            if (entry.second > 0)
                entries.push_back(entry);
        std::sort(entries.begin(), entries.end(), [](auto a, auto b) {
            return a.second > b.second;
        });
        require(
            entries.size() <= next.influences,
            "This mesh cannot blend that many bones; remove an influence or attach at 100 percent");
        v.weights = {};
        v.joints = {};
        for (unsigned k = 0; k < entries.size(); ++k) {
            v.joints[k] = std::uint16_t(entries[k].first);
            v.weights[k] = entries[k].second;
        }
    }
    auto checked = model;
    checked.meshes.at(mesh_index) = next;
    replace_mesh_geometry(model.model.original, checked);
    model.meshes.at(mesh_index) = std::move(next);
}
void transform_vertices(SkinMesh &mesh, const std::set<std::size_t> &selected, Point translation,
                        Point rotation, Point scale, std::optional<Point> pivot) {
    finite(translation);
    finite(rotation);
    finite(scale);
    require(!selected.empty(), "Select vertices first");
    if (translation == Point{} && rotation == Point{} && scale == Point{1, 1, 1})
        return;
    for (auto s : scale)
        require(s > 0, "Scale must be positive");
    Point center{};
    for (auto i : selected)
        for (unsigned k = 0; k < 3; ++k)
            center[k] += mesh.vertices.at(i).position[k] / float(selected.size());
    if (pivot) {
        finite(*pivot);
        center = *pivot;
    }
    auto next = mesh;
    for (auto i : selected) {
        auto &v = next.vertices.at(i);
        Point p{};
        for (unsigned k = 0; k < 3; ++k)
            p[k] = (v.position[k] - center[k]) * scale[k];
        for (unsigned axis = 0; axis < 3; ++axis) {
            auto u = (axis + 1) % 3, w = (axis + 2) % 3;
            float a = rotation[axis] * .01745329252f, c = std::cos(a), s = std::sin(a), x = p[u],
                  y = p[w];
            p[u] = c * x - s * y;
            p[w] = s * x + c * y;
        }
        for (unsigned k = 0; k < 3; ++k)
            v.position[k] = p[k] + center[k] + translation[k];
        finite(v.position);
        if (rotation != Point{} || scale != Point{1, 1, 1}) {
            auto direction = [&](Point &n, bool normal) {
                float original_length = std::hypot(n[0], n[1], n[2]);
                if (original_length < 1e-8f)
                    return;
                for (unsigned k = 0; k < 3; ++k)
                    n[k] *= normal ? 1 / scale[k] : scale[k];
                for (unsigned axis = 0; axis < 3; ++axis) {
                    auto u = (axis + 1) % 3, w = (axis + 2) % 3;
                    float a = rotation[axis] * .01745329252f, c = std::cos(a), s = std::sin(a),
                          x = n[u], y = n[w];
                    n[u] = c * x - s * y;
                    n[w] = s * x + c * y;
                }
                float length = std::hypot(n[0], n[1], n[2]);
                for (auto &x : n)
                    x = x / length * original_length;
            };
            if (v.normal_offset)
                direction(v.normal, true);
            if (v.tangent_offset)
                direction(v.tangent, false);
        }
    }
    mesh = std::move(next);
}
void rebuild_mesh_normals(SkinMesh &mesh) {
    std::vector<Point> normals(mesh.vertices.size());
    for (unsigned t = 0; t < mesh.indices.size(); t += 3) {
        auto a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
        Point u{}, v{};
        for (unsigned k = 0; k < 3; ++k) {
            u[k] = mesh.vertices[b].position[k] - mesh.vertices[a].position[k];
            v[k] = mesh.vertices[c].position[k] - mesh.vertices[a].position[k];
        }
        Point n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        for (auto i : {a, b, c})
            for (unsigned k = 0; k < 3; ++k)
                normals[i][k] += n[k];
    }
    for (unsigned i = 0; i < mesh.vertices.size(); ++i) {
        auto &vertex = mesh.vertices[i];
        require(vertex.normal_offset && vertex.normal_elements >= 3,
                "This mesh does not have editable per-vertex normals");
        auto n = normals[i];
        float length = std::hypot(n[0], n[1], n[2]);
        if (length < 1e-8f)
            continue;
        float multiplier = vertex.normal_format == 3   ? 1.f
                           : vertex.normal_format == 2 ? 32767.f
                           : vertex.normal_format == 1 ? 255.f
                                                       : 127.f;
        for (unsigned k = 0; k < 3; ++k)
            vertex.normal[k] = n[k] / length * multiplier;
    }
}
}
