#include "compiler/model_build.h"
#include "compiler/asset.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace studio {
namespace model_build {
std::uint32_t hash(const std::string &name) {
    std::uint32_t h = 0x01000193;
    for (unsigned char c : name)
        h = h * 0x01000193 ^ c;
    return h;
}
void string(Bytes &bytes, const std::string &name) {
    append(bytes, View(reinterpret_cast<const std::uint8_t *>(name.data()), name.size()));
}
void named(Bytes &bytes, const std::string &name) {
    require(name.size() < 256, "Name is too long");
    append32(bytes, hash(name));
    bytes.push_back(static_cast<std::uint8_t>(name.size()));
    string(bytes, name);
}
void table(Bytes &bytes, const std::vector<std::string> &names) {
    append32(bytes, narrow(names.size()));
    for (const auto &name : names) {
        require(name.size() < 64, "Model dependency name is too long");
        auto pos = bytes.size();
        bytes.resize(pos + 68);
        put32(bytes, pos, hash(name));
        std::copy(name.begin(), name.end(), bytes.begin() + static_cast<std::ptrdiff_t>(pos + 4));
    }
}
Bytes section(const std::string &tag, View payload) {
    Bytes out(16);
    std::copy(tag.begin(), tag.end(), out.begin());
    append(out, payload);
    out.resize(aligned(out.size(), 16));
    put32(out, 8, narrow(out.size() - 16));
    put32(out, 12, 0xffffffff);
    return out;
}
Bytes material(View old, const std::string &name, const std::string &texture) {
    std::size_t pos = 16;
    Bytes payload;
    for (unsigned i = 0; i < 4; ++i) {
        auto length = slice(old, pos + 4, 1)[0];
        auto field = slice(old, pos, 5 + length);
        if (i == 0)
            named(payload, name);
        else
            append(payload, field);
        pos += field.size();
    }
    append(payload, slice(old, pos, 168));
    pos += 168;
    auto count = u32(old, pos);
    require(count > 0 && count <= 3, "Unsupported material texture count");
    append32(payload, count);
    pos += 4;
    bool changed = false;
    for (unsigned i = 0; i < count; ++i) {
        auto length = slice(old, pos + 4, 1)[0];
        auto state = pos + 5 + length;
        auto parameters = slice(old, state, 42);
        if (parameters[0] == 0) {
            require(!changed && parameters[1] == 0, "Primary texture must use UV mapping");
            require(f32(parameters, 2) == 1 && f32(parameters, 6) == 1 &&
                        f32(parameters, 10) == 0 && f32(parameters, 14) == 0 &&
                        f32(parameters, 18) == 0,
                    "Primary texture transform must be identity");
            named(payload, texture);
            changed = true;
        } else
            append(payload, slice(old, pos, 5 + length));
        append(payload, parameters);
        pos = state + 42;
    }
    require(changed, "Missing primary texture binding");
    pos = 16 + aligned(pos - 16, 16);
    payload.resize(aligned(payload.size(), 16), 255);
    append(payload, slice(old, pos, old.size() - pos));
    return section("material", payload);
}
Bytes mesh_names(View old, const std::string &name, const std::string &mat, std::uint8_t bone) {
    Bytes out(old.begin(), old.begin() + 144);
    std::fill(out.begin() + 20, out.begin() + 84, std::uint8_t{0});
    put32(out, 16, hash(name));
    std::copy(name.begin(), name.end(), out.begin() + 20);
    put32(out, 124, 1);
    std::size_t pos = 144;
    for (unsigned i = 0; i < 3; ++i) {
        auto length = 16 + std::size_t(u32(old, pos));
        append(out, slice(old, pos, length));
        pos += length;
    }
    auto length = u32(old, pos + 4);
    auto palette = pos + 8 + length;
    auto padded = std::size_t(length);
    while (padded < mat.size() + 1)
        padded += 16;
    require(padded <= 64, "Material name exceeds mesh name storage");
    append32(out, hash(mat));
    append32(out, narrow(padded));
    auto name_start = out.size();
    string(out, mat);
    out.resize(name_start + padded);
    Bytes bones(32);
    bones[0] = 1;
    bones[1] = bone;
    append(out, bones);
    append(out, slice(old, palette + 32, old.size() - palette - 32));
    out.resize(aligned(out.size(), 16));
    put32(out, 8, narrow(out.size() - 16));
    return out;
}
std::size_t skeleton_end(View tail) {
    auto pos = 96 + 16 + std::size_t(u32(tail, 96)) + u32(tail, 100);
    auto count = u32(tail, pos);
    pos += 16;
    for (std::size_t i = 0; i < count; ++i) {
        pos += 1 + slice(tail, pos, 1)[0];
        pos += 1 + slice(tail, pos, 1)[0];
        pos += 37;
        slice(tail, 0, pos);
    }
    return pos;
}
Bytes metadata(const Model &m, const std::array<std::vector<std::string>, 4> &names, View tail) {
    Bytes payload;
    std::size_t original_table = 32;
    for (std::size_t category = 0; category < 4; ++category) {
        append32(payload, narrow(names[category].size()));
        for (const auto &name : names[category]) {
            auto existing = std::find(m.names[category].begin(), m.names[category].end(), name);
            if (existing != m.names[category].end())
                append(payload, slice(m.original,
                                      original_table + 4 +
                                          std::size_t(existing - m.names[category].begin()) * 68,
                                      68));
            else {
                Bytes entry;
                table(entry, {name});
                append(payload, slice(entry, 4, 68));
            }
        }
        original_table += 4 + m.names[category].size() * 68;
    }
    auto end = skeleton_end(tail), old_prefix = m.bounds_offset - 32;
    auto footer = aligned(old_prefix + end, 16) - old_prefix;
    append(payload, slice(tail, 0, end));
    payload.resize(aligned(payload.size(), 16));
    if (footer < tail.size())
        append(payload, slice(tail, footer, tail.size() - footer));
    return section("gfmodel", payload);
}
Bytes assemble(View original, View meta, const std::vector<Bytes> &mats,
               const std::vector<Bytes> &meshes) {
    Bytes out(original.begin(), original.begin() + 16);
    put32(out, 4, narrow(1 + mats.size() + meshes.size()));
    append(out, meta);
    for (const auto &bytes : mats)
        append(out, bytes);
    for (const auto &bytes : meshes)
        append(out, bytes);
    out.resize(aligned(out.size(), 128));
    return out;
}
void bounds(Bytes &bytes, std::size_t pos, std::array<float, 3> low, std::array<float, 3> high,
            bool expand) {
    for (unsigned i = 0; i < 3; ++i) {
        put_float(bytes, pos + 4 * i, expand ? std::min(f32(bytes, pos + 4 * i), low[i]) : low[i]);
        put_float(bytes, pos + 16 + 4 * i,
                  expand ? std::max(f32(bytes, pos + 16 + 4 * i), high[i]) : high[i]);
    }
}
}
using namespace model_build;
Bytes pack_resources(const std::vector<ModelResource> &resources) {
    std::array<std::size_t, 5> counts{};
    std::set<std::pair<std::size_t, std::size_t>> keys;
    for (const auto &r : resources) {
        require(r.category < 5 && r.index < 65536 && !r.name.empty() && r.name.size() < 256,
                "Invalid resource entry");
        require(keys.insert({r.category, r.index}).second, "Repeated resource slot");
        counts[r.category] = std::max(counts[r.category], r.index + 1);
    }
    std::size_t total = 0;
    for (auto n : counts)
        total += n;
    Bytes out(24 + 4 * total);
    put32(out, 0, 0x10000);
    std::vector<std::size_t> fields;
    for (unsigned i = 0; i < 5; ++i)
        put32(out, 4 + i * 4, narrow(counts[i]));
    for (const auto &r : resources) {
        auto slot = r.index;
        for (std::size_t i = 0; i < r.category; ++i)
            slot += counts[i];
        put32(out, 24 + slot * 4, narrow(out.size()));
        out.push_back(static_cast<std::uint8_t>(r.name.size()));
        string(out, r.name);
        fields.push_back(out.size());
        append32(out, 0);
    }
    out.resize(aligned(out.size(), 128));
    for (std::size_t i = 0; i < resources.size(); ++i) {
        put32(out, fields[i], narrow(out.size()));
        append(out, resources[i].bytes);
        out.resize(aligned(out.size(), 128));
    }
    auto checked = ModelPack::parse(out);
    require(checked.resources.size() == resources.size(), "Resource pack rebuild count mismatch");
    return out;
}
GeneratedSkin compile_asset(const ModelPack &donor, const AssetDocument &asset) {
    require(!asset.meshes.empty() && !asset.materials.empty() && !asset.joints.empty(),
            "Empty imported asset");
    auto primary = std::find_if(donor.resources.begin(), donor.resources.end(), [](auto &r) {
        return r.category == 0;
    });
    require(primary != donor.resources.end(), "Missing material preset model");
    auto skin = SkinnedModel::parse(primary->bytes);
    const auto &m = skin.model;
    auto source = std::find_if(skin.meshes.begin(), skin.meshes.end(), [&](auto &part) {
        return !part.vertices.front().rigid && u32(m.original, part.section_offset + 120) == 1;
    });
    require(source != skin.meshes.end(), "Asset profile needs a blended mesh preset");
    auto sec = std::find_if(m.sections.begin(), m.sections.end(), [&](auto &section) {
        return section.offset == source->section_offset;
    });
    auto old = slice(m.original, sec->offset, sec->size);
    std::size_t cursor = 144;
    std::array<Bytes, 3> commands_template;
    for (auto &list : commands_template) {
        auto data = slice(old, cursor, 16 + std::size_t(u32(old, cursor)));
        list.assign(data.begin(), data.end());
        cursor += data.size();
    }
    auto layout = vertex_layout(slice(commands_template[0], 16, commands_template[0].size() - 16));
    std::set<unsigned> semantics;
    for (auto a : layout.attributes) {
        semantics.insert(a.semantic);
        require((a.semantic == 0 || a.semantic == 1)
                    ? (a.format == 3 && a.elements == 3)
                    : (a.semantic == 4 ? (a.format == 3 && a.elements == 2)
                                       : ((a.semantic == 7 || a.semantic == 8) && a.format == 1 &&
                                          a.elements == 4)),
                "Unsupported imported vertex preset");
    }
    require(semantics == std::set<unsigned>{0, 1, 4, 7, 8},
            "Imported preset is missing required attributes");
    auto old_name = text(slice(old, cursor + 8, u32(old, cursor + 4)));
    Bytes material_template;
    std::size_t material_index = 0;
    for (auto &section : m.sections)
        if (section.kind == "material") {
            if (m.names[2][material_index++] == old_name) {
                auto data = slice(m.original, section.offset, section.size);
                material_template.assign(data.begin(), data.end());
            }
        }
    require(!material_template.empty(), "Missing material preset");
    auto texture_template =
        std::find_if(donor.resources.begin(), donor.resources.end(), [](auto &r) {
            return r.category == 1;
        });
    require(texture_template != donor.resources.end(), "Missing texture preset");
    auto names = m.names;
    names[2].clear();
    names[3].clear();
    std::vector<Bytes> mats, meshes;
    auto resources = donor.resources;
    std::size_t next_texture = 0;
    for (auto &r : resources)
        if (r.category == 1)
            next_texture = std::max(next_texture, r.index + 1);
    for (auto &mat : asset.materials) {
        auto texture = mat.name + ".tga";
        names[1].push_back(texture);
        names[2].push_back(mat.name);
        mats.push_back(material(material_template, mat.name, texture));
        resources.push_back({1, next_texture++, mat.name,
                             encode_texture(texture_template->bytes, mat.image, texture), 0});
    }
    float radius = 1;
    for (auto &part : asset.meshes)
        for (auto &v : part.vertices)
            for (auto x : v.position) {
                require(std::isfinite(x) && std::abs(x) < 100000, "Invalid imported position");
                radius = std::max(radius, std::abs(x) * 3 + 10);
            }
    std::array<float, 3> low{-radius, -radius, -radius}, high{radius, radius, radius};
    for (auto &part : asset.meshes) {
        require(part.material < asset.materials.size(), "Missing imported material");
        names[3].push_back(part.name);
        auto lists = commands_template;
        bool patched = false, wide = false;
        for (auto c : commands(slice(lists[2], 16, lists[2].size() - 16))) {
            if (c.reg == 0x227)
                wide = (c.value & 0x80000000) != 0;
            if (c.reg == 0x228) {
                require(c.mask == 15, "Unsupported draw count mask");
                put32(lists[2], 16 + c.offset, narrow(part.indices.size()));
                patched = true;
            }
        }
        require(patched && wide, "Imported profile requires 16-bit indices");
        Bytes vb(part.vertices.size() * layout.stride);
        for (std::size_t i = 0; i < part.vertices.size(); ++i) {
            auto &v = part.vertices[i];
            for (auto a : layout.attributes) {
                auto at = i * layout.stride + a.offset;
                if (a.semantic == 7 || a.semantic == 8) {
                    auto values = a.semantic == 7 ? v.joints : v.weights;
                    std::copy(values.begin(), values.end(),
                              vb.begin() + static_cast<std::ptrdiff_t>(at));
                } else
                    for (unsigned k = 0; k < a.elements; ++k)
                        put_float(vb, at + k * 4,
                                  a.semantic == 0 ? v.position[k]
                                                  : (a.semantic == 1 ? v.normal[k] : v.uv[k]));
            }
        }
        vb.resize(aligned(vb.size(), 16));
        Bytes ib(part.indices.size() * 2);
        for (std::size_t i = 0; i < part.indices.size(); ++i)
            put16(ib, i * 2, part.indices[i]);
        ib.resize(aligned(ib.size(), 16));
        Bytes mesh(old.begin(), old.begin() + 144);
        for (auto &list : lists)
            append(mesh, list);
        auto record_length = 8 + std::size_t(u32(old, cursor + 4)) + 32 + 16;
        append(mesh, slice(old, cursor, record_length));
        auto counts = mesh.size() - 16;
        put32(mesh, counts, narrow(part.vertices.size()));
        put32(mesh, counts + 4, narrow(part.indices.size()));
        put32(mesh, counts + 8, narrow(vb.size()));
        put32(mesh, counts + 12, narrow(ib.size()));
        append(mesh, vb);
        append(mesh, ib);
        mesh = mesh_names(mesh, part.name, asset.materials[part.material].name, 0);
        put32(mesh, 124, 4);
        std::size_t at = 144;
        for (unsigned i = 0; i < 3; ++i)
            at += 16 + u32(mesh, at);
        at += 8 + u32(mesh, at + 4);
        mesh[at] = static_cast<std::uint8_t>(part.palette.size());
        std::copy(part.palette.begin(), part.palette.end(),
                  mesh.begin() + static_cast<std::ptrdiff_t>(at + 1));
        bounds(mesh, 88, low, high, false);
        meshes.push_back(mesh);
    }
    auto old_tail = slice(m.original, m.bounds_offset,
                          m.sections[0].offset + m.sections[0].size - m.bounds_offset);
    auto skeleton = 96 + 16 + std::size_t(u32(old_tail, 96)) + u32(old_tail, 100);
    Bytes tail(old_tail.begin(), old_tail.begin() + static_cast<std::ptrdiff_t>(skeleton + 16));
    put32(tail, skeleton, narrow(asset.joints.size()));
    for (auto &joint : asset.joints) {
        for (auto &name : {joint.name, joint.parent_name}) {
            tail.push_back(static_cast<std::uint8_t>(name.size()));
            string(tail, name);
        }
        tail.push_back(joint.flags);
        for (auto values : {joint.scale, joint.rotation, joint.translation})
            for (auto v : values) {
                auto at = tail.size();
                tail.resize(at + 4);
                put_float(tail, at, v);
            }
    }
    auto prefix = m.bounds_offset - 32;
    tail.resize(aligned(prefix + tail.size(), 16) - prefix);
    auto footer = aligned(prefix + skeleton_end(old_tail), 16) - prefix;
    append(tail, slice(old_tail, footer, old_tail.size() - footer));
    auto model = assemble(m.original, metadata(m, names, tail), mats, meshes);
    auto parsed = SkinnedModel::parse(model);
    bounds(model, parsed.model.bounds_offset, low, high, false);
    for (auto &r : resources)
        if (r.category == 0 && r.index == primary->index) {
            r.name = asset.name;
            r.bytes = model;
        }
    return {model, pack_resources(resources), {}};
}

}
