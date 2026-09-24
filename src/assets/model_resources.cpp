#include "assets/model_resources.h"
#include "assets/model_exchange.h"
#include "assets/face_materials.h"
#include "formats/container.h"
#include <algorithm>
#include <set>
namespace studio {
namespace {
std::uint32_t hash(const std::string &s) {
    std::uint32_t h = 0x01000193;
    for (unsigned char c : s)
        h = h * 0x01000193 ^ c;
    return h;
}
void valid_name(const std::string &name) {
    require(!name.empty() && name.size() < 64 &&
                std::all_of(name.begin(), name.end(),
                            [](unsigned char c) {
                                return c >= 32 && c < 127 && c != '/' && c != '\\';
                            }),
            "Use a resource name of 1–63 printable ASCII characters, without slashes");
}
void named(Bytes &out, const std::string &name) {
    valid_name(name);
    append32(out, hash(name));
    out.push_back(std::uint8_t(name.size()));
    out.insert(out.end(), name.begin(), name.end());
}
Bytes metadata(const Model &model, const std::array<std::vector<std::string>, 4> &names) {
    Bytes out(model.original.begin() + 16, model.original.begin() + 32);
    std::size_t old = 32;
    for (unsigned table = 0; table < 4; ++table) {
        append32(out, narrow(names[table].size()));
        for (auto &name : names[table]) {
            auto it = std::find(model.names[table].begin(), model.names[table].end(), name);
            if (it != model.names[table].end())
                append(out, slice(model.original,
                                  old + 4 + std::size_t(it - model.names[table].begin()) * 68, 68));
            else {
                valid_name(name);
                auto at = out.size();
                out.resize(at + 68);
                put32(out, at, hash(name));
                std::copy(name.begin(), name.end(), out.begin() + at + 4);
            }
        }
        old += 4 + model.names[table].size() * 68;
    }
    auto tail = slice(model.original, model.bounds_offset,
                      model.sections[0].offset + model.sections[0].size - model.bounds_offset);
    std::size_t end = 112 + u32(tail, 96) + u32(tail, 100);
    auto count = u32(tail, end);
    end += 16;
    for (unsigned i = 0; i < count; ++i) {
        end += 1 + slice(tail, end, 1)[0];
        end += 1 + slice(tail, end, 1)[0];
        end += 37;
        slice(tail, 0, end);
    }
    auto prefix = model.bounds_offset - 32, footer = aligned(prefix + end, 16) - prefix;
    append(out, slice(tail, 0, end));
    out.resize(aligned(out.size(), 16));
    if (footer < tail.size())
        append(out, slice(tail, footer, tail.size() - footer));
    out.resize(aligned(out.size(), 16));
    put32(out, 8, narrow(out.size() - 16));
    return out;
}
Bytes rename(View old, const std::string &material, const std::string &texture,
             const std::string &replacement) {
    Bytes out(old.begin(), old.begin() + 16);
    std::size_t p = 16;
    for (unsigned i = 0; i < 4; ++i) {
        auto n = slice(old, p + 4, 1)[0];
        auto name = text(slice(old, p + 5, n));
        if (i == 0 && !material.empty())
            named(out, material);
        else
            append(out, slice(old, p, 5 + n));
        p += 5 + n;
    }
    if (!texture.empty())
        for (unsigned i = 0; i < 3; ++i)
            require(u32(old, p + i * 4) != hash(texture),
                    "This texture name also identifies a lighting table; keep the resource");
    append(out, slice(old, p, 168));
    p += 168;
    auto count = u32(old, p);
    append32(out, count);
    p += 4;
    for (unsigned i = 0; i < count; ++i) {
        auto n = slice(old, p + 4, 1)[0];
        auto name = text(slice(old, p + 5, n));
        if (name == texture) {
            require(!replacement.empty(),
                    "This texture is used by a material; choose a replacement");
            named(out, replacement);
        } else
            append(out, slice(old, p, 5 + n));
        p += 5 + n;
        append(out, slice(old, p, 42));
        p += 42;
    }
    p = aligned(p, 16);
    out.resize(aligned(out.size(), 16), 255);
    append(out, slice(old, p, old.size() - p));
    put32(out, 8, narrow(out.size() - 16));
    return out;
}
Bytes assemble(const Model &model, const std::array<std::vector<std::string>, 4> &names,
               const std::vector<Bytes> &sections) {
    Bytes out(model.original.begin(), model.original.begin() + 16);
    put32(out, 4, narrow(1 + sections.size()));
    append(out, metadata(model, names));
    for (auto &b : sections)
        append(out, b);
    out.resize(aligned(out.size(), 128));
    Model::parse(out);
    return out;
}
}
Bytes rebuild_model_metadata(const Model &model, const std::array<std::vector<std::string>, 4> &names) {
    return metadata(model, names);
}
Bytes copy_model_material(View bytes, std::size_t index, const std::string &name) {
    valid_name(name);
    auto model = Model::parse(bytes);
    auto names = model.names;
    require(std::find(names[2].begin(), names[2].end(), name) == names[2].end(),
            "A material with this name already exists");
    require(index < names[2].size(), "Choose a donor material");
    names[2].push_back(name);
    std::vector<Bytes> sections;
    Bytes added;
    unsigned material = 0;
    for (auto &s : model.sections)
        if (s.kind == "material") {
            auto raw = slice(bytes, s.offset, s.size);
            sections.emplace_back(raw.begin(), raw.end());
            if (material++ == index)
                added = rename(raw, name, {}, {});
        }
    sections.push_back(added);
    for (auto &s : model.sections)
        if (s.kind != "material" && s.kind != "gfmodel") {
            auto raw = slice(bytes, s.offset, s.size);
            sections.emplace_back(raw.begin(), raw.end());
        }
    return assemble(model, names, sections);
}
Bytes remove_model_material(View bytes, std::size_t index, const std::string &replacement) {
    auto model = Model::parse(bytes);
    require(model.names[2].size() > 1 && index < model.names[2].size(),
            "Keep at least one material");
    auto name = model.names[2][index];
    require(name != replacement && std::find(model.names[2].begin(), model.names[2].end(),
                                             replacement) != model.names[2].end(),
            "Choose a different replacement material");
    auto meshes = decode_model_exchange(bytes);
    auto materials = mesh_materials(bytes);
    MaterialFaces faces;
    for (std::size_t i = 0; i < materials.size(); ++i)
        if (materials[i] == name)
            for (std::size_t f = 0; f < meshes.meshes.at(i).indices.size() / 3; ++f)
                faces[i].insert(f);
    Bytes reassigned(bytes.begin(), bytes.end());
    if (!faces.empty())
        reassigned = assign_face_material(bytes, faces, replacement).bytes;
    model = Model::parse(reassigned);
    auto names = model.names;
    names[2].erase(names[2].begin() + index);
    std::vector<Bytes> sections;
    unsigned material = 0;
    for (auto &s : model.sections)
        if (s.kind != "gfmodel") {
            if (s.kind == "material" && material++ == index)
                continue;
            auto raw = slice(reassigned, s.offset, s.size);
            sections.emplace_back(raw.begin(), raw.end());
        }
    return assemble(model, names, sections);
}
Bytes change_model_texture(View bytes, const std::string &name, const std::string &replacement,
                           bool add) {
    valid_name(name);
    auto model = Model::parse(bytes);
    auto names = model.names;
    auto found = std::find(names[1].begin(), names[1].end(), name);
    if (add) {
        require(found == names[1].end(), "This texture name already exists in the model");
        names[1].push_back(name);
    } else if (found != names[1].end())
        names[1].erase(found);
    if (!replacement.empty() &&
        std::find(names[1].begin(), names[1].end(), replacement) == names[1].end())
        names[1].push_back(replacement);
    std::vector<Bytes> sections;
    for (auto &s : model.sections)
        if (s.kind != "gfmodel") {
            auto raw = slice(bytes, s.offset, s.size);
            if (!add && s.kind == "material")
                sections.push_back(rename(raw, {}, name, replacement));
            else
                sections.emplace_back(raw.begin(), raw.end());
        }
    return assemble(model, names, sections);
}
Bytes change_pack_texture(View bytes, const std::string &name, View added) {
    valid_name(name);
    if (u32(bytes, 0) != 0x10000) {
        auto pack = Container::parse(bytes);
        auto found = std::find_if(pack.files.begin(), pack.files.end(), [&](auto &b) {
            return b.size() >= 128 && u32(b, 0) == 0x15041213 && text(slice(b, 40, 64)) == name;
        });
        if (added.empty()) {
            require(found != pack.files.end(), "Texture resource is missing");
            pack.files.erase(found);
        } else {
            require(found == pack.files.end(), "A texture with this name already exists");
            pack.files.emplace_back(added.begin(), added.end());
        }
        std::size_t alignment = 1;
        auto original = Container::parse(bytes);
        for (std::size_t n = 2; n <= 128; n *= 2) {
            bool match = true;
            for (unsigned i = 0; i <= original.files.size(); ++i)
                match &= u32(bytes, 4 + i * 4) % n == 0;
            if (!match)
                break;
            alignment = n;
        }
        return pack.write(alignment);
    }
    auto pack = ModelPack::parse(bytes);
    auto found = std::find_if(pack.resources.begin(), pack.resources.end(), [&](auto &r) {
        return r.category == 1 && r.name == name;
    });
    std::array<std::size_t, 5> counts{};
    std::size_t total = 0, alignment = 16;
    for (unsigned i = 0; i < 5; ++i)
        counts[i] = u32(bytes, 4 + i * 4);
    for (std::size_t n = 32; n <= 128; n *= 2) {
        bool match = bytes.size() % n == 0;
        for (auto &r : pack.resources)
            match &= u32(bytes, r.address_field) % n == 0;
        if (!match)
            break;
        alignment = n;
    }
    if (added.empty()) {
        require(found != pack.resources.end(), "Texture resource is missing");
        require(std::none_of(found + 1, pack.resources.end(),
                             [](auto &r) {
                                 return r.category == 0;
                             }),
                "This pack stores a model after the texture; removal would change its saved source "
                "identity");
        auto removed = found->index;
        pack.resources.erase(found);
        --counts[1];
        for (auto &resource : pack.resources)
            if (resource.category == 1 && resource.index > removed)
                --resource.index;
    } else {
        require(found == pack.resources.end(), "A texture with this name already exists");
        pack.resources.push_back({1, counts[1]++, name, Bytes(added.begin(), added.end()), 0});
    }
    for (auto c : counts)
        total += c;
    Bytes out(24 + total * 4);
    put32(out, 0, 0x10000);
    for (unsigned i = 0; i < 5; ++i)
        put32(out, 4 + i * 4, narrow(counts[i]));
    std::vector<std::size_t> fields;
    for (auto &r : pack.resources) {
        auto slot = r.index;
        for (unsigned i = 0; i < r.category; ++i)
            slot += counts[i];
        put32(out, 24 + slot * 4, narrow(out.size()));
        out.push_back(std::uint8_t(r.name.size()));
        out.insert(out.end(), r.name.begin(), r.name.end());
        fields.push_back(out.size());
        append32(out, 0);
    }
    out.resize(aligned(out.size(), alignment));
    for (unsigned i = 0; i < pack.resources.size(); ++i) {
        put32(out, fields[i], narrow(out.size()));
        append(out, pack.resources[i].bytes);
        out.resize(aligned(out.size(), alignment));
    }
    auto check = ModelPack::parse(out);
    require(check.resources.size() == pack.resources.size(), "Resource count changed unexpectedly");
    for (unsigned i = 0; i < pack.resources.size(); ++i)
        require(std::equal(pack.resources[i].bytes.begin(), pack.resources[i].bytes.end(),
                           check.resources[i].bytes.begin(),
                           check.resources[i].bytes.begin() + pack.resources[i].bytes.size()),
                "An unrelated resource changed");
    return out;
}
}
