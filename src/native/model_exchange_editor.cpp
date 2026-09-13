#include "native/tutorial_widgets.h"
#include "native/model_exchange_editor.h"
#include "assets/blender_preview.h"
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
                    file_ += action >= 3 ? ".usum-motion" : ".usum-model";
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
                    export_blender_model(doc, file_);
                    message_ =
                        "Exported model, preview metadata and textures. Keep the .preview file and "
                        ".textures folder beside the model when importing in Blender.";
                } else {
                    auto bytes = read_file(file_);
                    auto imported = parse_model_exchange(std::string(bytes.begin(), bytes.end()));
                    doc.import_model_exchange(imported);
                    preview = doc.model;
                    preview.select_motion(-1);
                    playing = false;
                    renderer.set_scene(preview.scene);
                    message_ = "Imported geometry and bind skeleton. Review the result, then Save "
                               "Project and Stage Project.";
                }
            }
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    }
    ImGui::TextWrapped(
        "Edit this model in Blender, then bring it back with changed topology, UVs, normals, "
        "weights and bind bones. Motion clips can also be exchanged separately.");
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
        static const SDL_DialogFileFilter model_filters[] = {{"Studio model exchange",
                                                              "usum-model"}},
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
                                                                       : "model.usum-model")
                                        : file_;
        suggestion.replace_extension(motion ? ".usum-motion" : ".usum-model");
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
    ImGui::TextWrapped("Install blender/usum_model.py from the application package. Use File > "
                       "Import / Export > USUM Studio model in Blender. Keep the exported mesh "
                       "objects and existing bone names; vertices and triangles may change.");
    ImGui::TextWrapped("Re-export from Studio after changing its geometry. Imports validate the "
                       "source and native mesh limits before changing the document.");
    if (!message_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", message_.c_str());
    }
}
}
