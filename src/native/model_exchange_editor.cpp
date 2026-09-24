#include "native/tutorial_widgets.h"
#include "native/model_exchange_editor.h"
#include "assets/blender_preview.h"
#include "assets/asset_package.h"
#include <SDL3/SDL.h>
#include <imgui.h>
namespace studio {
void ModelExchangeEditor::draw(MaterialDocument &doc, ModelDocument &preview,
                               EnvironmentRenderer &renderer, bool &playing, SDL_Window *window) {
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
    if (ready) {
        auto action = action_;
        action_ = 0;
        try {
            require(error.empty(), error);
            if (!path.empty()) {
                require(owner_ == doc.identity(),
                        "The open model changed; choose the exchange file again");
                file_ = std::filesystem::u8path(path);
                if (file_.extension().empty())
                    file_ += action >= 3 ? ".usum-motion" : ".usum-asset";
                if (action >= 3) {
                    require(motion_ >= 0, "Choose a motion first");
                    if (action == 3) {
                        auto text =
                            serialize_motion_exchange(doc.motion_exchange(std::size_t(motion_)));
                        write_file_atomic(
                            file_,
                            View(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
                        message_ = "Exported motion. Import the matching model in Blender, then "
                                   "import this motion.";
                    } else {
                        auto bytes = read_file(file_);
                        doc.import_motion_exchange(
                            std::size_t(motion_),
                            parse_motion_exchange(std::string(bytes.begin(), bytes.end())));
                        preview = doc.model;
                        preview.select_motion(motion_);
                        renderer.set_scene(preview.scene);
                        playing = false;
                        message_ = "Imported motion. Review and save the project.";
                    }
                } else if (action == 1) {
                    write_file_atomic(file_, encode_asset_package(model_asset_package(doc)));
                    message_ =
                        "Exported asset with game resources and Blender previews. No sidecar files are needed.";
                } else {
                    auto bytes = read_file(file_);
                    AssetPackage package;
                    if (is_asset_package(bytes)) package = decode_asset_package(bytes);
                    auto imported = parse_model_exchange(package.empty() ? text(bytes) : text(package.at("models/0.usum-model")));
                    if (imported.standalone) {
                        destination_ = doc.model_exchange();
                        materials_.assign(imported.meshes.size(), 0);
                        for (std::size_t i = 0; i < imported.meshes.size(); ++i) {
                            unsigned needed = 0;
                            for (const auto &v : imported.meshes[i].vertices) {
                                unsigned count = 0;
                                for (auto weight : v.weights)
                                    count += weight > 0;
                                needed = std::max(needed, count);
                            }
                            for (std::size_t m = 0; m < destination_.meshes.size(); ++m)
                                if (destination_.meshes[m].influences >= needed) {
                                    materials_[i] = m;
                                    break;
                                }
                        }
                        incoming_ = std::move(imported);
                        message_.clear();
                        ImGui::OpenPopup("Import new model");
                    } else {
                        if (package.empty()) doc.import_model_exchange(imported);
                        else import_model_asset_package(doc, package);
                        preview = doc.model;
                        preview.select_motion(-1);
                        playing = false;
                        renderer.set_scene(preview.scene);
                        message_ =
                            "Imported asset. Review geometry, materials and motions, then Save "
                            "Project and Stage Project.";
                    }
                }
            }
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    }
    ImGui::SetNextWindowSize(ImVec2(650, 480), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Import new model", nullptr, ImGuiWindowFlags_None)) {
        ImGui::TextWrapped("Replace geometry in %s. Assign a game material to each Blender part; "
                           "edit textures and material settings in Studio afterward.",
                           doc.model.name.c_str());
        if (incoming_) {
            ImGui::Text("%zu parts | %zu bones", incoming_->meshes.size(),
                        incoming_->joints.size());
            if (!incoming_->joints.empty())
                ImGui::TextWrapped(
                    "This replaces the bind skeleton. Existing motion clips are not retargeted; "
                    "import matching motions before using this as an animated character. Pokemon "
                    "keep the donor shadow geometry; review it separately.");
            ImGui::BeginChild("New model material assignments", ImVec2(0, -95));
            for (std::size_t i = 0; i < incoming_->meshes.size(); ++i) {
                ImGui::PushID(int(i));
                ImGui::TextWrapped("%s", incoming_->meshes[i].name.c_str());
                auto label = [&](std::size_t m) {
                    const auto &mesh = destination_.meshes[m];
                    return mesh.name + " / " + mesh.texture + " (" +
                           std::to_string(mesh.influences) + " weights)";
                };
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##Game material", label(materials_[i]).c_str())) {
                    for (std::size_t m = 0; m < destination_.meshes.size(); ++m)
                        if (ImGui::Selectable((label(m) + "##" + std::to_string(m)).c_str(),
                                              materials_[i] == m))
                            materials_[i] = m;
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (!message_.empty())
                ImGui::TextWrapped("%s", message_.c_str());
            if (ImGui::Button("Import geometry")) {
                try {
                    require(owner_ == doc.identity() &&
                                destination_.source == doc.model_exchange().source,
                            "The destination changed; choose the model again");
                    doc.import_new_model(*incoming_, materials_);
                    preview = doc.model;
                    preview.select_motion(-1);
                    playing = false;
                    renderer.set_scene(preview.scene);
                    incoming_.reset();
                    message_ = "New model imported. Review materials and textures, then save and "
                               "stage the project. Undo restores the previous model.";
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception &e) {
                    message_ = e.what();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                incoming_.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::TextWrapped(
        "Edit this model in Blender, then bring it back with changed topology, UVs, normals, "
        "weights and bind bones. New Blender models can be imported without a prior Studio "
        "export. Packages include the exported game materials and textures; export again after "
        "editing those in Studio.");
    if (preview.area >= 0)
        ImGui::TextWrapped(
            "Changes affect this shared model. Map placements and collision stay separate. "
            "Edit map material motions in Studio.");
    ImGui::Separator();
    auto choose = [&](bool save, bool motion = false) {
        owner_ = doc.identity();
        action_ = motion ? (save ? 3 : 4) : (save ? 1 : 2);
        motion_ = motion ? preview.motion : -1;
        {
            std::lock_guard lock(dialog_->mutex);
            dialog_->pending = true;
            dialog_->ready = false;
            dialog_->path.clear();
            dialog_->error.clear();
        }
        auto *owner = new std::shared_ptr<FolderSelection>(dialog_);
        const SDL_DialogFileFilter *filters;
        static const SDL_DialogFileFilter model_filters[] = {{"Studio asset or legacy model",
                                                              "usum-asset;usum-model"}},
                                          motion_filters[] = {
                                              {"Studio motion exchange", "usum-motion"}};
        filters = motion ? motion_filters : model_filters;
        auto callback = [](void *data, const char *const *files, int) {
            std::unique_ptr<std::shared_ptr<FolderSelection>> holder(
                static_cast<std::shared_ptr<FolderSelection> *>(data));
            auto &state = **holder;
            std::lock_guard lock(state.mutex);
            if (!files)
                state.error = SDL_GetError();
            else if (files[0])
                state.path = files[0];
            state.pending = false;
            state.ready = true;
        };
        auto suggestion = file_.empty() ? std::filesystem::path(motion ? "motion.usum-motion"
                                                                       : "model.usum-asset")
                                        : file_;
        suggestion.replace_extension(motion ? ".usum-motion" : ".usum-asset");
        auto initial = suggestion.string();
        if (save)
            SDL_ShowSaveFileDialog(callback, owner, window, filters, 1, initial.c_str());
        else
            SDL_ShowOpenFileDialog(callback, owner, window, filters, 1, initial.c_str(), false);
    };
    ImGui::BeginDisabled(pending());
    if (studio::TutorialWidgets::Button("model_exchange_editor", "Export for Blender", {-1, 0}))
        choose(true);
    if (studio::TutorialWidgets::Button("model_exchange_editor", "Import from Blender", {-1, 0}))
        choose(false);
    ImGui::BeginDisabled(preview.motion < 0 || preview.shadow_model || preview.area >= 0);
    if (studio::TutorialWidgets::Button("model_exchange_editor", "Export selected motion", {-1, 0}))
        choose(true, true);
    if (studio::TutorialWidgets::Button("model_exchange_editor", "Import selected motion", {-1, 0}))
        choose(false, true);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::TextWrapped(
        "Install blender/usum_model.py from the application package. Use File > "
        "Import / Export > USUMStudio asset in Blender. Keep the exported "
        "materials and existing bone names. Add, delete, split or join mesh objects; use "
        "the Blender Studio model panel to prepare newly imported geometry.");
    ImGui::TextWrapped(
        "For a new Blender model, select its meshes and use File > Export > USUMStudio new asset. "
        "Import here to assign game materials. For a new Pokemon entry, create its asset bundle "
        "first; for a reusable map prop, import through Authoring’s asset editor.");
    ImGui::TextWrapped("Re-export from Studio after changing its geometry. Imports validate the "
                       "source and native mesh limits before changing the document.");
    if (!message_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", message_.c_str());
    }
}
}
