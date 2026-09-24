#include "native/tutorial_widgets.h"
#include "native/pokemon_bundle_editor.h"
#include "assets/pokemon_bundle.h"
#include "native/project_binding.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
namespace studio {
void PokemonBundleEditor::draw(const ModelDocument &donor, SDL_Window *window) {
    if (!donor.is_pokemon())
        return;
    auto source_path = [&]() {
        if (auto *project = project_store())
            return project->source / TargetProfile::pokemon_archive;
        return donor.archive_sources.resolve(donor.dump, TargetProfile::pokemon_archive);
    };
    std::string path, error;
    bool ready = false;
    {
        std::lock_guard lock(dialog_->mutex);
        ready = dialog_->ready;
        if (ready) {
            path = dialog_->path;
            error = dialog_->error;
            dialog_->ready = false;
        }
    }
    if (ready)
        try {
            auto action = action_;
            action_ = 0;
            require(error.empty(), error);
            if (!path.empty()) {
                if (action == 1) {
                    if (auto *project = project_store())
                        require(!project->unstaged() &&
                                    std::all_of(project->edits.begin(), project->edits.end(),
                                                [](const auto &entry) {
                                                    return entry.second.kind == "composition" ||
                                                           entry.second.kind == "field-map-created" ||
                                                           entry.second.kind == "character-registration-created" ||
                                                           (entry.second.kind == "asset-library" || entry.second.kind == "studio-asset");
                                                }),
                                "Stage and reload project edits before adding a bundle");
                    require(owner_ == donor.name,
                            "The donor changed; choose the bundle folder again");
                    Archive source(source_path());
                    auto bundle =
                        clone_pokemon_bundle(source, donor.pokemon, unsigned(target_), mode_ == 1);
                    folder_ = std::filesystem::u8path(path) /
                              ("pokemon-" + std::to_string(bundle.species) + "-form-" +
                               std::to_string(bundle.form));
                    save_pokemon_bundle(source, bundle, folder_);
                    if (mode_ == 0)
                        target_ = int(bundle.species + 1);
                    auto dump = donor.dump;
                    auto archives = donor.archive_sources;
                    archives.pokemon = folder_ / "pokemon.garc";
                    if (auto *project = project_store()) {
                        project->import_file(TargetProfile::pokemon_archive, archives.pokemon,
                                             true);
                        dump = project->source;
                        archives.pokemon.clear();
                    }
                    auto catalog = load_pokemon_catalog(dump, archives);
                    auto found = std::find_if(catalog.begin(), catalog.end(), [&](auto &e) {
                        return e.species == bundle.species && e.form == bundle.form && !e.female;
                    });
                    require(found != catalog.end(), "Created Pokemon is missing");
                    prepared_ = std::make_unique<ModelDocument>(
                        load_pokemon(dump, *found, false, nullptr, archives));
                    message_ = "Created independent donor assets. Send the new entry to Studio to "
                               "edit it, or export its GARC.";
                } else {
                    export_ = std::filesystem::u8path(path);
                    Archive(folder_ / "pokemon.garc").export_to(export_, {});
                    message_ = "Exported and verified the expanded Pokemon GARC.";
                }
            }
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    if (!studio::TutorialWidgets::CollapsingHeader("pokemon_bundle_editor", "Add Pokemon assets"))
        return;
    ImGui::BeginDisabled(action_ != 0);
    ImGui::TextWrapped("Clone this archive donor into a new species row or an additional form. "
                       "Only the Pokemon GARC changes. Other tools handle game registration.");
    if (studio::TutorialWidgets::RadioButton("pokemon_bundle_editor", "New species row", &mode_, 0))
        target_ = 0;
    ImGui::SameLine();
    if (studio::TutorialWidgets::RadioButton("pokemon_bundle_editor", "Additional form", &mode_, 1))
        target_ = int(donor.pokemon.species);
    if (target_ == 0)
        try {
            target_ = int(pokemon_species_rows(Archive(source_path())) + 1);
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    if (mode_ == 0) {
        ImGui::Text("New species: %d", target_);
    } else {
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("Target species", &target_);
    }
    auto choose = [&](int action) {
        owner_ = donor.name;
        action_ = action;
        if (action == 1 && mode_ == 0)
            target_ = int(pokemon_species_rows(Archive(source_path())) + 1);
        {
            std::lock_guard lock(dialog_->mutex);
            dialog_->path.clear();
            dialog_->error.clear();
            dialog_->ready = false;
        }
        auto *owner = new std::shared_ptr<FolderSelection>(dialog_);
        auto callback = [](void *data, const char *const *files, int) {
            std::unique_ptr<std::shared_ptr<FolderSelection>> holder(
                static_cast<std::shared_ptr<FolderSelection> *>(data));
            auto &state = **holder;
            std::lock_guard lock(state.mutex);
            if (!files)
                state.error = SDL_GetError();
            else if (files[0])
                state.path = files[0];
            state.ready = true;
        };
        if (action == 1)
            SDL_ShowOpenFolderDialog(callback, owner, window, nullptr, false);
        else {
            static const SDL_DialogFileFilter filters[] = {{"Pokemon archive", "garc"}};
            SDL_ShowSaveFileDialog(callback, owner, window, filters, 1, "pokemon.garc");
        }
    };
    if (studio::TutorialWidgets::Button("pokemon_bundle_editor", "Create donor bundle..."))
        try {
            if (auto *project = project_store())
                require(!project->unstaged() &&
                            std::all_of(project->edits.begin(), project->edits.end(),
                                        [](const auto &entry) {
                                            return entry.second.kind == "composition" ||
                                                   entry.second.kind == "field-map-created" ||
                                                           entry.second.kind == "character-registration-created" ||
                                                           (entry.second.kind == "asset-library" || entry.second.kind == "studio-asset");
                                        }),
                        "Stage and reload project edits before adding a bundle");
            choose(1);
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    if (prepared_) {
        if (studio::TutorialWidgets::Button("pokemon_bundle_editor", "Send new entry to Studio"))
            created_ = std::make_unique<ModelDocument>(*prepared_);
        if (studio::TutorialWidgets::Button("pokemon_bundle_editor",
                                            "Export initial bundle GARC..."))
            choose(2);
    }
    ImGui::EndDisabled();
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
}
}
