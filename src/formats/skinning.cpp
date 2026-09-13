#include "formats/skinning.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <functional>
namespace studio {
namespace {
Matrix identity() {
    Matrix m{};
    for (unsigned i = 0; i < 4; ++i)
        m[i * 5] = 1;
    return m;
}
Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix m{};
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned k = 0; k < 4; ++k)
                m[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
    return m;
}
std::array<float, 3> transform(const Matrix &m, std::array<float, 3> v, bool point = true) {
    std::array<float, 3> out{};
    for (unsigned r = 0; r < 3; ++r) {
        out[r] = point ? m[r * 4 + 3] : 0;
        for (unsigned c = 0; c < 3; ++c)
            out[r] += m[r * 4 + c] * v[c];
    }
    return out;
}
Matrix local(const Joint &j) {
    auto m = identity();
    for (unsigned axis = 0; axis < 3; ++axis) {
        auto r = identity();
        auto a = (axis + 1) % 3, b = (axis + 2) % 3;
        auto c = std::cos(j.rotation[axis]), s = std::sin(j.rotation[axis]);
        r[a * 4 + a] = c;
        r[b * 4 + b] = c;
        r[a * 4 + b] = -s;
        r[b * 4 + a] = s;
        m = multiply(r, m);
    }
    for (unsigned i = 0; i < 3; ++i)
        m[i * 4 + 3] = j.translation[i];
    return m;
}
Matrix inverse(const Matrix &m) {
    auto out = identity();
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c)
            out[r * 4 + c] = m[c * 4 + r];
    auto t = transform(out, {-m[3], -m[7], -m[11]}, false);
    for (unsigned r = 0; r < 3; ++r)
        out[r * 4 + 3] = t[r];
    return out;
}
float compact_float(std::uint32_t v) {
    auto sign = (v & 0x800000) ? -1.f : 1.f;
    auto exponent = (v >> 16) & 127, mantissa = v & 65535;
    require(exponent != 127, "Non-finite fixed vertex attribute");
    return sign * std::ldexp(exponent ? 1.f + float(mantissa) / 65536.f : float(mantissa) / 65536.f,
                             int(exponent ? exponent : 1) - 63);
}
std::map<unsigned, std::array<float, 4>> fixed_attributes(View stream) {
    std::map<unsigned, std::uint32_t> regs;
    std::array<std::array<std::uint32_t, 3>, 12> words{};
    std::array<unsigned, 12> seen{};
    unsigned index = 0;
    for (const auto &c : commands(stream)) {
        auto mask = std::uint32_t(0);
        for (unsigned i = 0; i < 4; ++i)
            if (c.mask & (1 << i))
                mask |= 255u << (i * 8);
        regs[c.reg] = (regs[c.reg] & ~mask) | (c.value & mask);
        if (c.reg == 0x232)
            index = regs[c.reg] & 15;
        if (c.reg >= 0x233 && c.reg <= 0x235) {
            require(index < 12, "Fixed attribute index is out of range");
            words[index][c.reg - 0x233] = regs[c.reg];
            seen[index] |= 1u << (c.reg - 0x233);
            if (c.reg == 0x235)
                ++index;
        }
    }
    auto semantics = std::uint64_t(regs[0x2bb]) | (std::uint64_t(regs[0x2bc]) << 32);
    auto flags = (regs[0x202] >> 16) & 4095;
    std::map<unsigned, std::array<float, 4>> out;
    for (unsigned i = 0; i < 12; ++i)
        if (flags & (1 << i)) {
            auto semantic = unsigned((semantics >> (4 * i)) & 15);
            if (semantic > 8)
                continue;
            require(seen[i] == 7, "Missing fixed attribute words");
            auto w = words[i];
            out[semantic] = {
                compact_float(w[2] & 0xffffff), compact_float((w[2] >> 24) | ((w[1] & 65535) << 8)),
                compact_float((w[1] >> 16) | ((w[0] & 255) << 16)), compact_float(w[0] >> 8)};
        }
    return out;
}

}
SkinnedModel SkinnedModel::parse(View bytes) {
    SkinnedModel out;
    out.model = Model::parse(bytes);
    const auto &m = out.model;
    require(m.bones <= 255, "Editable model profile supports at most 255 joints");
    bool identity_transform = true, unset_transform = true;
    for (unsigned i = 0; i < 16; ++i) {
        auto value = f32(bytes, m.bounds_offset + 32 + 4 * i);
        identity_transform &= std::abs(value - (i % 5 == 0 ? 1.f : 0.f)) < 1e-5f;
        unset_transform &= value == 0.f;
    }
    require(m.bones == 0 || identity_transform || unset_transform,
            "Skinned profile requires an identity or unset model transform");
    auto pos = m.bounds_offset + 96;
    pos += 16 + std::size_t(u32(bytes, pos)) + u32(bytes, pos + 4);
    pos += 16;
    auto metadata_end = m.sections[0].offset + m.sections[0].size;
    std::map<std::string, int> names;
    auto string = [&]() {
        auto n = slice(bytes, pos++, 1)[0];
        auto s = text(slice(bytes, pos, n));
        pos += n;
        require(s.size() == n, "Embedded zero in joint name");
        return s;
    };
    for (std::size_t i = 0; i < m.bones; ++i) {
        Joint j;
        j.name = string();
        j.parent_name = string();
        j.flags = slice(bytes, pos++, 1)[0];
        j.transform_offset = pos;
        require(!j.name.empty() && names.emplace(j.name, int(i)).second,
                "Empty or duplicate joint name");
        for (auto *v : {&j.scale, &j.rotation, &j.translation})
            for (auto &value : *v) {
                value = f32(bytes, pos);
                pos += 4;
                require(std::isfinite(value), "Non-finite joint transform");
            }
        for (auto s : j.scale)
            require(std::abs(s - 1) < 1e-5f, "Initial skinned profile requires unit joint scales");
        require((j.flags & 0xfc) == 0, "Billboard or unknown joint flags are unsupported");
        out.joints.push_back(j);
    }
    require(pos <= metadata_end, "Skeleton extends beyond model metadata");
    for (auto &j : out.joints)
        if (!j.parent_name.empty()) {
            require(names.contains(j.parent_name), "Joint parent is missing");
            j.parent = names.at(j.parent_name);
        }
    std::vector<unsigned> visited(out.joints.size());
    std::function<void(std::size_t)> bind = [&](std::size_t i) {
        require(visited[i] != 1, "Cyclic skeleton");
        if (visited[i] == 2)
            return;
        visited[i] = 1;
        auto &j = out.joints[i];
        auto mat = local(j);
        if (j.parent >= 0) {
            bind(std::size_t(j.parent));
            mat = multiply(out.joints[std::size_t(j.parent)].bind, mat);
        }
        j.bind = mat;
        j.inverse_bind = inverse(mat);
        visited[i] = 2;
    };
    for (std::size_t i = 0; i < out.joints.size(); ++i)
        bind(i);
    for (const auto &section : m.sections)
        if (section.kind == "mesh") {
            auto b = slice(bytes, section.offset, section.size);
            auto count = u32(b, 120);
            require(count > 0 && count < 1024, "Invalid submesh count");
            auto mode = u32(b, 124);
            require(mode <= 4, "Unsupported influence count");
            pos = 144;
            std::vector<View> streams;
            for (std::size_t i = 0; i < count * 3; ++i) {
                auto length = u32(b, pos);
                require(u32(b, pos + 4) == i && u32(b, pos + 8) == count * 3,
                        "Invalid submesh command ordering");
                streams.push_back(slice(b, pos + 16, length));
                pos += 16 + length;
            }
            struct Data {
                SkinMesh mesh;
                std::size_t vertices, indices, vbytes, ibytes;
            };
            std::vector<Data> data;
            for (std::size_t i = 0; i < count; ++i) {
                Data d;
                d.mesh.name = text(slice(b, 20, 64)) + "/" + std::to_string(i);
                d.mesh.section_offset = section.offset;
                d.mesh.influences = mode;
                auto length = u32(b, pos + 4);
                auto mat = text(slice(b, pos + 8, length));
                require(std::find(m.names[2].begin(), m.names[2].end(), mat) != m.names[2].end(),
                        "Submesh material is missing");
                pos += 8 + length;
                d.mesh.palette_offset = section.offset + pos;
                auto palette_count = slice(b, pos, 32)[0];
                require((mode == 0 || palette_count > 0) && palette_count <= 31,
                        "Invalid bone palette size");
                std::set<unsigned> used;
                for (unsigned j = 0; j < palette_count; ++j) {
                    auto bone = b[pos + 1 + j];
                    require((mode == 0 || bone < out.joints.size()) && used.insert(bone).second,
                            "Invalid or duplicate palette joint");
                    d.mesh.palette.push_back(bone);
                }
                pos += 32;
                d.vertices = u32(b, pos);
                d.indices = u32(b, pos + 4);
                d.vbytes = u32(b, pos + 8);
                d.ibytes = u32(b, pos + 12);
                pos += 16;
                require(d.vertices > 0 && d.vertices <= 65536 && d.indices < 1000000,
                        "Invalid geometry counts");
                data.push_back(d);
            }
            for (std::size_t sub = 0; sub < count; ++sub) {
                auto &d = data[sub];
                auto layout = vertex_layout(streams[sub * 3]);
                auto fixed = fixed_attributes(streams[sub * 3]);
                {
                    auto stream = streams[sub * 3];
                    auto offset = std::size_t(stream.data() - bytes.data());
                    std::map<unsigned, std::uint32_t> regs;
                    for (auto c : commands(stream))
                        regs[c.reg] = c.value;
                    auto semantics =
                        std::uint64_t(regs[0x2bb]) | (std::uint64_t(regs[0x2bc]) << 32);
                    unsigned index = 0;
                    for (auto c : commands(stream)) {
                        if (c.reg == 0x232)
                            index = c.value & 15;
                        if (c.reg >= 0x233 && c.reg <= 0x235) {
                            auto semantic =
                                index < 12 ? unsigned((semantics >> (index * 4)) & 15) : 15u;
                            if (index < 12 && c.mask == 15) {
                                if (semantic == 7)
                                    d.mesh.fixed_indices[c.reg - 0x233] = offset + c.offset;
                                if (semantic == 8)
                                    d.mesh.fixed_weights[c.reg - 0x233] = offset + c.offset;
                            }
                            if (c.reg == 0x235)
                                ++index;
                        }
                    }
                }
                auto vb = slice(b, pos, d.vbytes);
                require(d.vertices * layout.stride <= d.vbytes, "Vertex buffer is truncated");
                bool wide = false;
                std::uint32_t draw_count = 0;
                for (auto c : commands(streams[sub * 3 + 2])) {
                    if (c.reg == 0x227)
                        wide = (c.value >> 31) != 0;
                    if (c.reg == 0x228)
                        draw_count = c.value;
                    if (c.reg == 0x25e && (c.mask & 8))
                        require((c.value >> 28) == 0, "Skinned profile requires triangle lists");
                }
                require(draw_count == d.indices && d.indices % 3 == 0, "Index draw count mismatch");
                for (std::size_t vi = 0; vi < d.vertices; ++vi) {
                    auto attributes = fixed;
                    SkinVertex v;
                    v.rigid = mode == 1;
                    bool has_position = false;
                    for (auto a : layout.attributes) {
                        auto address = vi * layout.stride + a.offset;
                        std::array<float, 4> value{};
                        for (unsigned k = 0; k < a.elements; ++k) {
                            if (a.format == 3)
                                value[k] = f32(vb, address + 4 * k);
                            else if (a.format == 2)
                                value[k] =
                                    float(static_cast<std::int16_t>(u16(vb, address + 2 * k)));
                            else
                                value[k] = a.format == 1
                                               ? float(vb[address + k])
                                               : float(static_cast<std::int8_t>(vb[address + k]));
                            require(std::isfinite(value[k]), "Non-finite vertex attribute");
                        }
                        if (a.semantic == 2) {
                            v.tangent_offset = section.offset + pos + address;
                            v.tangent_format = a.format;
                            v.tangent_elements = a.elements;
                            std::copy_n(value.begin(), 3, v.tangent.begin());
                        }
                        if (a.semantic == 1) {
                            v.normal_offset = section.offset + pos + address;
                            v.normal_format = a.format;
                            v.normal_elements = a.elements;
                        }
                        if (a.semantic == 7) {
                            v.index_offset = section.offset + pos + address;
                            v.index_elements = a.elements;
                        }
                        if (a.semantic == 8) {
                            v.weight_offset = section.offset + pos + address;
                            v.weight_elements = a.elements;
                        }
                        attributes[a.semantic] = value;
                        if (a.semantic == 1)
                            std::copy_n(value.begin(), 3, v.normal.begin());
                        if (a.semantic == 4)
                            std::copy_n(value.begin(), 2, v.uv.begin());
                        if (a.semantic == 0) {
                            require(a.format == 3 && a.elements == 3,
                                    "Skinned edit profile requires float3 positions");
                            has_position = true;
                            v.position_offset = section.offset + pos + address;
                            std::copy_n(value.begin(), 3, v.position.begin());
                        }
                        if (a.semantic == 7 || a.semantic == 8)
                            require(a.format == 1,
                                    "Skinned profile requires byte bone indices and weights");
                    }
                    require(has_position &&
                                (mode == 0 || (attributes.contains(7) && attributes.contains(8))),
                            "Missing position or skin attributes");
                    auto indices = mode ? attributes.at(7) : std::array<float, 4>{},
                         weights = mode ? attributes.at(8) : std::array<float, 4>{};
                    float sum = 0;
                    for (unsigned k = 0; k < 4; ++k) {
                        require(weights[k] >= 0 && weights[k] <= 255, "Invalid skin weight");
                        v.weights[k] = weights[k] / 255.f;
                        sum += v.weights[k];
                        if (v.weights[k] > 0) {
                            require(indices[k] >= 0 && indices[k] == std::floor(indices[k]) &&
                                        indices[k] < float(d.mesh.palette.size()),
                                    "Weighted palette index is invalid");
                            v.joints[k] = d.mesh.palette[std::size_t(indices[k])];
                        }
                    }
                    require(mode == 0 || std::abs(sum - 1) < 1e-5f,
                            "Skin weights do not sum to one");
                    if (v.rigid)
                        require(v.weights[0] > .999f, "Rigid submesh has blended weights");
                    v.bind_position = v.position;
                    d.mesh.vertices.push_back(v);
                }
                pos += d.vbytes;
                auto ib = slice(b, pos, d.ibytes);
                require(d.indices * (wide ? 2 : 1) <= d.ibytes, "Index buffer is truncated");
                for (std::size_t i = 0; i < d.indices; ++i) {
                    auto index = wide ? u16(ib, i * 2) : ib[i];
                    require(std::size_t(index) < d.vertices, "Triangle index exceeds vertex count");
                    d.mesh.indices.push_back(static_cast<std::uint16_t>(index));
                }
                pos += d.ibytes;
                out.meshes.push_back(std::move(d.mesh));
            }
            require(pos <= b.size(), "Geometry extends beyond mesh section");
        }
    return out;
}
}
