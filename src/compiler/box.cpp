#include "compiler/box.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace studio {
namespace {
struct Vertex {
    std::array<float, 3> position, normal;
    std::array<float, 2> uv;
};
std::vector<Vertex> box_vertices(BoxSize size) {
    const float x = size.x / 2, y = size.y, z = size.z / 2;
    using Point = std::array<float, 3>;
    const std::array<std::array<Point, 4>, 6> faces{
        {{{{-x, 0, z}, {x, 0, z}, {x, y, z}, {-x, y, z}}},
         {{{x, 0, -z}, {-x, 0, -z}, {-x, y, -z}, {x, y, -z}}},
         {{{x, 0, z}, {x, 0, -z}, {x, y, -z}, {x, y, z}}},
         {{{-x, 0, -z}, {-x, 0, z}, {-x, y, z}, {-x, y, -z}}},
         {{{-x, y, z}, {x, y, z}, {x, y, -z}, {-x, y, -z}}},
         {{{-x, 0, -z}, {x, 0, -z}, {x, 0, z}, {-x, 0, z}}}}};
    const std::array<Point, 6> normals{
        {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}}};
    const std::array<std::array<float, 2>, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    std::vector<Vertex> out;
    for (std::size_t f = 0; f < 6; ++f)
        for (std::size_t i = 0; i < 4; ++i)
            out.push_back({faces[f][i], normals[f], uv[i]});
    return out;
}
void bounds(Bytes &bytes, std::size_t offset, BoxSize size) {
    const std::array<float, 8> values{-size.x / 2, 0,      -size.z / 2, 1,
                                      size.x / 2,  size.y, size.z / 2,  1};
    for (std::size_t i = 0; i < values.size(); ++i)
        put_float(bytes, offset + 4 * i, values[i]);
}
}
BoxBuild compile_box(const Model &donor, BoxSize size) {
    for (auto v : {size.x, size.y, size.z})
        require(std::isfinite(v) && v > 0 && v <= 100000,
                "Box dimensions must be finite and between 0 and 100000");
    require(donor.bones == 0, "Box profile requires a rigid donor without skeleton joints");
    for (std::size_t i = 0; i < 16; ++i) {
        const auto value = f32(donor.original, donor.bounds_offset + 32 + 4 * i);
        require(std::isfinite(value) && std::abs(value - (i % 5 == 0 ? 1.0f : 0.0f)) < 0.00001f,
                "Box profile requires an identity model transform");
    }
    require(donor.names[2].size() == 1 && donor.names[3].size() == 1,
            "Box profile requires one material and one mesh");
    auto mesh = std::find_if(donor.sections.begin(), donor.sections.end(), [](const auto &s) {
        return s.kind == "mesh";
    });
    require(mesh != donor.sections.end(), "Missing mesh section");
    auto b = slice(donor.original, mesh->offset, mesh->size);
    require(u32(b, 120) == 1, "Box profile requires one submesh");
    std::size_t pos = 144;
    std::array<Bytes, 3> lists;
    for (std::size_t i = 0; i < 3; ++i) {
        auto count = u32(b, pos);
        require(u32(b, pos + 4) == i && u32(b, pos + 8) == 3,
                "Expected enable, disable and draw command lists");
        auto v = slice(b, pos, 16 + std::size_t(count));
        lists[i].assign(v.begin(), v.end());
        pos += 16 + count;
    }
    auto layout = vertex_layout(slice(lists[0], 16, lists[0].size() - 16));
    bool position = false, uv = false;
    for (const auto &a : layout.attributes) {
        require(a.semantic <= 6, "Box profile does not support streamed skinning attributes");
        if (a.semantic == 0) {
            require(a.format == 3 && a.elements == 3, "Box profile requires float3 positions");
            position = true;
        }
        if (a.semantic >= 4) {
            require(a.format == 3 && a.elements == 2, "Box profile requires float2 UVs");
            if (a.semantic == 4)
                uv = true;
        }
        if (a.semantic == 1 || a.semantic == 2)
            require(a.elements >= 3 && a.format != 1, "Unsupported normal/tangent representation");
        if (a.semantic == 3)
            require(a.format == 1 && a.elements == 4, "Box profile requires byte RGBA colors");
    }
    require(position && uv, "Box profile requires position and primary UV streams");
    auto material_length = u32(b, pos + 4);
    auto metadata_size = 8 + std::size_t(material_length) + 32 + 16;
    auto metadata = slice(b, pos, metadata_size);
    Bytes record(metadata.begin(), metadata.end());
    auto counts = metadata_size - 16;
    auto palette = 8 + std::size_t(material_length);
    require(record[palette] <= 2, "Unsupported rigid bone palette");
    require(u32(record, counts) > 0 && u32(record, counts + 8) >= layout.stride,
            "Missing donor vertex data");
    auto original_vertex = slice(b, pos + metadata_size, layout.stride);
    auto draw = commands(slice(lists[2], 16, lists[2].size() - 16));
    bool indexed = false, patched = false;
    for (const auto &c : draw) {
        if (c.reg == 0x227) {
            require((c.value & 0x80000000) != 0, "Box profile requires 16-bit indices");
            indexed = true;
        }
        if (c.reg == 0x25e && (c.mask & 8))
            require((c.value >> 28) == 0, "Box profile requires triangle-list primitives");
        if (c.reg == 0x228) {
            require(c.mask == 15, "Unsupported partial index count command");
            put32(lists[2], 16 + c.offset, 36);
            patched = true;
        }
    }
    require(indexed && patched, "Draw commands do not define supported indexed geometry");
    auto vertices = box_vertices(size);
    Bytes vertex_bytes;
    for (const auto &v : vertices) {
        auto base = vertex_bytes.size();
        append(vertex_bytes, original_vertex);
        for (const auto &a : layout.attributes) {
            std::array<float, 4> values{};
            if (a.semantic == 0)
                std::copy(v.position.begin(), v.position.end(), values.begin());
            else if (a.semantic == 1)
                std::copy(v.normal.begin(), v.normal.end(), values.begin());
            else if (a.semantic == 2)
                values = {v.normal[2] != 0 ? v.normal[2] : (v.normal[0] != 0 ? 0.0f : 1.0f), 0,
                          -v.normal[0], 1};
            else if (a.semantic == 3)
                values = {1, 1, 1, 1};
            else {
                values[0] = v.uv[0];
                values[1] = v.uv[1];
            }
            auto off = base + a.offset;
            for (unsigned i = 0; i < a.elements; ++i) {
                if (a.format == 3)
                    put_float(vertex_bytes, off + 4 * i, values[i]);
                else if (a.format == 2)
                    put16(vertex_bytes, off + 2 * i,
                          static_cast<std::uint16_t>(
                              static_cast<std::int16_t>(std::lround(values[i] * 32767))));
                else
                    vertex_bytes[off + i] = static_cast<std::uint8_t>(
                        static_cast<int>(std::lround(values[i] * (a.format == 1 ? 255 : 127))));
            }
        }
    }
    vertex_bytes.resize(aligned(vertex_bytes.size(), 16));
    Bytes indices(72);
    const std::array<unsigned, 6> triangle{0, 1, 2, 0, 2, 3};
    for (unsigned face = 0; face < 6; ++face)
        for (unsigned i = 0; i < 6; ++i)
            put16(indices, (face * 6 + i) * 2, static_cast<std::uint16_t>(face * 4 + triangle[i]));
    indices.resize(aligned(indices.size(), 16));
    put32(record, counts, 24);
    put32(record, counts + 4, 36);
    put32(record, counts + 8, narrow(vertex_bytes.size()));
    put32(record, counts + 12, narrow(indices.size()));
    Bytes rebuilt(b.begin(), b.begin() + 144);
    bounds(rebuilt, 88, size);
    for (const auto &list : lists)
        append(rebuilt, list);
    append(rebuilt, record);
    append(rebuilt, vertex_bytes);
    append(rebuilt, indices);
    rebuilt.resize(aligned(rebuilt.size(), 16));
    put32(rebuilt, 8, narrow(rebuilt.size() - 16));
    BoxBuild result;
    result.material = donor.names[2][0];
    append(result.model, slice(donor.original, 0, mesh->offset));
    append(result.model, rebuilt);
    append(result.model, slice(donor.original, mesh->offset + mesh->size,
                               donor.original.size() - mesh->offset - mesh->size));
    bounds(result.model, donor.bounds_offset, size);
    auto check = Model::parse(result.model);
    require(check.names == donor.names, "Model dependency names changed");
    std::ostringstream obj;
    obj << "o prop\n";
    for (const auto &v : vertices)
        obj << "v " << v.position[0] << ' ' << v.position[1] << ' ' << v.position[2] << '\n';
    for (const auto &v : vertices)
        obj << "vt " << v.uv[0] << ' ' << v.uv[1] << '\n';
    for (const auto &v : vertices)
        obj << "vn " << v.normal[0] << ' ' << v.normal[1] << ' ' << v.normal[2] << '\n';
    for (unsigned face = 0; face < 6; ++face)
        for (unsigned t = 0; t < 2; ++t) {
            obj << "f";
            for (unsigned i = 0; i < 3; ++i) {
                auto n = face * 4 + triangle[t * 3 + i] + 1;
                obj << ' ' << n << '/' << n << '/' << n;
            }
            obj << '\n';
        }
    result.preview_obj = obj.str();
    return result;
}
}
