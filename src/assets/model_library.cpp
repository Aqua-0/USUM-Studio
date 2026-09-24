#include "assets/battle_effects.h"
#include "assets/motion_table.h"
#include "assets/model_library.h"
#include "assets/material_document.h"
#include "scene/model_decoder.h"
#include "core/digest.h"
namespace studio {
const char *model_category_archive(ModelCategory category) {
    if (category == ModelCategory::FieldCharacters)
        return TargetProfile::character_archive;
    if (category == ModelCategory::BattleArenas)
        return TargetProfile::battle_arenas_archive;
    return TargetProfile::battle_trainers_archive;
}
bool model_category_matches(ModelCategory category, const std::string &name) {
    bool ball = name.size() == 9 && name.starts_with("ob02") && name.ends_with("_00") &&
                name[4] >= '0' && name[4] <= '9' && name[5] >= '0' && name[5] <= '9';
    if (category == ModelCategory::PokeBalls)
        return ball;
    if (category == ModelCategory::PokeBeans)
        return name.starts_with("pb");
    if (category == ModelCategory::BattleProps)
        return !ball && !name.starts_with("pb") && !name.starts_with("tr") &&
               !name.starts_with("p1_") && !name.starts_with("p2_");
    if (category == ModelCategory::BattleCharacters)
        return name.starts_with("tr") || name.starts_with("p1_") || name.starts_with("p2_");
    return true;
}
std::vector<LibraryModel> load_model_library(const std::filesystem::path &path,
                                             ModelCategory category, std::atomic_bool *cancel) {
    Archive archive(path);
    std::vector<LibraryModel> result;
    for (std::size_t i = 0; i < archive.size(); ++i) {
        require(!cancel || !cancel->load(), "Model catalog cancelled");
        try {
            auto container = Container::parse(
                archive.decoded(i), category == ModelCategory::BattleArenas ? "BG" : "CM");
            if (container.files.empty() || container.files[0].size() < 4 ||
                u32(container.files[0], 0) != 0x10000)
                continue;
            auto pack = ModelPack::parse(container.files[0]);
            auto model =
                std::find_if(pack.resources.begin(), pack.resources.end(), [](auto &resource) {
                    return resource.category == 0;
                });
            if (model != pack.resources.end() && model_category_matches(category, model->name))
                result.push_back({i, model->name});
        } catch (const std::exception &) {
        }
    }
    return result;
}
ModelDocument load_library_model(const std::filesystem::path &dump,
                                 const std::filesystem::path &path, ModelCategory category,
                                 const LibraryModel &entry, std::atomic_bool *cancel,
                                 const std::string &prefix, const Bytes *replacement,
                                 std::optional<std::size_t> selected_model,
                                 const std::vector<std::size_t> &container_path) {
    ModelDocument doc;
    doc.texture_prefix = prefix;
    doc.kind = ModelAssetKind::ArchiveModel;
    doc.dump = dump;
    doc.name = entry.name;
    doc.looping_effects = false;
    auto bytes = replacement ? *replacement : Archive(path).decoded(entry.member);
    auto relative =
        std::filesystem::absolute(path).lexically_relative(std::filesystem::absolute(dump));
    auto logical =
        relative.empty() || *relative.begin() == ".." ? std::filesystem::absolute(path) : relative;
    doc.sources.push_back({logical, entry.member, "Model resource", sha256(bytes), bytes});
    auto container = Container::parse(asset_resource(bytes, container_path),
                                      category == ModelCategory::BattleArenas ? "BG" : "CM");
    auto resource_path = [&](std::vector<std::size_t> path) {
        path.insert(path.begin(), container_path.begin(), container_path.end());
        return path;
    };
    require(!container.files.empty(), "Model container is empty");
    auto pack = ModelPack::parse(container.files[0]);
    ModelDecoder decoder;
    decoder.cancel = cancel;
    decoder.keep_skeleton = true;
    decoder.skeletal_motions[doc.texture_prefix] = {SkeletalMotion{}, false};
    decoder.out.source_bytes = bytes.size();
    for (auto &resource : pack.resources)
        if (resource.category == 3 || resource.category == 4)
            decoder.shader(resource.bytes, doc.texture_prefix);
    for (unsigned i = 0; i < pack.resources.size(); ++i) {
        auto &resource = pack.resources[i];
        doc.resources.push_back({0, resource_path({0, i}),
                                 resource.category == 0                             ? "Model"
                                 : resource.category == 1                           ? "Texture"
                                 : resource.category == 3 || resource.category == 4 ? "Shader"
                                                                                    : "Auxiliary",
                                 resource.name});
        if (resource.category == 1) {
            decoder.texture(resource.bytes, doc.texture_prefix);
            doc.texture_resources[doc.texture_prefix + text(slice(resource.bytes, 40, 64))] = i;
        }
    }
    for (unsigned i = 0; i < pack.resources.size(); ++i) {
        auto &resource = pack.resources[i];
        if (resource.category != 0 || (selected_model && i != *selected_model))
            continue;
        decoder.checkpoint();
        decoder.placed_model(resource.bytes, resource.name, doc.texture_prefix);
        doc.draw_resources.resize(decoder.out.draws.size(), i);
        doc.material_resources.resize(decoder.out.materials.size(), i);
    }
    require(!decoder.out.draws.empty(), "Model has no supported geometry");
    auto motion = [&](View data, unsigned slot, bool face = false) {
        AssetMotion m;
        m.slot = slot;
        m.name = face ? "Face motion" : "Motion " + std::to_string(slot);
        m.group = face ? 1 : 0;
        m.resource = doc.resources.size();
        auto motion_path = face ? std::vector<std::size_t>{4}
                           : category == ModelCategory::BattleArenas
                               ? std::vector<std::size_t>{1}
                               : std::vector<std::size_t>{1, motion_table_path, slot};
        doc.resources.push_back({0, resource_path(motion_path), "Motion", m.name});
        try {
            m.skeletal = decode_skeletal_motion(data);
            m.material = decode_material_motion(data);
            m.visibility = decode_visibility_motion(data);
        } catch (const std::exception &e) {
            m.error = e.what();
        }
        doc.motions.push_back(std::move(m));
    };
    if (container.files.size() > 1 && !container.files[1].empty())
        try {
            auto &data = container.files[1];
            if (category == ModelCategory::BattleArenas)
                motion(data, 0);
            else {
                auto count = u32(data, 0);
                require(count < 65536 && 4ull + count * 4ull <= data.size(),
                        "Invalid character motion table");
                std::vector<std::size_t> offsets;
                for (unsigned i = 0; i < count; ++i) {
                    auto offset = u32(data, 4 + i * 4);
                    if (offset) {
                        auto start = std::size_t(offset) + 4;
                        require(start >= 4ull + count * 4ull && start + 4 <= data.size(),
                                "Invalid character motion offset");
                        offsets.push_back(start);
                    }
                }
                offsets.push_back(data.size());
                std::sort(offsets.begin(), offsets.end());
                for (unsigned i = 0; i < count; ++i) {
                    decoder.checkpoint();
                    auto offset = u32(data, 4 + i * 4);
                    if (!offset)
                        continue;
                    auto start = std::size_t(offset) + 4;
                    auto end = *std::upper_bound(offsets.begin(), offsets.end(), start);
                    motion(slice(data, start, end - start), i);
                }
            }
        } catch (const std::exception &e) {
            decoder.checkpoint();
            decoder.note(std::string("Motion catalog unavailable: ") + e.what());
        }

    if (category == ModelCategory::FieldCharacters && container.files.size() > 4 &&
        !container.files[4].empty())
        motion(container.files[4], 0, true);

    if (entry.name.starts_with("p1_") || entry.name.starts_with("p2_"))
        decoder.note("Player base only; clothing assembly is not included in this preview.");
    doc.scene = std::make_shared<Environment>(std::move(decoder.out));
    doc.select_motion(-1);
    return doc;
}
ModelDocument studio_library_model(const ModelDocument &source, std::size_t resource) {
    auto &link = source.resources.at(resource);
    if (source.battle_effect) {
        const auto &member = source.sources.at(link.source);
        std::map<std::size_t, Bytes> bytes;
        for (const auto &s : source.sources)
            bytes[s.member] = s.original;
        return load_effect_model(source.dump, member.member, member.subfile, link.path,
                                 source.effect_motions, nullptr, bytes);
    }
    require(link.role == "Model" && link.path.size() >= 2 && link.path[link.path.size() - 2] == 0,
            "Choose a native model resource");
    std::vector<std::size_t> container_path(link.path.begin(), link.path.end() - 2);
    auto &member = source.sources.at(link.source);
    auto category = container_path.empty() && member.original[0] == 'B'
                        ? ModelCategory::BattleArenas
                        : ModelCategory::FieldCharacters;
    auto result =
        load_library_model(source.dump, source.archive_sources.resolve(source.dump, member.archive),
                           category, {member.member, link.name}, nullptr, "model/",
                           &member.original, link.path.back(), container_path);
    result.archive_sources = source.archive_sources;
    result.sources.front().archive = member.archive;
    result.originating_map = source.originating_map;
    return result;
}
ModelDocument reload_editable_model(const ModelDocument &source,
                                    const std::map<std::size_t, Bytes> &members) {
    if (source.area >= 0)
        return reload_map_model(source, members);
    if (source.is_pokemon())
        return reload_pokemon(source, members);
    require(source.area < 0 && !source.clothing && !source.material_resources.empty(),
            "Select an individual library model");
    auto &link = source.resources.at(source.material_resources.front());
    auto snapshot = source;
    for (auto &member : snapshot.sources)
        if (auto found = members.find(member.member); found != members.end())
            member.original = found->second;
    auto result = studio_library_model(snapshot, source.material_resources.front());
    result.independent_asset = source.independent_asset;
    result.looping_effects = source.looping_effects;
    if (source.motion >= 0) {
        auto &before = source.motions.at(source.motion);
        auto found = std::find_if(result.motions.begin(), result.motions.end(), [&](auto &motion) {
            return motion.slot == before.slot && motion.group == before.group;
        });
        result.select_motion(found == result.motions.end() ? -1
                                                           : int(found - result.motions.begin()));
    }
    return result;
}

}
