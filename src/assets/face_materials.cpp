#include "assets/face_materials.h"
#include <algorithm>
#include <array>
namespace studio {
namespace {
struct Part {
    std::array<Bytes, 3> commands;
    Bytes descriptor, vertices, indices;
    std::string material;
    std::size_t palette = 0;
    unsigned vertex_count = 0, index_count = 0;
    bool wide = false;
    std::vector<std::uint16_t> triangles;
    std::vector<bool> selected;
    std::set<std::size_t> sources;
};
struct Group {
    Bytes header, tail;
    std::vector<Part> parts;
};
std::vector<Group> read_groups(View bytes) {
    auto model = Model::parse(bytes);
    std::vector<Group> groups;
    std::size_t index = 0;
    for (auto &section : model.sections)
        if (section.kind == "mesh") {
            auto data = slice(bytes, section.offset, section.size);
            Group group;
            auto header = slice(data, 0, 144);
            group.header.assign(header.begin(), header.end());
            auto count = u32(data, 120);
            require(count > 0 && count < 1024, "Invalid material draw-section count");
            group.parts.resize(count);
            std::size_t p = 144;
            for (unsigned i = 0; i < count; ++i)
                for (unsigned s = 0; s < 3; ++s) {
                    auto length = u32(data, p);
                    require(u32(data, p + 4) == i * 3 + s && u32(data, p + 8) == count * 3,
                            "Invalid material draw-command ordering");
                    auto stream = slice(data, p + 16, length);
                    group.parts[i].commands[s].assign(stream.begin(), stream.end());
                    p += 16 + length;
                }
            for (auto &part : group.parts) {
                auto start = p;
                auto length = u32(data, p + 4);
                part.material = text(slice(data, p + 8, length));
                require(std::find(model.names[2].begin(), model.names[2].end(), part.material) !=
                            model.names[2].end(),
                        "Missing native material");
                part.palette = 8 + length;
                p += part.palette + 32;
                part.vertex_count = u32(data, p);
                part.index_count = u32(data, p + 4);
                require(part.vertex_count > 0 && part.vertex_count <= 65536 &&
                            part.index_count < 1000000 && part.index_count % 3 == 0,
                        "Invalid face counts");
                p += 16;
                auto descriptor = slice(data, start, p - start);
                part.descriptor.assign(descriptor.begin(), descriptor.end());
                part.sources.insert(index++);
            }
            for (auto &part : group.parts) {
                auto counts = part.palette + 32;
                auto vertex_bytes = u32(part.descriptor, counts + 8),
                     index_bytes = u32(part.descriptor, counts + 12);
                auto vb = slice(data, p, vertex_bytes),
                     ib = slice(data, p + vertex_bytes, index_bytes);
                part.vertices.assign(vb.begin(), vb.end());
                part.indices.assign(ib.begin(), ib.end());
                p += vertex_bytes + index_bytes;
                bool count = false;
                for (auto command : commands(part.commands[2])) {
                    if (command.reg == 0x227)
                        part.wide = (command.value >> 31) != 0;
                    if (command.reg == 0x228) {
                        require(command.value == part.index_count, "Face draw count differs");
                        count = true;
                    }
                    if (command.reg == 0x25e && (command.mask & 8))
                        require(command.value >> 28 == 0,
                                "Material assignment requires triangle lists");
                }
                require(count, "Missing face draw count");
                for (unsigned i = 0; i < part.index_count; ++i) {
                    auto vertex = part.wide ? u16(ib, i * 2) : slice(ib, i, 1)[0];
                    require(vertex < part.vertex_count, "Face references a missing vertex");
                    part.triangles.push_back(std::uint16_t(vertex));
                }
                part.selected.resize(part.index_count / 3, false);
            }
            auto tail = slice(data, p, data.size() - p);
            group.tail.assign(tail.begin(), tail.end());
            groups.push_back(std::move(group));
        }
    return groups;
}
void set_material(Part &part, const std::string &name) {
    if (part.material == name)
        return;
    auto length = aligned(name.size() + 1, 16);
    Bytes descriptor;
    std::uint32_t hash = 0x01000193;
    for (unsigned char c : name)
        hash = hash * 0x01000193 ^ c;
    append32(descriptor, hash);
    append32(descriptor, narrow(length));
    auto start = descriptor.size();
    descriptor.resize(start + length);
    std::copy(name.begin(), name.end(), descriptor.begin() + start);
    append(descriptor, slice(part.descriptor, part.palette, 48));
    part.descriptor = std::move(descriptor);
    part.palette = 8 + length;
    part.material = name;
}
void set_indices(Part &part) {
    bool wide = part.wide || std::any_of(part.triangles.begin(), part.triangles.end(), [](auto i) {
                    return i > 255;
                });
    Bytes bytes(aligned(part.triangles.size() * (wide ? 2 : 1), 16));
    for (unsigned i = 0; i < part.triangles.size(); ++i)
        if (wide)
            put16(bytes, i * 2, part.triangles[i]);
        else
            bytes[i] = std::uint8_t(part.triangles[i]);
    bool count = false, format = false;
    for (auto command : commands(part.commands[2])) {
        if (command.reg == 0x228) {
            require(command.mask == 15, "Unsupported partial face-count command");
            put32(part.commands[2], command.offset, narrow(part.triangles.size()));
            count = true;
        }
        if (command.reg == 0x227) {
            require(command.mask == 15, "Unsupported partial index-format command");
            put32(part.commands[2], command.offset,
                  (command.value & 0x7fffffffu) | (wide ? 0x80000000u : 0));
            format = true;
        }
    }
    require(count && format, "Missing writable face commands");
    part.index_count = narrow(part.triangles.size());
    part.wide = wide;
    part.indices = std::move(bytes);
    put32(part.descriptor, part.palette + 36, part.index_count);
    put32(part.descriptor, part.palette + 44, narrow(part.indices.size()));
}
bool can_join(const Part &a, const Part &b) {
    if (a.material != b.material || a.vertices != b.vertices || a.commands[0] != b.commands[0] ||
        a.commands[1] != b.commands[1] || a.vertex_count != b.vertex_count)
        return false;
    if (!std::equal(a.descriptor.begin() + a.palette, a.descriptor.begin() + a.palette + 32,
                    b.descriptor.begin() + b.palette))
        return false;
    auto first = a.commands[2], second = b.commands[2];
    for (auto *stream : {&first, &second})
        for (auto c : commands(*stream))
            if (c.reg == 0x228)
                put32(*stream, c.offset, 0);
    return first == second;
}
}
std::vector<std::string> mesh_materials(View bytes) {
    std::vector<std::string> result;
    for (auto &group : read_groups(bytes))
        for (auto &part : group.parts)
            result.push_back(part.material);
    return result;
}
FaceMaterialEdit assign_face_material(View bytes, const MaterialFaces &faces,
                                      const std::string &material) {
    auto model = Model::parse(bytes);
    require(std::find(model.names[2].begin(), model.names[2].end(), material) !=
                model.names[2].end(),
            "Choose an existing material in this model");
    auto groups = read_groups(bytes);
    std::size_t total = 0;
    for (auto &group : groups)
        total += group.parts.size();
    for (auto &[mesh, selected] : faces) {
        require(mesh < total, "Selected mesh is missing");
        (void)selected;
    }
    bool changed = false;
    std::size_t index = 0;
    for (auto &group : groups) {
        bool group_changed = false;
        std::vector<Part> next;
        for (auto &part : group.parts) {
            auto found = faces.find(index++);
            if (found == faces.end() || found->second.empty()) {
                next.push_back(std::move(part));
                continue;
            }
            for (auto face : found->second)
                require(face < part.triangles.size() / 3, "Selected face is missing");
            if (part.material == material) {
                for (auto face : found->second)
                    part.selected[face] = true;
                next.push_back(std::move(part));
                continue;
            }
            changed = group_changed = true;
            if (found->second.size() == part.triangles.size() / 3) {
                set_material(part, material);
                std::fill(part.selected.begin(), part.selected.end(), true);
                next.push_back(std::move(part));
                continue;
            }
            auto assigned = part;
            set_material(assigned, material);
            assigned.triangles.clear();
            assigned.selected.clear();
            std::vector<std::uint16_t> remaining;
            for (unsigned face = 0; face < part.triangles.size() / 3; ++face) {
                auto &target = found->second.contains(face) ? assigned.triangles : remaining;
                target.insert(target.end(), part.triangles.begin() + face * 3,
                              part.triangles.begin() + face * 3 + 3);
            }
            part.triangles = std::move(remaining);
            part.selected.assign(part.triangles.size() / 3, false);
            assigned.selected.assign(assigned.triangles.size() / 3, true);
            set_indices(part);
            set_indices(assigned);
            next.push_back(std::move(part));
            next.push_back(std::move(assigned));
        }
        if (group_changed)
            for (std::size_t i = 0; i < next.size(); ++i)
                for (std::size_t j = i + 1; j < next.size();) {
                    if (can_join(next[i], next[j])) {
                        auto &a = next[i];
                        auto &b = next[j];
                        require(a.triangles.size() + b.triangles.size() < 1000000,
                                "Material section has too many triangle indices");
                        a.triangles.insert(a.triangles.end(), b.triangles.begin(),
                                           b.triangles.end());
                        a.selected.insert(a.selected.end(), b.selected.begin(), b.selected.end());
                        a.sources.insert(b.sources.begin(), b.sources.end());
                        set_indices(a);
                        next.erase(next.begin() + j);
                    } else
                        ++j;
                }
        require(next.size() < 1024, "Material assignment exceeds this mesh's draw-section limit; "
                                    "reduce its material partitions");
        group.parts = std::move(next);
    }
    FaceMaterialEdit result;
    for (auto &group : groups)
        for (auto &part : group.parts) {
            auto mesh = result.sources.size();
            result.sources.push_back(part.sources);
            for (unsigned face = 0; face < part.selected.size(); ++face)
                if (part.selected[face])
                    result.selected[mesh].insert(face);
        }
    if (!changed) {
        result.bytes.assign(bytes.begin(), bytes.end());
        return result;
    }
    result.bytes.assign(bytes.begin(), bytes.begin() + 16);
    index = 0;
    for (auto &section : model.sections) {
        if (section.kind != "mesh") {
            append(result.bytes, slice(bytes, section.offset, section.size));
            continue;
        }
        auto &group = groups.at(index++);
        Bytes data = group.header;
        put32(data, 120, narrow(group.parts.size()));
        unsigned stream_index = 0;
        for (auto &part : group.parts)
            for (auto &stream : part.commands) {
                append32(data, narrow(stream.size()));
                append32(data, stream_index++);
                append32(data, narrow(group.parts.size() * 3));
                append32(data, 0);
                append(data, stream);
            }
        for (auto &part : group.parts)
            append(data, part.descriptor);
        for (auto &part : group.parts) {
            append(data, part.vertices);
            append(data, part.indices);
        }
        append(data, group.tail);
        data.resize(aligned(data.size(), 16));
        put32(data, 8, narrow(data.size() - 16));
        append(result.bytes, data);
    }
    auto verified = read_groups(result.bytes);
    require(verified.size() == groups.size(), "Material mesh groups did not round-trip");
    for (unsigned g = 0; g < groups.size(); ++g) {
        require(verified[g].parts.size() == groups[g].parts.size(),
                "Material sections did not round-trip");
        for (unsigned p = 0; p < groups[g].parts.size(); ++p) {
            auto &a = groups[g].parts[p];
            auto &b = verified[g].parts[p];
            require(a.material == b.material && a.triangles == b.triangles &&
                        a.vertices == b.vertices && a.commands == b.commands &&
                        a.descriptor == b.descriptor,
                    "Face material assignment did not round-trip");
        }
    }
    return result;
}
}
