#include "assets/material_effect.h"
#include "assets/material_motion.h"
#include "assets/material_document.h"
#include "assets/pokemon_motions.h"
#include "scene/model_decoder.h"
#include "core/digest.h"
#include <algorithm>
#include <numeric>
#include <set>
namespace studio {
namespace {
std::uint32_t hash(const std::string &s) {
    std::uint32_t h = 0x01000193;
    for (unsigned char c : s)
        h = h * 0x01000193 ^ c;
    return h;
}
void named(Bytes &b, const std::string &s) {
    require(s.size() < 64, "Imported resource name is too long");
    append32(b, hash(s));
    b.push_back(std::uint8_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
Bytes section(const char *tag, View payload) {
    Bytes b(16);
    std::copy_n(tag, std::strlen(tag), b.begin());
    append(b, payload);
    b.resize(aligned(b.size(), 16));
    put32(b, 8, narrow(b.size() - 16));
    put32(b, 12, 0xffffffff);
    return b;
}
std::string unique_name(const std::string &prefix, const std::string &source,
                        std::set<std::string> &used) {
    auto stem = prefix + source.substr(0, 32);
    auto name = stem;
    for (unsigned i = 2; used.contains(name); ++i)
        name = stem + "_" + std::to_string(i);
    require(name.size() < 64, "Imported name exceeds model limits");
    used.insert(name);
    return name;
}
Bytes source_resource(const ModelDocument &doc, const AssetResourceLink &link) {
    return asset_resource(doc.sources.at(link.source).original, link.path);
}
const AssetResourceLink &shader_link(const ModelDocument &doc, const std::string &name) {
    for (auto &r : doc.resources)
        if (r.role == "Shader" && r.name == name)
            return r;
    throw std::runtime_error("Missing donor shader resource: " + name);
}
struct LookupRecords {
    std::size_t offset = 0, end = 0;
    unsigned length = 0;
    std::vector<Bytes> records;
};
LookupRecords lookup_records(const Model &model) {
    auto b = slice(model.original, 0, model.sections.front().offset + model.sections.front().size);
    std::size_t p = model.bounds_offset + 96;
    p += 16 + u32(b, p) + u32(b, p + 4);
    auto count = u32(b, p);
    p += 16;
    for (unsigned i = 0; i < count; ++i) {
        p += 1 + slice(b, p, 1)[0];
        p += 1 + slice(b, p, 1)[0];
        p += 37;
        slice(b, 0, p);
    }
    p = aligned(p, 16);
    LookupRecords out;
    out.offset = p;
    count = u32(b, p);
    out.length = u32(b, p + 4);
    require(count <= 4096 && out.length % 8 == 0, "Invalid lighting table layout");
    p = aligned(p + 8, 16);
    for (unsigned i = 0; i < count; ++i) {
        auto record = slice(b, p, 16 + out.length);
        out.records.emplace_back(record.begin(), record.end());
        p += record.size();
    }
    out.end = p;
    return out;
}
std::size_t material_metadata(View b) {
    std::size_t p = 16;
    for (unsigned i = 0; i < 4; ++i)
        p += 5 + slice(b, p + 4, 1)[0];
    slice(b, p, 168);
    return p;
}
Bytes rename_material(View old, const std::map<std::string, std::string> &materials,
                      const std::map<std::string, std::string> &shaders,
                      const std::map<std::string, std::string> &textures,
                      const std::map<std::uint32_t, std::uint32_t> &tables, std::uint8_t stencil) {
    Bytes b;
    std::size_t p = 16;
    for (unsigned i = 0; i < 4; ++i) {
        auto n = old[p + 4];
        auto name = text(slice(old, p + 5, n));
        auto &map = i == 0 ? materials : shaders;
        auto it = map.find(name);
        named(b, it == map.end() ? name : it->second);
        p += 5 + n;
    }
    auto metadata = b.size();
    append(b, slice(old, p, 168));
    for (unsigned c = 0; c < 3; ++c) {
        auto id = u32(b, metadata + c * 4);
        if (id)
            put32(b, metadata + c * 4, tables.at(id));
    }
    p += 168;
    unsigned count = u32(old, p);
    append32(b, count);
    p += 4;
    for (unsigned i = 0; i < count; ++i) {
        auto n = old[p + 4];
        auto name = text(slice(old, p + 5, n));
        named(b, textures.at(name));
        p += 5 + n;
        append(b, slice(old, p, 42));
        p += 42;
    }
    p = aligned(p, 16);
    b.resize(aligned(b.size(), 16), 255);
    auto header = b.size();
    append(b, slice(old, p, old.size() - p));
    for (unsigned c = 0; c < 3; ++c) {
        auto at = header + 16 + c * 4;
        auto id = u32(b, at);
        if (id)
            put32(b, at, tables.at(id));
    }
    auto stream = slice(b, header + 32, u32(b, header));
    for (auto c : commands(stream))
        if (c.reg == 0x105 && (c.mask & 4))
            put32(b, header + 32 + c.offset, (c.value & ~0xff0000u) | (unsigned(stencil) << 16));
    return section("material", b);
}
Bytes renamed_mesh(View old, const std::string &mesh, const std::string &material) {
    Bytes b(old.begin(), old.begin() + 144);
    put32(b, 16, hash(mesh));
    std::fill(b.begin() + 20, b.begin() + 84, 0);
    std::copy(mesh.begin(), mesh.end(), b.begin() + 20);
    auto count = u32(old, 120);
    std::size_t p = 144;
    for (unsigned i = 0; i < count * 3; ++i) {
        auto length = 16 + u32(old, p);
        append(b, slice(old, p, length));
        p += length;
    }
    for (unsigned i = 0; i < count; ++i) {
        auto length = u32(old, p + 4);
        p += 8 + length;
        auto padded = aligned(material.size() + 1, 16);
        append32(b, hash(material));
        append32(b, narrow(padded));
        auto at = b.size();
        b.resize(at + padded);
        std::copy(material.begin(), material.end(), b.begin() + at);
        append(b, slice(old, p, 48));
        p += 48;
    }
    append(b, slice(old, p, old.size() - p));
    b.resize(aligned(b.size(), 16));
    put32(b, 8, narrow(b.size() - 16));
    return b;
}
Bytes metadata(const Model &model, const std::array<std::vector<std::string>, 4> &names,
               const std::vector<Bytes> &imported_tables) {
    Bytes b;
    std::size_t old = 32;
    for (unsigned table = 0; table < 4; ++table) {
        append32(b, narrow(names[table].size()));
        for (auto &name : names[table]) {
            auto it = std::find(model.names[table].begin(), model.names[table].end(), name);
            if (it != model.names[table].end())
                append(b, slice(model.original,
                                old + 4 + std::size_t(it - model.names[table].begin()) * 68, 68));
            else {
                auto at = b.size();
                b.resize(at + 68);
                put32(b, at, hash(name));
                std::copy(name.begin(), name.end(), b.begin() + at + 4);
            }
        }
        old += 4 + model.names[table].size() * 68;
    }
    auto tail = slice(model.original, model.bounds_offset,
                      model.sections[0].offset + model.sections[0].size - model.bounds_offset);
    std::size_t end = 112 + u32(tail, 96) + u32(tail, 100);
    auto joints = u32(tail, end);
    end += 16;
    for (unsigned i = 0; i < joints; ++i) {
        end += 1 + slice(tail, end, 1)[0];
        end += 1 + slice(tail, end, 1)[0];
        end += 37;
        slice(tail, 0, end);
    }
    auto old_prefix = model.bounds_offset - 32, footer = aligned(old_prefix + end, 16) - old_prefix;
    append(b, slice(tail, 0, end));
    b.resize(aligned(b.size(), 16));
    if (imported_tables.empty()) {
        if (footer < tail.size())
            append(b, slice(tail, footer, tail.size() - footer));
    } else {
        auto tables = lookup_records(model);
        auto length = tables.length;
        for (auto &record : imported_tables)
            length = std::max(length, narrow(record.size() - 16));
        require(tables.records.size() + imported_tables.size() <= 4096, "Too many lighting tables");
        auto header = b.size();
        append(b, slice(model.original, tables.offset, 16));
        put32(b, header, narrow(tables.records.size() + imported_tables.size()));
        put32(b, header + 4, length);
        auto append_tables = [&](const std::vector<Bytes> &group) {
            for (auto &record : group) {
                auto at = b.size();
                append(b, record);
                b.resize(at + 16 + length, 0);
            }
        };
        append_tables(tables.records);
        append_tables(imported_tables);
        auto limit = model.sections.front().offset + model.sections.front().size;
        if (tables.end < limit)
            append(b, slice(model.original, tables.end, limit - tables.end));
    }
    return section("gfmodel", b);
}
std::map<unsigned, Bytes> motion_sections(View b) {
    std::map<unsigned, Bytes> out;
    if (b.empty())
        return out;
    require(u32(b, 0) == 0x60000, "Unsupported loop motion");
    for (unsigned i = 0; i < u32(b, 4); ++i) {
        auto at = 8 + i * 12;
        auto v = slice(b, u32(b, at + 8), u32(b, at + 4));
        require(out.emplace(u32(b, at), Bytes(v.begin(), v.end())).second,
                "Duplicate motion section");
    }
    return out;
}
Bytes merge_loop(View old, View donor, const std::map<std::string, std::string> &material_names,
                 const std::map<std::string, std::string> &texture_names) {
    auto sections = motion_sections(old), source = motion_sections(donor);
    auto added = decode_material_motion(donor);
    std::erase_if(added.tracks, [&](auto &t) {
        return !material_names.contains(t.material);
    });
    require(!added.tracks.empty(), "Donor loop has no tracks for the selected materials");
    for (auto &t : added.tracks) {
        t.material = material_names.at(t.material);
        for (auto &k : t.textures)
            k.texture = texture_names.at(k.texture);
    }
    MaterialMotion combined = old.empty() ? MaterialMotion{} : decode_material_motion(old);
    auto old_frames = unsigned(combined.frames), new_frames = unsigned(added.frames);
    unsigned frames = old_frames ? std::lcm(old_frames, new_frames) : new_frames;
    require(frames > 0 && frames <= 4096, "Combined effect loop exceeds 4096 frames");
    if (old_frames && old_frames != frames)
        for (auto &[type, data] : sections)
            require(type == 0 || type == 3 || type == 4 || type == 5,
                    "Existing loop has non-material channels with a different duration");
    auto extend = [&](MaterialMotion &motion) {
        if (unsigned(motion.frames) == frames)
            return;
        for (auto &t : motion.tracks) {
            for (auto &curve : t.curves)
                if (curve.keys.size() > 1) {
                    auto original = curve;
                    curve.keys.clear();
                    for (unsigned f = 0; f <= frames; ++f) {
                        auto at = float(f % unsigned(motion.frames));
                        curve.keys.push_back({float(f), original.sample(at, 0), 0});
                    }
                }
            if (!t.textures.empty()) {
                auto keys = t.textures;
                t.textures.clear();
                for (unsigned f = 0; f < frames; f += unsigned(motion.frames))
                    for (auto k : keys)
                        if (k.frame < unsigned(motion.frames)) {
                            k.frame += f;
                            t.textures.push_back(k);
                        }
            }
        }
        motion.frames = float(frames);
    };
    if (old_frames)
        extend(combined);
    extend(added);
    combined.frames = float(frames);
    combined.looping = true;
    combined.tracks.insert(combined.tracks.end(), added.tracks.begin(), added.tracks.end());
    if (sections.empty())
        sections[0] = source.at(0);
    put32(sections[0], 0, frames);
    put16(sections[0], 4, u16(sections[0], 4) | 1);
    for (auto [type, kind] : {std::pair{3u, MaterialTrack::Kind::TextureTransform},
                              {4u, MaterialTrack::Kind::TexturePattern},
                              {5u, MaterialTrack::Kind::ConstantColor}}) {
        if (std::any_of(combined.tracks.begin(), combined.tracks.end(), [&](auto &t) {
                return t.kind == kind;
            }))
            sections[type] = encode_material_tracks(combined, kind);
    }
    Bytes b(8 + sections.size() * 12);
    put32(b, 0, 0x60000);
    put32(b, 4, narrow(sections.size()));
    unsigned i = 0;
    for (auto &[type, data] : sections) {
        auto at = 8 + i++ * 12;
        put32(b, at, type);
        put32(b, at + 4, narrow(data.size()));
        put32(b, at + 8, narrow(b.size()));
        append(b, data);
    }
    decode_material_motion(b);
    return b;
}
}
MaterialEffectResult borrow_material_effect(const ModelDocument &target,
                                            std::size_t target_material, const ModelDocument &donor,
                                            const std::vector<std::size_t> &donor_materials,
                                            bool keep_original) {
    require(target.area < 0 && donor.area < 0,
            "Material effect bundles currently support Pokemon models");
    require(!donor_materials.empty(), "Select at least one donor material pass");
    require(donor_materials.size() <= 8, "Select at most eight material passes");
    std::set<std::size_t> selected(donor_materials.begin(), donor_materials.end());
    require(selected.size() == donor_materials.size(), "Duplicate material pass");
    MaterialEffectResult result;
    auto target_name = target.scene->materials.at(target_material).name;
    auto target_link = target.resources.at(target.material_resources.at(target_material));
    auto donor_link = donor.resources.at(donor.material_resources.at(donor_materials.front()));
    auto model = Model::parse(source_resource(target, target_link));
    auto donor_model = Model::parse(source_resource(donor, donor_link));
    require(target_link.path == std::vector<std::size_t>{0}, "Unsupported model container layout");
    auto model_member = target.sources.at(target_link.source).member;
    auto pack = Container::parse(target.sources.at(target_link.source).original, "PC");
    auto names = model.names;
    std::set<std::string> used;
    for (auto &table : names)
        used.insert(table.begin(), table.end());
    for (auto &m : target.scene->materials) {
        used.insert(m.vertex_shader);
        used.insert(m.fragment_shader);
    }
    auto prefix = "effect_" + sha256(donor_model.original).substr(0, 8) + "_";
    std::map<std::string, std::string> materials, textures, shaders;
    std::map<std::string, Bytes> images, shader_bytes;
    std::map<std::string, bool> vertex_shaders;
    std::vector<ModelSection> donor_sections;
    for (auto &s : donor_model.sections)
        if (s.kind == "material")
            donor_sections.push_back(s);
    auto existing_tables = lookup_records(model), available_tables = lookup_records(donor_model);
    std::set<std::uint32_t> used_hashes;
    for (auto &record : existing_tables.records)
        used_hashes.insert(u32(record, 0));
    for (auto &name : names[1])
        used_hashes.insert(hash(name));
    std::map<std::uint32_t, std::uint32_t> table_ids;
    std::vector<Bytes> imported_tables;
    auto import_texture = [&](const std::string &name) {
        if (textures.contains(name))
            return;
        auto resource = donor.texture_resources.find(donor.texture_prefix + name);
        require(resource != donor.texture_resources.end(),
                "Donor texture has no source resource: " + name);
        auto bytes = source_resource(donor, donor.resources.at(resource->second));
        auto imported = unique_name(prefix, name, used);
        while (!hash(imported) || used_hashes.contains(hash(imported)))
            imported = unique_name(prefix, name, used);
        used_hashes.insert(hash(imported));
        std::fill(bytes.begin() + 40, bytes.begin() + 104, 0);
        std::copy(imported.begin(), imported.end(), bytes.begin() + 40);
        decode_field_texture(bytes);
        textures[name] = imported;
        images[imported] = std::move(bytes);
    };
    auto import_table = [&](std::uint32_t id) {
        if (!id || table_ids.contains(id))
            return;
        auto found = std::find_if(available_tables.records.begin(), available_tables.records.end(),
                                  [&](auto &record) {
                                      return u32(record, 0) == id;
                                  });
        require(found != available_tables.records.end(),
                "Donor lighting table is missing: " + std::to_string(id));
        auto named =
            std::find_if(donor_model.names[1].begin(), donor_model.names[1].end(), [&](auto &name) {
                return hash(name) == id;
            });
        std::string imported;
        if (named != donor_model.names[1].end() &&
            donor.texture_resources.contains(donor.texture_prefix + *named)) {
            import_texture(*named);
            imported = textures.at(*named);
        } else {
            auto original =
                named == donor_model.names[1].end() ? "lookup_" + std::to_string(id) : *named;
            imported = unique_name(prefix, original, used);
            while (!hash(imported) || used_hashes.contains(hash(imported)))
                imported = unique_name(prefix, original, used);
            used_hashes.insert(hash(imported));
            names[1].push_back(imported);
        }
        auto bytes = *found;
        table_ids[id] = hash(imported);
        put32(bytes, 0, hash(imported));
        imported_tables.push_back(std::move(bytes));
    };
    std::set<unsigned> stencil_used;
    for (auto &m : target.scene->materials)
        if (m.stencil_test & 1)
            stencil_used.insert((m.stencil_test >> 16) & 255);
    unsigned stencil = 1;
    while (stencil_used.contains(stencil) && stencil < 256)
        ++stencil;
    require(stencil < 256, "No free stencil reference for the effect");
    std::set<unsigned> donor_stencils;
    for (auto index : donor_materials) {
        auto &m = donor.scene->materials.at(index);
        require(m.combiner.present && m.combiner.unsupported.empty() && !m.unsupported_mapping,
                "Donor material has unsupported preview inputs");
        require(!m.height_tint && !m.screen_refraction,
                "This effect needs geometry-specific vertex parameters");
        if (m.stencil_test & 1)
            donor_stencils.insert((m.stencil_test >> 16) & 255);
        materials[m.name] = unique_name(prefix, m.name, used);
        result.materials.push_back(materials.at(m.name));
        for (auto &shader : {m.vertex_shader, m.fragment_shader})
            if (!shaders.contains(shader)) {
                auto bytes = source_resource(donor, shader_link(donor, shader));
                auto name = unique_name(prefix, shader, used);
                require(bytes.size() >= 104 && text(slice(bytes, 16, 8)) == "shader",
                        "Unsupported donor shader resource");
                std::fill(bytes.begin() + 32, bytes.begin() + 96, 0);
                std::copy(name.begin(), name.end(), bytes.begin() + 32);
                put32(bytes, 96, hash(name));
                shaders[shader] = name;
                shader_bytes[name] = std::move(bytes);
                vertex_shaders[name] = shader == m.vertex_shader;
            }
        auto &section = donor_sections.at(index);
        auto material = slice(donor_model.original, section.offset, section.size);
        auto meta = material_metadata(material);
        for (unsigned c = 0; c < 3; ++c)
            import_table(u32(material, meta + c * 4));
        auto at = meta + 168;
        auto count = u32(material, at);
        at += 4;
        for (unsigned i = 0; i < count; ++i)
            at += 5 + slice(material, at + 4, 1)[0] + 42;
        at = aligned(at, 16);
        for (unsigned c = 0; c < 3; ++c)
            import_table(u32(material, at + 16 + c * 4));
        for (auto &texture : m.texture_inputs)
            if (!texture.empty())
                import_texture(texture.substr(donor.texture_prefix.size()));
    }
    require(donor_stencils.size() <= 1, "Select the passes for one stencil effect at a time");
    for (auto &[old, name] : shaders)
        if (std::find(donor_model.names[0].begin(), donor_model.names[0].end(), old) !=
            donor_model.names[0].end())
            names[0].push_back(name);
    for (auto &[old, name] : textures)
        names[1].push_back(name);
    for (auto &name : result.materials)
        names[2].push_back(name);
    std::vector<Bytes> mats, meshes;
    for (auto &s : model.sections)
        if (s.kind == "material")
            mats.emplace_back(model.original.begin() + s.offset,
                              model.original.begin() + s.offset + s.size);
    for (auto index : donor_materials) {
        auto &s = donor_sections.at(index);
        mats.push_back(rename_material(slice(donor_model.original, s.offset, s.size), materials,
                                       shaders, textures, table_ids, std::uint8_t(stencil)));
    }
    names[3].clear();
    unsigned replaced = 0;
    for (auto &s : model.sections)
        if (s.kind == "mesh") {
            auto raw = slice(model.original, s.offset, s.size);
            auto mesh = text(slice(raw, 20, 64));
            bool match = false, other = false;
            for (auto &draw : target.scene->draws)
                if (draw.mesh == mesh) {
                    match |= draw.material == target_material;
                    other |= draw.material != target_material;
                }
            if (!match) {
                names[3].push_back(mesh);
                meshes.emplace_back(raw.begin(), raw.end());
                continue;
            }
            require(!other, "This mesh contains several materials; isolate its material submeshes "
                            "before borrowing an effect");
            if (donor_materials.size() > 1 || keep_original)
                for (auto &motion : target.motions)
                    for (auto &track : motion.visibility.tracks)
                        require(track.mesh != mesh,
                                "This mesh has visibility animation; duplicating its visibility "
                                "tracks is not supported yet");
            ++replaced;
            if (keep_original) {
                names[3].push_back(mesh);
                meshes.emplace_back(raw.begin(), raw.end());
            }
            bool first = !keep_original;
            for (auto index : donor_materials) {
                auto imported = first ? mesh : unique_name(prefix, mesh, used);
                names[3].push_back(imported);
                meshes.push_back(
                    renamed_mesh(raw, imported, materials.at(donor.scene->materials[index].name)));
                first = false;
            }
        }
    require(replaced > 0, "The selected target material has no meshes");
    Bytes assembled(model.original.begin(), model.original.begin() + 16);
    put32(assembled, 4, narrow(1 + mats.size() + meshes.size()));
    append(assembled, metadata(model, names, imported_tables));
    for (auto &b : mats)
        append(assembled, b);
    for (auto &b : meshes)
        append(assembled, b);
    assembled.resize(aligned(assembled.size(), 128));
    auto check = Model::parse(assembled);
    require(check.bones == model.bones, "Effect import changed the skeleton");
    model_lighting_tables(check);
    pack.files[0] = std::move(assembled);
    auto built = pack.write(128);
    for (auto &[name, bytes] : shader_bytes) {
        auto &reference = shader_link(
            target, vertex_shaders.at(name) ? target.scene->materials.front().vertex_shader
                                            : target.scene->materials.front().fragment_shader);
        auto path = reference.path;
        require(!path.empty(), "Shader resource has no containing pack");
        path.pop_back();
        auto parent = Container::parse(asset_resource(built, path));
        parent.files.push_back(bytes);
        built = replace_asset_resource(built, path, parent.write(128));
    }
    result.members[model_member] = std::move(built);
    Archive archive(target.archive_sources.resolve(target.dump, TargetProfile::pokemon_archive));
    for (unsigned variant : {1u, 2u}) {
        auto member = target.pokemon.texture_member + variant;
        auto found = std::find_if(target.sources.begin(), target.sources.end(), [&](auto &s) {
            return s.member == member;
        });
        auto textures_pack = Container::parse(
            found == target.sources.end() ? archive.decoded(member) : found->original, "PC");
        for (auto &[name, bytes] : images)
            textures_pack.files.push_back(bytes);
        result.members[member] = textures_pack.write(128);
    }
    unsigned loops = 0;
    for (unsigned group = 0; group < 4; ++group) {
        auto donor_motion = std::find_if(donor.motions.begin(), donor.motions.end(), [&](auto &m) {
            return m.group == group && m.slot == pokemon_main_motion_counts[group] + 7 &&
                   m.error.empty();
        });
        if (donor_motion == donor.motions.end())
            continue;
        bool relevant = std::any_of(donor_motion->material.tracks.begin(),
                                    donor_motion->material.tracks.end(), [&](auto &t) {
                                        return materials.contains(t.material);
                                    });
        if (!relevant)
            continue;
        auto member = target.pokemon.motion_member + TargetProfile::pokemon_motion_slots[group];
        auto found = std::find_if(target.sources.begin(), target.sources.end(), [&](auto &s) {
            return s.member == member;
        });
        require(found != target.sources.end(), "Missing target motion set");
        auto motion_pack = Container::parse(found->original, "PC");
        auto slot = pokemon_main_motion_counts[group] + 7;
        require(slot < motion_pack.files.size(),
                "Target motion pack lacks its looping-effect slot");
        auto bytes = source_resource(donor, donor.resources.at(donor_motion->resource));
        motion_pack.files[slot] = merge_loop(motion_pack.files[slot], bytes, materials, textures);
        result.members[member] = motion_pack.write(128);
        ++loops;
    }
    result.summary = "Imported " + std::to_string(donor_materials.size()) + " material passes, " +
                     std::to_string(images.size()) + " textures, " +
                     std::to_string(imported_tables.size()) + " lighting tables and " +
                     std::to_string(loops) + " looping motions onto " + target_name + ".";
    return result;
}
}
