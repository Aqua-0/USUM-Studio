#include "assets/overworld_character_asset.h"
#include "assets/model_library.h"
#include "assets/motion_table.h"
#include "core/digest.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace studio {
namespace {
Bytes pack_resources(std::vector<ModelResource> resources) {
    std::stable_sort(resources.begin(), resources.end(), [](auto &a, auto &b) {
        return a.category < b.category;
    });
    std::array<unsigned, 5> counts{};
    for (const auto &r : resources) {
        require(r.category < counts.size() && !r.name.empty() && r.name.size() <= 255,
                "Invalid character resource");
        ++counts[r.category];
    }
    Bytes out(24 + resources.size() * 4);
    put32(out, 0, 0x10000);
    for (unsigned i = 0; i < 5; ++i)
        put32(out, 4 + i * 4, counts[i]);
    std::vector<std::size_t> fields;
    for (unsigned i = 0; i < resources.size(); ++i) {
        put32(out, 24 + i * 4, narrow(out.size()));
        out.push_back(std::uint8_t(resources[i].name.size()));
        out.insert(out.end(), resources[i].name.begin(), resources[i].name.end());
        fields.push_back(out.size());
        append32(out, 0);
    }
    for (unsigned i = 0; i < resources.size(); ++i) {
        out.resize(aligned(out.size(), 128));
        put32(out, fields[i], narrow(out.size()));
        append(out, resources[i].bytes);
    }
    out.resize(aligned(out.size(), 128));
    ModelPack::parse(out);
    return out;
}
Bytes motion_table(const std::vector<Bytes> &motions) {
    Bytes out(4 + motions.size() * 4);
    put32(out, 0, narrow(motions.size()));
    std::map<Bytes, unsigned> written;
    for (unsigned i = 0; i < motions.size(); ++i) {
        auto [it, fresh] = written.emplace(motions[i], narrow(out.size() - 4));
        put32(out, 4 + i * 4, it->second);
        if (fresh) {
            append(out, motions[i]);
            out.resize(aligned(out.size(), 4));
        }
    }
    return out;
}
}
bool can_convert_overworld_character(const ModelDocument &model) {
    return !model.shadow_model && !model.clothing && !model.material_resources.empty() &&
           std::all_of(model.material_resources.begin(), model.material_resources.end(),
                       [&](auto resource) { return resource == model.material_resources.front(); });
}
OverworldMotionSelection default_overworld_motions(const ModelDocument &model) {
    OverworldMotionSelection result;
    for (unsigned i = 0; i < model.motions.size(); ++i) {
        const auto &m = model.motions[i];
        if (!m.error.empty() || m.skeletal.tracks.empty())
            continue;
        if (result.idle < 0)
            result.idle = int(i);
        if (m.group == (model.is_pokemon() ? 2u : 0u) && m.slot == 0)
            result.idle = int(i);
        if (m.group == (model.is_pokemon() ? 2u : 0u) && m.slot == (model.is_pokemon() ? 2u : 1u))
            result.walk = int(i);
        if (m.group == (model.is_pokemon() ? 2u : 0u) && m.slot == (model.is_pokemon() ? 3u : 2u))
            result.run = int(i);
    }
    return result;
}
Bytes convert_overworld_character(const MaterialDocument &document, View behavior_donor,
                                const OverworldMotionSelection &selection, unsigned scale_percent) {
    require(can_convert_overworld_character(document.model), "Choose an individual native model");
    auto donor = Container::parse(behavior_donor, "CM");
    require(donor.files.size() == 6 && donor.files[3].size() >= 128,
            "Choose an NPC or Pokemon behavior donor");
    const auto type = u32(donor.files[3], 0);
    require(type == 1 || (type == 2 && donor.files[3].size() >= 132),
            "Choose an NPC or Pokemon behavior donor");
    if (type == 2) {
        require(document.model.is_pokemon() && document.model.pokemon.species > 0 &&
                    document.model.pokemon.species <= 65535,
                "Pokemon behavior needs a Pokemon identity. Choose an NPC donor for this model.");
        require(scale_percent >= 1 && scale_percent <= 1000,
                "Scale must be between 1 and 1000 percent");
    }
    auto snapshot = document.model;
    auto compiled = document.package_members();
    for (unsigned i = 0; i < snapshot.sources.size(); ++i) {
        snapshot.sources[i].original = compiled.at(i);
        snapshot.sources[i].hash = sha256(compiled.at(i));
    }
    auto baked = reload_editable_model(snapshot, {});

    auto resource = [&](std::size_t index) {
        const auto &r = baked.resources.at(index);
        return asset_resource(baked.sources.at(r.source).original, r.path);
    };
    auto motion = [&](int index) {
        if (index == -1) {
            Bytes rest(56);
            put32(rest, 0, 0x60000);
            put32(rest, 4, 1);
            put32(rest, 12, 36);
            put32(rest, 16, 20);
            put32(rest, 20, 1);
            put16(rest, 24, 1);
            put16(rest, 26, 1);
            auto native = resource(baked.material_resources.front());
            auto bounds = Model::parse(native).bounds_offset;
            for (unsigned i = 0; i < 6; ++i)
                put32(rest, 28 + i * 4, u32(native, bounds + (i < 3 ? i : i + 1) * 4));
            return rest;
        }
        require(index >= 0 && std::size_t(index) < baked.motions.size(),
                "Choose idle, walk and run motions");
        const auto &m = baked.motions[index];
        require(m.error.empty() && !m.skeletal.tracks.empty(),
                "Choose a supported skeletal motion");
        return resource(m.resource);
    };
    auto idle = motion(selection.idle), walk = motion(selection.walk), run = motion(selection.run);
    std::vector<ModelResource> resources;
    const auto model_index = baked.material_resources.at(0);
    const auto model = resource(model_index);
    Model::parse(model);
    resources.push_back({0, 0,
                         "overworld_model",
                         model, 0});
    for (unsigned i = 0; i < baked.resources.size(); ++i) {
        const auto &r = baked.resources[i];
        if (r.role == "Auxiliary" && !baked.is_pokemon())
            resources.push_back({2, 0, r.name, resource(i), 0});
        if (r.role == "Texture")
            resources.push_back({1, 0, r.name, resource(i), 0});
        if (r.role == "Shader") {
            auto data = resource(i);
            auto shader = decode_material_shader(data);
            bool vertex = false;
            for (const auto &[reg, value] : shader.registers)
                vertex |= reg >= 0x2cb && reg <= 0x2d2;
            resources.push_back({vertex ? 3u : 4u, 0, shader.name, std::move(data), 0});
        }
    }
    // Keep generic action slots valid without borrowing motions for another skeleton.
    auto slots = std::max(3u, u32(donor.files[1], 0));
    require(slots < 65536, "Invalid donor motion table");
    std::vector<Bytes> motions(slots, idle);
    motions[1] = walk;
    motions[2] = run;
    for (const auto &m : baked.motions)
        if (m.error.empty())
            motions.push_back(resource(m.resource));
    require(motions.size() < 65536, "Too many character motions");
    donor.files[0] = pack_resources(std::move(resources));
    donor.files[1] = motion_table(motions);
    // No donor joint constraints or motion-specific blend rules fit the new skeleton.
    donor.files[2] = Bytes(28);
    put32(donor.files[2], 0, 0x10000);
    put32(donor.files[2], 4, 1);
    put32(donor.files[2], 8, 5);
    put32(donor.files[2], 12, 8);
    put32(donor.files[2], 16, 20);
    if (type == 2) {
        put32(donor.files[3], 32, scale_percent);
        put32(donor.files[3], 128, baked.pokemon.species);
    } else {
        put32(donor.files[3], 4, 0);
    }
    donor.files[4].clear();
    donor.files[5].clear();
    return donor.write(TargetProfile::resource_alignment);
}
ModelDocument open_independent_asset(const AssetPackage &package,
                                     const std::filesystem::path &dump) {
    const auto kind = text(package.at("type"));
    ModelDocument result;
    if (kind == "pokemon") {
        ModelDocument source;
        source.dump = dump;
        source.pokemon.label = source.name = text(package.at("name"));
        source.pokemon.model_member = 1;
        source.pokemon.texture_member = 2;
        source.pokemon.motion_member = 10;
        if (package.contains("pokemon-identity")) {
            std::istringstream identity(text(package.at("pokemon-identity")));
            require(bool(identity >> source.pokemon.species >> source.pokemon.form >>
                         source.pokemon.female >> source.shiny),
                    "Invalid Pokemon asset identity");
        }
        source.sources.push_back({TargetProfile::pokemon_archive, 0, "Catalog", "", {}});
        std::istringstream list(text(package.at("sources")));
        std::size_t index;
        std::string role;
        while (list >> index >> std::quoted(role)) {
            unsigned member = 0;
            if (role == "Model and shaders")
                member = 1;
            else if (role == "Normal textures" || role == "Shiny textures") {
                source.shiny = role == "Shiny textures";
                member = source.shiny ? 4 : 3;
            } else if (role == "Refresh interaction regions")
                member = 2 + TargetProfile::pokemon_refresh_texture_slot;
            else
                for (unsigned group = 0; group < 4; ++group)
                    if (role ==
                        std::string(TargetProfile::pokemon_motion_names[group]) + " motions")
                        member = 10 + TargetProfile::pokemon_motion_slots[group];
            if (member) {
                const auto &bytes = package.at("native/" + std::to_string(index));
                source.sources.push_back(
                    {TargetProfile::pokemon_archive, member, role, sha256(bytes), bytes});
            }
        }
        require(list.eof(), "Invalid asset source list");
        for (unsigned member : {1u, source.shiny ? 4u : 3u, 14u, 15u, 16u, 17u})
            require(std::any_of(source.sources.begin(), source.sources.end(),
                                [&](auto &s) {
                                    return s.member == member;
                                }),
                    "Pokemon asset is missing native resources");
        result = reload_pokemon(source, {});
    } else {
        require(kind == "model", "Open a native Pokemon or character asset. New Blender geometry "
                                 "needs game materials from a template first.");
        std::istringstream list(text(package.at("sources")));
        std::size_t index;
        std::string role;
        Bytes bytes;
        while (list >> index >> std::quoted(role))
            if (role == "Model resource")
                bytes = package.at("native/" + std::to_string(index));
        require(!bytes.empty() && bytes[0] == 'C' && bytes[1] == 'M',
                "Choose a character model asset");
        std::optional<std::size_t> selected;
        if (package.contains("model-path")) {
            std::istringstream path(text(package.at("model-path")));
            std::size_t count, pack, index;
            require(bool(path >> count >> pack >> index) && count == 2 && pack == 0,
                    "Choose an individual character model asset");
            selected = index;
        }
        result = load_library_model(dump, dump / TargetProfile::character_archive,
                                    ModelCategory::FieldCharacters, {0, text(package.at("name"))},
                                    nullptr, "model/", &bytes, selected);
    }
    result.independent_asset = sha256(encode_asset_package(package));
    MaterialDocument edited(result);
    edited.import_model_exchange(parse_model_exchange(text(package.at("models/0.usum-model"))));
    result = edited.model;
    return result;
}
}
