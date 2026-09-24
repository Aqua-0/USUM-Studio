#include "assets/asset_package.h"
#include "assets/model_library.h"
#include "core/digest.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <set>
namespace studio {
namespace {
const std::string magic = "USUMSTUDIO_ASSET 2\n";
Bytes string_bytes(const std::string &s) {
    return Bytes(s.begin(), s.end());
}
void valid_name(const std::string &name) {
    require(!name.empty() && name.size() <= 240 && name.front() != '/' &&
                name.find("..") == std::string::npos &&
                name.find_first_of("\\:\r\n\0", 0, 5) == std::string::npos,
            "Invalid asset package entry name");
}
}
bool is_asset_package(View bytes) {
    return bytes.size() >= 17 && text(bytes.first(17)) == "USUMSTUDIO_ASSET ";
}
Bytes encode_asset_package(const AssetPackage &package) {
    require(!package.empty() && package.size() <= 10000, "Invalid asset package entry count");
    Bytes out(magic.begin(), magic.end());
    append32(out, narrow(package.size()));
    for (const auto &[name, data] : package) {
        valid_name(name);
        append32(out, narrow(name.size()));
        append32(out, narrow(data.size()));
        append(out, string_bytes(sha256(data)));
        append(out, string_bytes(name));
        append(out, data);
    }
    return out;
}
AssetPackage decode_asset_package(View bytes) {
    require(bytes.size() >= magic.size() + 4 && text(bytes.first(magic.size())) == magic,
            "Re-export this older asset package from Authoring to use it in Blender");
    std::size_t p = magic.size();
    const auto count = u32(bytes, p);
    p += 4;
    require(count > 0 && count <= 10000, "Invalid asset package entry count");
    AssetPackage out;
    for (unsigned i = 0; i < count; ++i) {
        const auto n = u32(bytes, p), size = u32(bytes, p + 4);
        p += 8;
        require(n > 0 && n <= 240, "Invalid asset package entry name length");
        const auto digest = text(slice(bytes, p, 64));
        p += 64;
        const auto name = text(slice(bytes, p, n));
        p += n;
        valid_name(name);
        const auto data = slice(bytes, p, size);
        p += size;
        require(sha256(data) == digest, "Asset package checksum failed: " + name);
        require(out.emplace(name, Bytes(data.begin(), data.end())).second,
                "Duplicate asset package entry");
    }
    require(p == bytes.size() && out.contains("type") && out.contains("name"),
            "Incomplete asset package or trailing data");
    return out;
}
Bytes compile_static_package(const AssetPackage &package) {
    auto object = Container::parse(package.at("static-resource"), "SM");
    auto pack = ModelPack::parse(object.files.at(1));
    std::istringstream paths(text(package.at("model-paths")));
    std::size_t index, resource;
    std::set<std::size_t> seen;
    while (paths >> index) {
        require(bool(paths >> resource), "Incomplete asset model mapping");
        require(resource < pack.resources.size() && pack.resources[resource].category == 0 &&
                    seen.insert(resource).second,
                "Invalid asset model resource mapping");
        auto &native = pack.resources[resource].bytes;
        const auto exchange = parse_model_exchange(
            text(package.at("models/" + std::to_string(index) + ".usum-model")));
        const auto edited = replace_model_exchange(native, exchange);
        if (edited != native)
            object.files[1] = ModelPack::parse(object.files[1]).replace(resource, edited);
    }
    require(paths.eof() && !seen.empty(), "Invalid asset model list");
    return object.write(TargetProfile::resource_alignment);
}
void add_package_model(AssetPackage &package, const MaterialDocument &document, std::size_t index) {
    auto exchange = document.model_exchange();
    const auto preview = document.preview_model();
    const auto key = "models/" + std::to_string(index) + ".usum-model";
    std::ostringstream metadata;
    metadata << "USUMSTUDIO_PREVIEW 1\n"
             << std::quoted(exchange.source) << ' ' << exchange.meshes.size() << '\n'
             << std::setprecision(9);
    for (std::size_t i = 0; i < exchange.meshes.size(); ++i) {
        const auto draw = preview.native_meshes.empty()
                              ? i
                              : std::size_t(std::find(preview.native_meshes.begin(),
                                                      preview.native_meshes.end(), i) -
                                            preview.native_meshes.begin());
        const auto &material = preview.scene->materials.at(preview.scene->draws.at(draw).material);
        const auto &uv = material.inputs[0];
        metadata << uv.source << ' ' << uv.wrap_u << ' ' << uv.wrap_v << ' ' << uv.mag_filter << ' '
                 << material.alpha_function << ' ' << material.alpha_reference << ' '
                 << material.blend_state;
        for (auto v : uv.row_u)
            metadata << ' ' << v;
        for (auto v : uv.row_v)
            metadata << ' ' << v;
        metadata << '\n';
        if (auto texture = preview.scene->textures.find(material.texture);
            texture != preview.scene->textures.end()) {
            const auto image =
                "textures/" + std::to_string(index) + "-" + std::to_string(i) + ".tga";
            package["models/" + image] = write_tga(texture->second);
            exchange.meshes[i].texture = image;
        }
    }
    package[key] = string_bytes(serialize_model_exchange(exchange));
    package[key + ".preview"] = string_bytes(metadata.str());
}
AssetPackage model_asset_package(const MaterialDocument &document) {
    AssetPackage out;
    out["type"] = string_bytes(document.model.project_asset  ? "static"
                               : document.model.is_pokemon() ? "pokemon"
                                                             : "model");
    out["name"] = string_bytes(document.model.name);
    if (document.model.is_pokemon()) {
        const auto &p = document.model.pokemon;
        out["pokemon-identity"] = string_bytes(std::to_string(p.species) + " " + std::to_string(p.form) + " " +
            std::to_string(p.female) + " " + std::to_string(document.model.shiny));
    }
    auto baked = document.package_members();
    auto snapshot = document.model;
    for (std::size_t i = 0; i < snapshot.sources.size(); ++i) {
        snapshot.sources[i].original = baked.at(i);
        snapshot.sources[i].hash = sha256(baked.at(i));
        if (snapshot.sources[i].role != "Catalog")
            out["native/" + std::to_string(i)] = baked.at(i);
    }
    std::ostringstream sources;
    for (std::size_t i = 0; i < snapshot.sources.size(); ++i)
        if (snapshot.sources[i].role != "Catalog")
            sources << i << ' ' << std::quoted(snapshot.sources[i].role) << '\n';
    out["sources"] = string_bytes(sources.str());
    const auto &link = snapshot.resources.at(snapshot.material_resources.front());
    std::ostringstream selected;
    selected << link.path.size();
    for (auto n : link.path)
        selected << ' ' << n;
    out["model-path"] = string_bytes(selected.str());
    if (document.model.project_asset) {
        out["static-resource"] = baked.at(0);
        out["model-paths"] = string_bytes("0 " + std::to_string(link.path.at(1)) + "\n");
    }
    MaterialDocument baked_document(reload_editable_model(snapshot, {}));
    add_package_model(
        out, document.model.area >= 0 && !document.model.project_asset ? document : baked_document,
        0);
    return out;
}
void import_model_asset_package(MaterialDocument &document, const AssetPackage &package) {
    const auto kind = text(package.at("type"));
    require(kind == (document.model.project_asset  ? "static"
                     : document.model.is_pokemon() ? "pokemon"
                                                   : "model"),
            "Choose a matching model destination for this asset");
    if (document.model.area >= 0 && !document.model.project_asset) {
        document.import_model_exchange(
            parse_model_exchange(text(package.at("models/0.usum-model"))));
        return;
    }
    if (document.model.project_asset) {
        const auto resource = compile_static_package(package);
        auto next = document;
        next.import_native_members({{document.model.sources.front().member, resource}});
        document = std::move(next);
        return;
    }
    const auto &link = document.model.resources.at(document.model.material_resources.front());
    std::ostringstream selected;
    selected << link.path.size();
    for (auto n : link.path)
        selected << ' ' << n;
    require(text(package.at("model-path")) == selected.str(),
            "Select the same model or shadow slot as the exported asset");
    std::map<std::size_t, Bytes> members;
    std::istringstream sources(text(package.at("sources")));
    std::size_t i;
    std::string role;
    std::set<std::size_t> used;
    while (sources >> i) {
        require(bool(sources >> std::quoted(role)), "Incomplete asset resource mapping");
        auto found = std::find_if(
            document.model.sources.begin(), document.model.sources.end(), [&](const auto &s) {
                return s.role != "Catalog" && s.role == role &&
                       !used.contains(std::size_t(&s - document.model.sources.data()));
            });
        require(found != document.model.sources.end(),
                "Destination lacks the asset resource: " + role);
        used.insert(std::size_t(found - document.model.sources.begin()));
        require(found->archive == document.archive_path(),
                "Import this asset through its original resource type");
        require(members.emplace(found->member, package.at("native/" + std::to_string(i))).second,
                "Ambiguous asset resource destination");
    }
    require(sources.eof() && used.size() == std::count_if(document.model.sources.begin(),
                                                          document.model.sources.end(),
                                                          [](const auto &source) {
                                                              return source.role != "Catalog";
                                                          }),
            "Asset and destination resource sets differ");
    auto &native = members.at(document.model.sources.at(link.source).member);
    const auto edited =
        replace_model_exchange(asset_resource(native, link.path),
                               parse_model_exchange(text(package.at("models/0.usum-model"))));
    native = replace_asset_resource(native, link.path, edited);
    auto next = document;
    next.import_native_members(members);
    document = std::move(next);
}
}
