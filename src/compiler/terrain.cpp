#include "compiler/terrain.h"
#include "compiler/model_build.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
#include <limits>
namespace studio {
namespace {
using namespace model_build;
void identity(const Model &model) {
    require(model.bones == 0, "Terrain export requires a rigid model");
    for (unsigned i = 0; i < 16; ++i)
        require(std::abs(f32(model.original, model.bounds_offset + 32 + i * 4) -
                         (i % 5 == 0 ? 1.f : 0.f)) < .00001f,
                "Terrain export requires an identity model transform");
}
Bytes authored_vertex_stream(View list) {
    auto writes = commands(list.subspan(16));
    std::map<unsigned, std::uint32_t> regs;
    for (const auto &c : writes)
        for (unsigned lane = 0; lane < 4; ++lane)
            if (c.mask & (1u << lane)) {
                const auto mask = 255u << (8 * lane);
                regs[c.reg] = (regs[c.reg] & ~mask) | (c.value & mask);
            }
    auto formats = std::uint64_t(regs[0x201]) | (std::uint64_t(regs[0x202]) << 32);
    auto semantics = std::uint64_t(regs[0x2bb]) | (std::uint64_t(regs[0x2bc]) << 32);
    unsigned total = unsigned(formats >> 60) + 1;
    require(total <= 12, "Authored mesh has an invalid shader attribute count");
    for (auto a : vertex_layout(list.subspan(16)).attributes)
        require(a.semantic <= 6, "Authored rigid mesh contains skinning attributes");
    std::uint64_t mapping = 0;
    for (unsigned semantic = 0; semantic < 7; ++semantic) {
        unsigned index = 0;
        for (; index < total; ++index)
            if (((semantics >> (4 * index)) & 15) == semantic)
                break;
        if (index == total) {
            require(total < 12, "Authored mesh has no free shader attribute slot");
            ++total;
            semantics =
                (semantics & ~(15ull << (4 * index))) | (std::uint64_t(semantic) << (4 * index));
        }
        const unsigned format = semantic == 3 ? 13 : semantic >= 4 ? 7 : 11;
        formats = (formats & ~(15ull << (4 * index))) | (std::uint64_t(format) << (4 * index));
        formats &= ~(1ull << (48 + index));
        mapping |= std::uint64_t(index) << (4 * semantic);
    }
    formats = (formats & ~(15ull << 60)) | (std::uint64_t(total - 1) << 60);
    std::map<unsigned, std::uint32_t> replacements{
        {0x201, std::uint32_t(formats)},
        {0x202, std::uint32_t(formats >> 32)},
        {0x204, std::uint32_t(mapping)},
        {0x205, (7u << 28) | (64u << 16)},
        {0x2bb, std::uint32_t(semantics)},
        {0x2bc, std::uint32_t(semantics >> 32)},
        {0x242, (regs[0x242] & ~255u) | (total - 1)},
        {0x2b9, (regs.contains(0x2b9) ? regs[0x2b9] & ~15u : 0xa0000000u) | (total - 1)}};
    Bytes result(list.begin(), list.end());
    std::size_t end = result.size();
    std::map<unsigned, unsigned> written_masks;
    for (const auto &c : writes) {
        if (c.reg == 0x23d) {
            end = std::min(end, 16 + c.offset);
            break;
        }
        if (auto found = replacements.find(c.reg); found != replacements.end()) {
            put32(result, 16 + c.offset, found->second);
            written_masks[c.reg] |= c.mask;
        }
    }
    result.resize(end);
    auto emit = [&](unsigned reg, std::uint32_t value, unsigned mask) {
        append32(result, value);
        append32(result, reg | (mask << 16));
    };
    for (auto [reg, value] : replacements)
        if (written_masks[reg] != 15)
            emit(reg, value, 15);
    if (result.size() % 16 == 0)
        emit(0, 0, 0);
    emit(0x23d, 1, 15);
    put32(result, 0, narrow(result.size() - 16));
    return result;
}
Bytes material_copy(View original, const std::string &name, const std::string &base,
                    const std::string &overlay) {
    Bytes payload;
    std::size_t pos = 16;
    for (unsigned i = 0; i < 4; ++i) {
        auto length = 5 + std::size_t(slice(original, pos + 4, 1)[0]);
        if (i == 0)
            named(payload, name);
        else
            append(payload, slice(original, pos, length));
        pos += length;
    }
    append(payload, slice(original, pos, 168));
    pos += 168;
    require(u32(original, pos) == 2, "Terrain blend preset must have two textures");
    append32(payload, 2);
    pos += 4;
    std::set<unsigned> slots;
    for (unsigned i = 0; i < 2; ++i) {
        auto state = pos + 5 + slice(original, pos + 4, 1)[0];
        auto parameters = slice(original, state, 42);
        auto slot = parameters[0];
        require(slot < 2 && slots.insert(slot).second && parameters[1] == 0,
                "Terrain preset needs two ordinary UV texture bindings");
        for (unsigned j = 0; j < 5; ++j)
            require(f32(parameters, 2 + j * 4) == (j < 2 ? 1.f : 0.f),
                    "Terrain preset texture transforms must be identity");
        require(u32(parameters, 22) == 2 && u32(parameters, 26) == 2,
                "Terrain preset textures must repeat");
        named(payload, slot == 0 && !overlay.empty() ? overlay : base);
        append(payload, parameters);
        pos = state + 42;
    }
    pos = aligned(pos, 16);
    payload.resize(aligned(payload.size(), 16), 255);
    const auto commands_at = payload.size() + 32;
    append(payload, slice(original, pos, original.size() - pos));
    bool alpha_test = false;
    for (auto c : commands(slice(payload, commands_at, u32(original, pos))))
        if (c.reg == 0x104) {
            require(c.mask & 1, "Terrain preset needs a writable alpha-test function");
            put32(payload, commands_at + c.offset, (c.value & ~0x70u) | 0x10u);
            alpha_test = true;
        }
    require(alpha_test, "Terrain preset is missing alpha-test state");
    return section("material", payload);
}
}
static Bytes append_meshes(const Model &destination, const std::string &material_preset,
                           const Model &geometry_preset, const std::string &geometry_mesh,
                           const std::vector<TerrainExportMesh> &meshes, bool replace_existing,
                           bool preserve_material, unsigned source_submesh) {
    using namespace model_build;
    identity(destination);
    require(geometry_preset.bones == 0, "Terrain vertex preset must be rigid");
    require(!meshes.empty(), "No authored terrain to export");
    auto material_id =
        std::find(destination.names[2].begin(), destination.names[2].end(), material_preset);
    require(material_id != destination.names[2].end(), "Missing terrain material preset");
    std::vector<Bytes> materials, existing_meshes;
    for (const auto &part : destination.sections) {
        auto bytes = slice(destination.original, part.offset, part.size);
        if (part.kind == "material")
            materials.emplace_back(bytes.begin(), bytes.end());
        else if (part.kind == "mesh")
            existing_meshes.emplace_back(bytes.begin(), bytes.end());
    }
    const auto material = materials.at(std::size_t(material_id - destination.names[2].begin()));
    auto source = std::find_if(
        geometry_preset.sections.begin(), geometry_preset.sections.end(), [&](const auto &part) {
            return part.kind == "mesh" &&
                   text(slice(geometry_preset.original, part.offset + 20, 64)) == geometry_mesh;
        });
    require(source != geometry_preset.sections.end(), "Missing terrain vertex-layout preset");
    const auto original = slice(geometry_preset.original, source->offset, source->size);
    const auto submeshes = u32(original, 120);
    require(submeshes > 0 && submeshes < 1024 && source_submesh < submeshes &&
                (preserve_material || submeshes == 1),
            "Unsupported source submesh selection");
    std::size_t pos = 144;
    std::array<Bytes, 3> lists;
    for (unsigned i = 0; i < submeshes * 3; ++i) {
        const auto size = 16 + std::size_t(u32(original, pos));
        require(u32(original, pos + 4) == i && u32(original, pos + 8) == submeshes * 3,
                "Unsupported geometry command list ordering");
        if (i / 3 == source_submesh) {
            const auto bytes = slice(original, pos, size);
            lists[i % 3] = {bytes.begin(), bytes.end()};
            put32(lists[i % 3], 4, i % 3);
            put32(lists[i % 3], 8, 3);
        }
        pos += size;
    }
    for (unsigned i = 0; i < source_submesh; ++i)
        pos += 8 + std::size_t(u32(original, pos + 4)) + 48;
    if (preserve_material)
        require(text(slice(original, pos + 8, u32(original, pos + 4))) == material_preset,
                "Extracted mesh material does not match its source submesh");
    if (preserve_material)
        lists[0] = authored_vertex_stream(lists[0]);
    auto layout = vertex_layout(slice(lists[0], 16, lists[0].size() - 16));
    std::set<unsigned> semantics;
    for (const auto &a : layout.attributes) {
        semantics.insert(a.semantic);
        require((a.semantic <= (preserve_material ? 2u : 1u) && a.format == 3 && a.elements == 3) ||
                    (a.semantic == 3 && a.format == 1 && a.elements == 4) ||
                    (a.semantic >= 4 && a.semantic <= 6 && a.format == 3 && a.elements == 2),
                "Unsupported terrain vertex attribute");
    }
    if (preserve_material)
        require(semantics.contains(0), "Rigid geometry needs a position stream");
    else
        for (unsigned required : {0u, 1u, 3u, 4u})
            require(semantics.contains(required),
                    "Terrain layout needs position, normal, color and UV0 streams");
    const auto name_size = std::size_t(u32(original, pos + 4)), palette_at = pos + 8 + name_size;
    auto palette = slice(original, palette_at, 32);
    require(palette[0] == 0 || (palette[0] == 1 && palette[1] == 0) ||
                (palette[0] == 2 && palette[1] == 0 && palette[2] == 1),
            "Terrain vertex preset has a nontrivial bone palette: " + std::to_string(palette[0]) +
                "," + std::to_string(palette[1]) + "," + std::to_string(palette[2]));
    auto names = destination.names;
    if (replace_existing) {
        if (!preserve_material) {
            materials.clear();
            names[2].clear();
        }
        existing_meshes.clear();
        names[3].clear();
    }
    std::set<std::string> used;
    used.insert(names[2].begin(), names[2].end());
    used.insert(names[3].begin(), names[3].end());
    std::array<float, 3> low{}, high{};
    for (unsigned a = 0; a < 3; ++a) {
        low[a] = replace_existing ? std::numeric_limits<float>::max()
                                  : f32(destination.original, destination.bounds_offset + a * 4);
        high[a] = replace_existing
                      ? std::numeric_limits<float>::lowest()
                      : f32(destination.original, destination.bounds_offset + 16 + a * 4);
    }
    for (const auto &input : meshes) {
        require(!input.name.empty() && input.name.size() < 64 &&
                    input.name.find('\0') == std::string::npos && used.insert(input.name).second,
                "Terrain mesh/material name is empty, repeated or too long");
        require(!input.vertices.empty() && input.vertices.size() <= 65536 &&
                    !input.indices.empty() && input.indices.size() % 3 == 0 &&
                    input.indices.size() < 3000000,
                "Invalid terrain vertex or index count");
        for (auto index : input.indices)
            require(index < input.vertices.size(), "Terrain triangle references a missing vertex");
        for (const auto &texture : {input.base_texture, input.overlay_texture})
            if (!texture.empty()) {
                require(texture.size() < 64 && texture.find('\0') == std::string::npos,
                        "Terrain texture name is too long");
                if (std::find(names[1].begin(), names[1].end(), texture) == names[1].end())
                    names[1].push_back(texture);
            }
        require(preserve_material || !input.base_texture.empty(), "Terrain needs a base texture");
        Bytes vertices(input.vertices.size() * layout.stride);
        std::array<float, 3> mesh_low{INFINITY, INFINITY, INFINITY},
            mesh_high{-INFINITY, -INFINITY, -INFINITY};
        for (std::size_t i = 0; i < input.vertices.size(); ++i) {
            const auto &v = input.vertices[i];
            for (float value : v.position)
                require(std::isfinite(value) && std::abs(value) < 1e7f, "Invalid terrain position");
            for (float value : v.normal)
                require(std::isfinite(value), "Invalid terrain normal");
            for (float value : v.uv)
                require(std::isfinite(value) && std::abs(value) < 1e7f, "Invalid terrain UV");
            if (preserve_material) {
                for (const auto &uv : {v.uv1, v.uv2})
                    for (float value : uv)
                        require(std::isfinite(value) && std::abs(value) < 1e7f,
                                "Invalid object UV");
                for (float value : v.tangent)
                    require(std::isfinite(value), "Invalid object tangent");
            }
            float length = 0;
            for (float value : v.normal)
                length += value * value;
            require(length > .99f && length < 1.01f, "Terrain normals must be normalized");
            for (unsigned a = 0; a < 3; ++a) {
                mesh_low[a] = std::min(mesh_low[a], v.position[a]);
                mesh_high[a] = std::max(mesh_high[a], v.position[a]);
            }
            for (const auto &a : layout.attributes) {
                const auto at = i * layout.stride + a.offset;
                if (a.semantic == 3)
                    put32(vertices, at, v.color);
                else
                    for (unsigned j = 0; j < a.elements; ++j)
                        put_float(vertices, at + j * 4,
                                  a.semantic == 0                        ? v.position[j]
                                  : a.semantic == 1                      ? v.normal[j]
                                  : a.semantic == 2                      ? v.tangent[j]
                                  : preserve_material && a.semantic == 5 ? v.uv1[j]
                                  : preserve_material && a.semantic == 6 ? v.uv2[j]
                                                                         : v.uv[j]);
            }
        }
        for (std::size_t i = 0; i < input.indices.size(); i += 3) {
            const auto &a = input.vertices[input.indices[i]].position;
            const auto &b = input.vertices[input.indices[i + 1]].position;
            const auto &c = input.vertices[input.indices[i + 2]].position;
            double area = 0;
            for (unsigned j = 0; j < 3; ++j) {
                const auto k = (j + 1) % 3, l = (j + 2) % 3;
                double n = (double(b[k]) - a[k]) * (double(c[l]) - a[l]) -
                           (double(b[l]) - a[l]) * (double(c[k]) - a[k]);
                area += n * n;
            }
            require(preserve_material || area > 1e-16, "Terrain has a collapsed triangle");
        }
        vertices.resize(aligned(vertices.size(), 16));
        Bytes indices(input.indices.size() * 2);
        for (std::size_t i = 0; i < input.indices.size(); ++i)
            put16(indices, i * 2, input.indices[i]);
        indices.resize(aligned(indices.size(), 16));
        auto commands_copy = lists;
        bool count = false, wide = false;
        for (const auto &command :
             commands(slice(commands_copy[2], 16, commands_copy[2].size() - 16))) {
            if (command.reg == 0x227) {
                require(command.mask == 15, "Unsupported terrain index-buffer mask");
                put32(commands_copy[2], 16 + command.offset, command.value | 0x80000000u);
                wide = true;
            }
            if (command.reg == 0x228) {
                require(command.mask == 15, "Unsupported terrain draw-count mask");
                put32(commands_copy[2], 16 + command.offset, narrow(input.indices.size()));
                count = true;
            }
            if (command.reg == 0x25e && (command.mask & 8))
                require(command.value >> 28 == 0, "Terrain preset must draw triangle lists");
        }
        require(count && wide, "Missing terrain indexed draw commands");
        Bytes mesh(original.begin(), original.begin() + 144);
        put32(mesh, 120, 1);
        std::fill(mesh.begin() + 20, mesh.begin() + 84, std::uint8_t(0));
        put32(mesh, 16, hash(input.name));
        std::copy(input.name.begin(), input.name.end(), mesh.begin() + 20);
        bounds(mesh, 88, mesh_low, mesh_high, false);
        for (const auto &list : commands_copy)
            append(mesh, list);
        const auto &binding = preserve_material ? material_preset : input.name;
        append32(mesh, hash(binding));
        const auto padded = aligned(binding.size() + 1, 16);
        append32(mesh, narrow(padded));
        auto at = mesh.size();
        string(mesh, binding);
        mesh.resize(at + padded);
        append(mesh, palette);
        append32(mesh, narrow(input.vertices.size()));
        append32(mesh, narrow(input.indices.size()));
        append32(mesh, narrow(vertices.size()));
        append32(mesh, narrow(indices.size()));
        append(mesh, vertices);
        append(mesh, indices);
        mesh.resize(aligned(mesh.size(), 16));
        put32(mesh, 8, narrow(mesh.size() - 16));
        if (!preserve_material) {
            materials.push_back(
                material_copy(material, input.name, input.base_texture, input.overlay_texture));
            names[2].push_back(input.name);
        }
        existing_meshes.push_back(std::move(mesh));
        names[3].push_back(input.name);
        for (unsigned a = 0; a < 3; ++a) {
            low[a] = std::min(low[a], mesh_low[a]);
            high[a] = std::max(high[a], mesh_high[a]);
        }
    }
    auto tail = slice(destination.original, destination.bounds_offset,
                      destination.sections.front().offset + destination.sections.front().size -
                          destination.bounds_offset);
    auto meta = metadata(destination, names, tail);
    auto result = assemble(destination.original, meta, materials, existing_meshes);
    auto parsed = Model::parse(result);
    bounds(result, parsed.bounds_offset, low, high, false);
    return result;
}
Bytes append_terrain_meshes(const Model &destination, const std::string &material_preset,
                            const Model &geometry_preset, const std::string &geometry_mesh,
                            const std::vector<TerrainExportMesh> &meshes, bool replace_existing) {
    return append_meshes(destination, material_preset, geometry_preset, geometry_mesh, meshes,
                         replace_existing, false, 0);
}
Bytes append_rigid_meshes(const Model &destination, const std::string &material,
                          const Model &geometry, const std::string &source_mesh,
                          const std::vector<TerrainExportMesh> &meshes, bool replace_geometry,
                          unsigned source_submesh) {
    auto local = destination;
    if (replace_geometry)
        for (unsigned i = 0; i < 16; ++i)
            put_float(local.original, local.bounds_offset + 32 + i * 4, i % 5 == 0 ? 1.f : 0.f);
    return append_meshes(local, material, geometry, source_mesh, meshes, replace_geometry, true,
                         source_submesh);
}

}
