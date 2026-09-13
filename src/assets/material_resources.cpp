#include "assets/material_document.h"
#include "assets/model_resources.h"
#include "assets/material_motion.h"
#include "scene/model_decoder.h"
#include <algorithm>
namespace studio {
namespace {
const AssetResourceLink &model_link(const ModelDocument &model) {
    require(model.area < 0 && !model.clothing,
            "Send an individual model to Studio to manage its resources");
    return model.resources.at(model.material_resources.front());
}
struct ResourceEdit {
    const ModelDocument &model;
    std::map<std::size_t, Bytes> members;
    Bytes &member(std::size_t index) {
        if (!members.contains(index)) {
            auto source = std::find_if(model.sources.begin(), model.sources.end(), [&](auto &s) {
                return s.member == index;
            });
            members[index] = source != model.sources.end()
                                 ? source->original
                                 : Archive(model.archive_sources.resolve(
                                               model.dump, TargetProfile::pokemon_archive))
                                       .decoded(index);
        }
        return members.at(index);
    }
    Bytes read(const AssetResourceLink &link) {
        auto &source = model.sources.at(link.source);
        auto found = members.find(source.member);
        return asset_resource(found == members.end() ? source.original : found->second, link.path);
    }
    void write(const AssetResourceLink &link, View bytes) {
        auto &b = member(model.sources.at(link.source).member);
        b = replace_asset_resource(b, link.path, bytes);
    }
};
void update_motions(ResourceEdit &edit, const std::string &material, const std::string &texture,
                    const std::string &replacement) {
    for (auto &motion : edit.model.motions) {
        auto &link = edit.model.resources.at(motion.resource);
        if (!motion.error.empty()) {
            auto bytes = edit.read(link);
            auto name = material.empty() ? texture : material;
            require(std::search(bytes.begin(), bytes.end(), name.begin(), name.end()) ==
                        bytes.end(),
                    "A motion using this resource could not be decoded; repair it before removing "
                    "the resource");
            continue;
        }
        auto changed = motion.material;
        if (!material.empty())
            std::erase_if(changed.tracks, [&](auto &t) {
                return t.material == material;
            });
        else
            for (auto &t : changed.tracks)
                for (auto &key : t.textures)
                    if (key.texture == texture) {
                        require(!replacement.empty(),
                                "Choose a replacement for this animated texture");
                        key.texture = replacement;
                    }
        if (changed == motion.material)
            continue;
        auto bytes = edit.read(link);
        edit.write(link, replace_material_motion(bytes, changed));
    }
}

Bytes texture_header() {
    Bytes b(128);
    put32(b, 0, 0x15041213);
    put32(b, 4, 1);
    std::copy_n("texture", 7, b.begin() + 8);
    put32(b, 20, 0xffffffff);
    return b;
}
}
void MaterialDocument::add_material(std::size_t donor, const std::string &name) {
    auto snapshot = preview_model();
    ResourceEdit edit{snapshot, compiled_members()};
    auto link = model_link(snapshot);
    auto bytes = copy_model_material(edit.read(link), donor, name);
    edit.write(link, bytes);
    replace_members(edit.members);
}
void MaterialDocument::remove_material(std::size_t material, std::size_t replacement) {
    auto snapshot = preview_model();
    ResourceEdit edit{snapshot, compiled_members()};
    auto link = model_link(snapshot);
    auto name = snapshot.scene->materials.at(material).name;
    auto target = snapshot.scene->materials.at(replacement).name;
    auto bytes = remove_model_material(edit.read(link), material, target);
    edit.write(link, bytes);
    bool shared = false;
    if (snapshot.is_pokemon()) {
        auto pack = Container::parse(edit.member(snapshot.pokemon.model_member));
        auto other = Model::parse(pack.files.at(snapshot.shadow_model ? 0 : 1));
        shared =
            std::find(other.names[2].begin(), other.names[2].end(), name) != other.names[2].end();
        auto catalog = load_pokemon_catalog(snapshot.dump, snapshot.archive_sources);
        for (auto &entry : catalog)
            shared |= entry.motion_member == snapshot.pokemon.motion_member &&
                      entry.model_member != snapshot.pokemon.model_member;
    } else
        for (auto &resource : snapshot.resources)
            if (resource.role == "Model" && resource.path != link.path) {
                auto other = Model::parse(edit.read(resource));
                shared |= std::find(other.names[2].begin(), other.names[2].end(), name) !=
                          other.names[2].end();
            }
    if (!shared)
        update_motions(edit, name, {}, {});
    replace_members(edit.members);
}
void MaterialDocument::add_texture(const std::string &name, const TextureImage &image,
                                   TextureFormat format) {
    add_encoded_texture(name, encode_texture(texture_header(), image, name, format));
}
void MaterialDocument::add_encoded_texture(const std::string &name, View encoded) {
    require(text(slice(encoded, 40, 64)) == name, "New texture name changed");
    auto snapshot = preview_model();
    ResourceEdit edit{snapshot, compiled_members()};
    auto link = model_link(snapshot);
    auto full = snapshot.texture_prefix + name;
    require(!snapshot.texture_resources.contains(full), "A texture with this name already exists");
    decode_field_texture(encoded);
    auto bytes = change_model_texture(edit.read(link), name, {}, true);
    edit.write(link, bytes);
    if (snapshot.is_pokemon())
        for (unsigned variant : {1u, 2u}) {
            auto &member = edit.member(snapshot.pokemon.texture_member + variant);
            member = change_pack_texture(member, name, encoded);
        }
    else {
        auto path = link.path;
        require(!path.empty(), "The model has no resource pack");
        path.pop_back();
        auto &member = edit.member(snapshot.sources.at(link.source).member);
        auto pack = asset_resource(member, path);
        require(u32(pack, 0) == 0x10000, "This model has no editable texture pack");
        member = replace_asset_resource(member, path, change_pack_texture(pack, name, encoded));
    }
    replace_members(edit.members);
}
void MaterialDocument::remove_texture(const std::string &full,
                                      const std::string &replacement_full) {
    auto snapshot = preview_model();
    ResourceEdit edit{snapshot, compiled_members()};
    auto link = model_link(snapshot);
    require(snapshot.texture_resources.contains(full), "Choose a texture resource");
    require(replacement_full.empty() ||
                (replacement_full != full && snapshot.texture_resources.contains(replacement_full)),
            "Choose a different replacement texture");
    auto name = full.substr(snapshot.texture_prefix.size()),
         replacement = replacement_full.empty()
                           ? std::string{}
                           : replacement_full.substr(snapshot.texture_prefix.size());
    if (snapshot.is_pokemon()) {
        auto catalog = load_pokemon_catalog(snapshot.dump, snapshot.archive_sources);
        bool animated = false;
        for (auto &motion : snapshot.motions)
            for (auto &track : motion.material.tracks)
                for (auto &key : track.textures)
                    animated |= key.texture == name;
        for (auto &entry : catalog)
            if (animated && entry.motion_member == snapshot.pokemon.motion_member &&
                entry.texture_member != snapshot.pokemon.texture_member)
                throw std::runtime_error("Another Pokemon texture pack uses this shared texture "
                                         "animation; keep the resource");
        for (auto &entry : catalog)
            if (entry.texture_member == snapshot.pokemon.texture_member &&
                entry.model_member != snapshot.pokemon.model_member) {
                Archive archive(snapshot.archive_sources.resolve(snapshot.dump,
                                                                 TargetProfile::pokemon_archive));
                auto other = Container::parse(archive.decoded(entry.model_member));
                for (unsigned i : {0u, 1u})
                    if (i < other.files.size() && !other.files[i].empty()) {
                        auto model = Model::parse(other.files[i]);
                        require(std::find(model.names[1].begin(), model.names[1].end(), name) ==
                                    model.names[1].end(),
                                "Another Pokemon model shares this texture; replace its pixels or "
                                "keep the resource");
                    }
            }
        auto &member = edit.member(snapshot.pokemon.model_member);
        auto pack = Container::parse(member);
        for (unsigned i : {0u, 1u})
            if (i < pack.files.size() && !pack.files[i].empty()) {
                auto model = Model::parse(pack.files[i]);
                if (std::find(model.names[1].begin(), model.names[1].end(), name) !=
                    model.names[1].end())
                    member = replace_asset_resource(
                        member, {i}, change_model_texture(pack.files[i], name, replacement));
            }
        update_motions(edit, {}, name, replacement);
        for (unsigned variant : {1u, 2u}) {
            auto &texture = edit.member(snapshot.pokemon.texture_member + variant);
            if (!replacement.empty()) {
                auto pack = Container::parse(texture);
                require(std::any_of(pack.files.begin(), pack.files.end(),
                                    [&](auto &b) {
                                        return b.size() >= 128 &&
                                               text(slice(b, 40, 64)) == replacement;
                                    }),
                        "The replacement must exist in both normal and shiny texture packs");
            }
            texture = change_pack_texture(texture, name);
        }
    } else {
        auto texture_link = snapshot.resources.at(snapshot.texture_resources.at(full));
        for (auto &resource : snapshot.resources)
            if (resource.role == "Model") {
                auto bytes = edit.read(resource);
                auto model = Model::parse(bytes);
                if (std::find(model.names[1].begin(), model.names[1].end(), name) !=
                    model.names[1].end())
                    edit.write(resource, change_model_texture(bytes, name, replacement));
            }
        update_motions(edit, {}, name, replacement);
        auto path = texture_link.path;
        require(!path.empty(), "Texture has no containing pack");
        path.pop_back();
        auto &member = edit.member(snapshot.sources.at(texture_link.source).member);
        member = replace_asset_resource(member, path,
                                        change_pack_texture(asset_resource(member, path), name));
    }
    replace_members(edit.members);
}
}
