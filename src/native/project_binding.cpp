#include "assets/character_registration.h"
#include "assets/battle_effects.h"
#include "native/project_binding.h"
#include "field/map_creation.h"
#include "assets/material_document.h"
#include "authoring/ground_export.h"
#include "authoring/object_export.h"
#include "assets/clothing.h"
#include "assets/pokemon_settings.h"
#include "field/placement_document.h"
#include "field/camera_document.h"
#include "field/warp_document.h"
#include "field/weather_document.h"
#include "field/zone_document.h"
#include "field/interaction_document.h"
#include "field/interaction_source.h"
#include "field/conversation_workspace.h"
#include "field/pedestrian_routes.h"
#include "formats/container.h"
#include "field/pickup_document.h"
#include "field/overworld_document.h"
#include "field/encounter_document.h"
#include "field/collision_document.h"
#include "audio/audio_document.h"
#include "audio/cry_document.h"
#include "images/image_document.h"
#include "formats/archive.h"
#include <sstream>
#include "core/digest.h"
#include <algorithm>
#include <iomanip>
namespace studio {
namespace {
ProjectStore *active = nullptr;
}
ProjectStore *project_store() {
    return active;
}
void set_project_store(ProjectStore *store) {
    active = store;
}
std::set<ProjectBinding *> &ProjectBinding::all() {
    static std::set<ProjectBinding *> bindings;
    return bindings;
}
ProjectBinding::ProjectBinding() {
    all().insert(this);
}
ProjectBinding::~ProjectBinding() {
    all().erase(this);
}
void ProjectBinding::bind(std::string kind, std::string key, std::string label,
                          std::string parameters, std::function<bool()> dirty,
                          std::function<Bytes()> encode, std::function<void()> saved) {
    edit_ = {std::move(key), std::move(kind), std::move(label), std::move(parameters), {}};
    dirty_ = std::move(dirty);
    encode_ = std::move(encode);
    saved_ = std::move(saved);
    imported_ = false;
    baseline_ = active && active->edits.contains(edit_.key) ? active->edits.at(edit_.key).blob
                                                            : std::string{};
}
std::filesystem::path ProjectBinding::document() const {
    return active ? active->document(edit_.key) : std::filesystem::path{};
}
bool ProjectBinding::capture() {
    captured_ = false;
    if (!active)
        return false;
    if (ready_)
        ready_();
    if (!dirty_ || (!dirty_() && !imported_))
        return false;
    auto data = encode_();
    auto current = active->edits.find(edit_.key);
    require(
        current == active->edits.end() || current->second.blob == baseline_ ||
            current->second.blob == sha256(data),
        "This asset was saved from another workspace. Reopen it before applying further edits: " +
            edit_.label);
    active->capture(edit_, data);
    captured_ = true;
    return true;
}
void ProjectBinding::acknowledge() {
    if (captured_) {
        saved_();
        baseline_ = active->edits.at(edit_.key).blob;
        captured_ = false;
        imported_ = false;
    }
}
bool save_editor_project() {
    if (!active)
        return false;
    auto previous = active->edits;
    try {
        std::map<std::string, std::string> captured;
        for (auto *b : ProjectBinding::all())
            if (b->capture()) {
                auto &edit = active->edits.at(b->key());
                if (auto found = captured.find(edit.key); found != captured.end())
                    require(found->second == edit.blob,
                            "The same asset has different unsaved edits in two workspaces: " +
                                edit.label);
                captured[edit.key] = edit.blob;
            }
        if (std::any_of(captured.begin(), captured.end(), [&](auto &item) {
                return active->edits.at(item.first).kind == "overworld" ||
                       active->edits.at(item.first).kind == "pickups";
            })) {
            std::map<unsigned, std::string> flags;
            Archive field(active->source / GameProfile::field_archive(active->source));
            for (auto &[key, edit] : active->edits)
                if (edit.kind == "overworld") {
                    unsigned area;
                    std::istringstream args(edit.parameters);
                    require(bool(args >> area), "Invalid overworld area");
                    auto base = area * TargetProfile::area_stride;
                    OverworldDocument doc(
                        area, field.decoded(base),
                        field.decoded(base + TargetProfile::character_resource_slot),
                        field.decoded(base + TargetProfile::static_resource_slot));
                    doc.restore(text(read_file(active->document(key))));
                    for (auto &op : doc.operations())
                        if (op.action != OverworldOperation::Action::Remove &&
                            doc.entries().at(op.entry).kind == OverworldKind::Pickup &&
                            (op.action == OverworldOperation::Action::Add ||
                             op.flag != doc.entries().at(op.entry).condition))
                            require(flags.emplace(op.flag, key).second,
                                    "Collection flag " + std::to_string(op.flag) +
                                        " is reserved by another pending pickup in this project");
                } else if (edit.kind == "pickups") {
                    unsigned area;
                    std::istringstream args(edit.parameters);
                    require(bool(args >> area), "Invalid pickup area");
                    PickupDocument doc(area, field.decoded(area * TargetProfile::area_stride));
                    doc.restore(text(read_file(active->document(key))));
                    for (unsigned i = 0; i < doc.records().size(); ++i) {
                        auto flag = doc.values(i).flag;
                        if (flag != doc.records()[i].condition)
                            require(flags.emplace(flag, key).second,
                                    "Collection flag " + std::to_string(flag) +
                                        " is reserved by another pending pickup in this project");
                    }
                }
        }
        active->save();
    } catch (...) {
        active->edits = std::move(previous);
        throw;
    }
    for (auto *b : ProjectBinding::all())
        b->acknowledge();
    return true;
}
Bytes project_text(const std::string &s) {
    return Bytes(s.begin(), s.end());
}
Bytes project_encode_file(const std::function<void(const std::filesystem::path &)> &writer) {
    require(active != nullptr, "Open an editor project first");
    auto file = active->root / "scratch" / "save-document";
    std::filesystem::create_directories(file.parent_path());
    writer(file);
    auto result = read_file(file);
    std::filesystem::remove(file);
    return result;
}
ModelDocument load_project_effect_model(const std::filesystem::path &source,
                                        const std::string &parameters) {
    std::istringstream args(parameters);
    std::string kind;
    require(bool(args >> kind) && kind == "battle-effect", "Invalid battle effect source");
    std::size_t member, count;
    unsigned subfile;
    require(bool(args >> member >> subfile >> count) && count <= 12,
            "Invalid battle effect source");
    std::vector<std::size_t> path(count);
    for (auto &child : path)
        require(bool(args >> child), "Missing effect resource path");
    require(bool(args >> count) && count <= 128, "Invalid effect motion count");
    std::vector<EffectMotionSource> motions(count);
    for (auto &motion : motions) {
        require(bool(args >> motion.member >> motion.subfile >> count) && count <= 12,
                "Invalid effect motion source");
        motion.path.resize(count);
        for (auto &child : motion.path)
            require(bool(args >> child), "Missing effect motion path");
    }
    return load_effect_model(source, member, subfile, path, motions);
}
std::string project_model_parameters(const ModelDocument &model) {
    std::ostringstream out;
    if (model.is_pokemon())
        out << (model.shadow_model ? "pokemon-shadow " : "pokemon ") << model.pokemon.species << ' '
            << model.pokemon.form << ' ' << model.pokemon.female << ' ' << model.shiny;
    else if (model.clothing) {
        auto &c = *model.clothing;
        out << "clothing " << c.profile << ' ' << c.skin << ' ' << c.hair << ' ' << c.eyes << ' '
            << c.lip << ' ' << model.clothing_part;
        for (auto item : c.items)
            out << ' ' << item;
    } else if (model.battle_effect) {
        const auto &link = model.resources.at(model.material_resources.front());
        const auto &source = model.sources.at(link.source);
        out << "battle-effect " << source.member << ' ' << source.subfile << ' '
            << link.path.size();
        for (auto child : link.path)
            out << ' ' << child;
        out << ' ' << model.effect_motions.size();
        for (const auto &motion : model.effect_motions) {
            out << ' ' << motion.member << ' ' << motion.subfile << ' ' << motion.path.size();
            for (auto child : motion.path)
                out << ' ' << child;
        }
    } else if (model.area < 0) {
        require(!model.sources.empty(), "Model source is missing");
        auto relative = model.sources.front().archive.is_absolute()
                            ? model.sources.front().archive.lexically_relative(model.dump)
                            : model.sources.front().archive;
        const auto &model_path = model.resources.at(model.material_resources.front()).path;
        if (model_path.size() == 4) {
            require(GameProfile::is_field_archive(relative) && model_path[1] == 1 &&
                        model_path[2] == 0 &&
                        model.sources.front().member % TargetProfile::area_stride ==
                            TargetProfile::character_resource_slot,
                    "Invalid map character source");
            out << "field-character " << std::quoted(relative.generic_string()) << ' '
                << model.sources.front().member << ' ' << std::quoted(model.name) << ' '
                << model_path[0] << ' ' << model_path[3];
            return out.str();
        }
        out << "library " << std::quoted(relative.generic_string()) << ' '
            << model.sources.front().member << ' ' << std::quoted(model.name);
        if (!model.material_resources.empty()) {
            auto &link = model.resources.at(model.material_resources.front());
            if (link.path.size() == 2)
                out << ' ' << link.path[1];
        }
    } else {
        MaterialDocument doc(model);
        out << "map " << model.area << ' ' << std::quoted(doc.identity());
    }
    return out.str();
}
ProjectBuildResult stage_editor_project(const ProjectBuild &build) {
    auto result = ProjectStore::stage(build, export_project_edit);
    if (std::none_of(build.edits.begin(), build.edits.end(), [](const auto &edit) {
            return edit.kind == "warps";
        }))
        return result;
    auto before = load_warp_destinations(build.source);
    auto after = load_warp_destinations(result.directory);
    for (const auto &old : before) {
        if (std::any_of(after.begin(), after.end(), [&](const auto &entry) {
                return entry.zone == old.zone && entry.event == old.event;
            }))
            continue;
        for (const auto &entry : after)
            require(!entry.destination || *entry.destination != std::pair{old.zone, old.event},
                    "Deleted zone " + std::to_string(old.zone) + " / entrance " +
                        std::to_string(old.event) + " is still targeted by zone " +
                        std::to_string(entry.zone) + " / entrance " + std::to_string(entry.event) +
                        ". Retarget or delete that entrance, then stage again.");
    }
    return result;
}
void export_project_edit(const ProjectEdit &e, const std::filesystem::path &source,
                         const std::filesystem::path &file, const std::filesystem::path &output) {
    auto patch = text(read_file(file));
    std::istringstream args(e.parameters);
    unsigned area = 0;
    if (e.kind == "character-registration") {
        auto data = read_file(file);
        patch.assign(data.begin(), data.end());
        export_character_registration(Archive(source / TargetProfile::character_archive),
                                      CharacterRegistration::parse(patch),
                                      output / TargetProfile::character_archive);
        return;
    }
    if (e.kind == "field-map") {
        export_map_creation(source, MapCreation::parse(patch), output);
        return;
    }
    if (e.kind == "field-map-created" || e.kind == "character-registration-created" ||
        (e.kind == "asset-library" || e.kind == "studio-asset"))
        return;
    if (e.kind == "material" || e.kind == "pokemon-settings") {
        std::string kind;
        unsigned species, form;
        bool female, shiny;
        require(bool(args >> kind), "Missing model source");
        ModelDocument model;
        if (kind == "pokemon" || kind == "pokemon-shadow") {
            require(bool(args >> species >> form >> female >> shiny), "Invalid Pokemon source");
            auto catalog = load_pokemon_catalog(source);
            auto it = std::find_if(catalog.begin(), catalog.end(), [&](auto &p) {
                return p.species == species && p.form == form && p.female == female;
            });
            require(it != catalog.end(), "Project Pokemon is missing: " + e.label);
            model = load_pokemon(source, *it, shiny, nullptr, {}, kind == "pokemon-shadow");
        } else if (kind == "clothing") {
            ClothingSelection selection;
            int part;
            require(bool(args >> selection.profile >> selection.skin >> selection.hair >>
                         selection.eyes >> selection.lip >> part),
                    "Invalid clothing source");
            for (auto &item : selection.items)
                require(bool(args >> item), "Missing clothing item");
            model = load_clothing(source, selection, part);
        } else if (kind == "battle-effect") {
            model = load_project_effect_model(source, e.parameters);
        } else if (kind == "field-character") {
            std::string path, name;
            std::size_t member, character, selected;
            require(bool(args >> std::quoted(path) >> member >> std::quoted(name) >> character >>
                         selected),
                    "Invalid map character source");
            require(GameProfile::is_field_archive(path) &&
                        member % TargetProfile::area_stride ==
                            TargetProfile::character_resource_slot,
                    "Invalid map character archive");
            model = load_library_model(source, ArchiveSources{}.resolve(source, path),
                                       ModelCategory::FieldCharacters, {member, name}, nullptr,
                                       "model/", nullptr, selected, {character, 1});
        } else if (kind == "library") {
            std::string path, name;
            std::size_t member;
            require(bool(args >> std::quoted(path) >> member >> std::quoted(name)),
                    "Invalid library source");
            std::optional<std::size_t> selected;
            std::size_t selection;
            if (args >> selection)
                selected = selection;
            bool found = false;
            for (int i = 0; i <= int(ModelCategory::BattleArenas); ++i) {
                auto category = ModelCategory(i);
                if (path == model_category_archive(category)) {
                    model = load_library_model(source, source / path, category, {member, name},
                                               nullptr, "model/", nullptr, selected);
                    found = true;
                    break;
                }
            }
            if (!found)
                for (auto &profile : ClothingProfile::archives)
                    for (auto archive : profile)
                        if (*archive && path == archive) {
                            model = load_library_model(
                                source, source / path, ModelCategory::FieldCharacters,
                                {member, name}, nullptr, "model/", nullptr, selected);
                            found = true;
                        }
            require(found, "Unknown model library archive");
        } else {
            std::string identity;
            require(bool(args >> area >> std::quoted(identity)), "Invalid field model source");
            auto scene = load_environment(source, area);
            bool found = false;
            for (unsigned i = 0; i < scene.draws.size(); ++i)
                if (scene.draws[i].source) {
                    auto candidate = isolate_map_model(scene, int(i), source, int(area));
                    MaterialDocument doc(candidate);
                    if (doc.identity() == identity) {
                        model = std::move(candidate);
                        found = true;
                        break;
                    }
                }
            require(found, "Project field model is missing: " + e.label);
        }
        if (e.kind == "material") {
            MaterialDocument doc(std::move(model));
            doc.restore(patch);
            if (doc.changed())
                doc.export_to(output);
        } else {
            Archive archive(source / TargetProfile::pokemon_archive);
            PokemonSettingsDocument doc(
                model.pokemon,
                archive.decoded(1 + model.pokemon.data_index * TargetProfile::pokemon_stride +
                                TargetProfile::pokemon_settings_slot));
            doc.restore(patch);
            if (!doc.changed())
                return;
            auto path = output / TargetProfile::pokemon_archive;
            std::filesystem::create_directories(path.parent_path());
            doc.export_archive(source / TargetProfile::pokemon_archive, path);
        }
        return;
    }
    if (e.kind == "pedestrians") {
        require(bool(args >> area), "Invalid pedestrian field area");
        Archive field(source / GameProfile::field_archive(source));
        PedestrianDocument doc(
            field.decoded(area * TargetProfile::area_stride + TargetProfile::placement_slot));
        doc.restore(patch);
        doc.export_to(source, output, area);
        return;
    }
    if (e.kind == "shared-interaction") {
        unsigned member;
        require(bool(args >> member), "Invalid shared script member");
        InteractionDocument doc(
            read_shared_script(Archive(source / TargetProfile::shared_script_archive), member));
        doc.restore(patch);
        doc.export_shared(source, output, member);
        return;
    }
    if (e.kind == "interaction") {
        unsigned local_zone;
        require(bool(args >> area >> local_zone), "Invalid interaction zone");
        Archive field(source / GameProfile::field_archive(source));
        auto scripts = Container::parse(
            field.decoded(area * TargetProfile::area_stride + TargetProfile::zone_script_slot),
            "ZS");
        require(local_zone < scripts.files.size(), "Interaction zone script is missing");
        InteractionDocument doc(scripts.files[local_zone]);
        doc.restore(patch);
        doc.export_to(source, output, area, local_zone);
        return;
    }
    if (e.kind == "zone_settings") {
        ZoneDocument doc(Archive(source / TargetProfile::zone_archive).decoded(0));
        doc.restore(patch);
        doc.export_to(source, output);
        return;
    }
    if (e.kind == "weather") {
        WeatherDocument doc(Archive(source / TargetProfile::zone_archive).decoded(0));
        doc.restore(patch);
        doc.export_to(source, output);
        return;
    }
    if (e.kind == "warps") {
        require(bool(args >> area), "Invalid entrance field area");
        Archive archive(source / GameProfile::field_archive(source));
        WarpDocument doc(area, archive.decoded(area * TargetProfile::area_stride +
                                               TargetProfile::placement_slot));
        doc.restore(patch);
        if (doc.changed())
            doc.export_to(source, {}, output);
        return;
    }
    if (e.kind == "encounters") {
        require(bool(args >> area), "Invalid encounter area");
        Archive field(source / GameProfile::field_archive(source));
        auto base = area * TargetProfile::area_stride;
        EncounterDocument doc(area, field.decoded(base),
                              field.decoded(base + TargetProfile::encounter_table_slot));
        doc.restore(patch);
        doc.export_to(source, {}, output);
        return;
    }
    if (e.kind == "overworld") {
        require(bool(args >> area), "Invalid overworld area");
        Archive archive(source / GameProfile::field_archive(source));
        auto base = area * TargetProfile::area_stride;
        OverworldDocument doc(area, archive.decoded(base),
                              archive.decoded(base + TargetProfile::character_resource_slot),
                              archive.decoded(base + TargetProfile::static_resource_slot));
        doc.restore(patch);
        doc.export_to(source, output);
        return;
    }
    if (e.kind == "pickups") {
        require(bool(args >> area), "Invalid pickup field area");
        Archive archive(source / GameProfile::field_archive(source));
        PickupDocument doc(area, archive.decoded(area * TargetProfile::area_stride +
                                                 TargetProfile::placement_slot));
        doc.restore(patch);
        if (doc.changed())
            doc.export_to(source, {}, output);
        return;
    }
    if (e.kind == "placements" || e.kind == "collision" || e.kind == "cameras") {
        require(bool(args >> area), "Invalid project field area");
        if (e.kind == "placements") {
            Archive archive(source / GameProfile::field_archive(source));
            PlacementDocument doc(area, archive.decoded(area * TargetProfile::area_stride +
                                                        TargetProfile::placement_slot));
            doc.restore(patch);
            if (doc.changed_count())
                export_placements(source, output, doc);
        } else if (e.kind == "cameras") {
            Archive archive(source / GameProfile::field_archive(source));
            CameraDocument doc(area, archive.decoded(area * TargetProfile::area_stride +
                                                     TargetProfile::camera_slot));
            doc.restore(patch);
            if (doc.changed())
                doc.export_to(source, {}, output);
        } else {
            auto scene = std::make_shared<Environment>(load_environment(source, area));
            CollisionDocument doc(scene, area, source);
            doc.restore(patch);
            if (doc.changed())
                doc.export_to(output);
        }
        return;
    }
    if (e.kind == "authored-conversations") {
        std::string baseline;
        require(bool(args >> area >> std::quoted(baseline)), "Invalid conversation source");
        require(baseline.empty() ||
                    (baseline.size() == 64 &&
                     baseline.find_first_not_of("0123456789abcdef") == std::string::npos),
                "Invalid conversation baseline");
        auto pinned = file.parent_path().parent_path() / "member-sources" /
                      (baseline.empty() ? "original" : baseline);
        ConversationWorkspace workspace(pinned, area);
        workspace.restore(patch);
        workspace.export_to(output, e.replay_staged ? ConversationBuildMode::PreviouslyStaged
                                                    : ConversationBuildMode::Current);
        return;
    }
    if (e.kind == "composition") {
        std::string baseline;
        require(bool(args >> area >> std::quoted(baseline)), "Invalid composition source");
        require(baseline.empty() ||
                    (baseline.size() == 64 &&
                     baseline.find_first_not_of("0123456789abcdef") == std::string::npos),
                "Invalid composition baseline");
        auto pinned = file.parent_path().parent_path() / "member-sources" /
                      (baseline.empty() ? "original" : baseline);
        require(std::filesystem::is_directory(pinned),
                "Reopen the composition to restore its source snapshot before staging");
        auto catalog = load_composition_catalog(pinned, area, patch);
        CompositionDocument doc(catalog, map_template_fingerprint(pinned, catalog),
                                {{0, 0}, 100, 1, 1});
        doc.restore(patch);
        if (doc.stage_ground())
            export_ground_patch(pinned, doc, doc.ground_attribute(), output);
        if (doc.stage_objects())
            export_composition_objects(pinned, doc, output);
        return;
    }
    if (e.kind == "audio") {
        AudioDocument doc(source);
        doc.load(file);
        if (doc.size())
            doc.export_to(output);
        return;
    }
    if (e.kind == "cries") {
        CryDocument doc(source);
        doc.load(file);
        if (doc.size())
            doc.export_to(output);
        return;
    }
    if (e.kind == "images") {
        ImageDocument doc(scan_image_catalog(source));
        doc.load(file);
        if (doc.size())
            doc.export_to(output);
        return;
    }
    if (e.kind == "palette") {
        unsigned profile;
        require(bool(args >> profile) && profile < 4, "Invalid clothing profile");
        ClothingPalette palette;
        palette.load(source / ClothingProfile::colors[profile]);
        palette.current = read_file(file);
        auto path = output / ClothingProfile::colors[profile];
        std::filesystem::create_directories(path.parent_path());
        palette.export_archive(path);
        return;
    }
    if (e.kind == "original-archive") {
        std::string path;
        require(bool(args >> std::quoted(path)) && patch == "USUM_RESET_ARCHIVE 1",
                "Invalid original archive reset");
        auto relative = std::filesystem::path(path);
        require(!relative.empty() && !relative.is_absolute() && !relative.has_root_name(),
                "Invalid reset path");
        for (auto &part : relative)
            require(part != "..", "Reset path escapes project");
        auto project = ProjectStore::open(file.parent_path().parent_path());
        Archive original(project.original / relative), current(source / relative);
        require(original.layout_identity() == current.layout_identity(),
                "Reset archive layout changed");
        std::map<std::pair<std::size_t, unsigned>, Bytes> replacements;
        for (std::size_t member = 0; member < original.size(); ++member)
            for (auto sub : original.subfiles(member)) {
                auto data = original.raw(member, sub);
                if (data != current.raw(member, sub))
                    replacements[{member, sub}] = std::move(data);
            }
        current.export_subfiles(output / relative, replacements);
        return;
    }
    if (e.kind == "original") {
        std::string path;
        int member, subfile;
        require(bool(args >> std::quoted(path) >> member >> subfile), "Invalid reset source");
        auto relative = std::filesystem::path(path);
        require(!relative.empty() && !relative.has_root_name() && !relative.has_root_directory(),
                "Reset path must be relative");
        for (auto &part : relative)
            require(part != "..", "Reset path escapes the project");
        auto out = output / relative;
        std::filesystem::create_directories(out.parent_path());
        if (member < 0)
            write_new_file(out, read_file(file));
        else {
            Archive archive(source / path);
            archive.export_subfiles(out,
                                    {{{std::size_t(member), unsigned(subfile)}, read_file(file)}});
        }
        return;
    }
    throw std::runtime_error("No project staging adapter for " + e.kind);
}
}
