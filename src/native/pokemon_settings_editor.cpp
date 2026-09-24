#include "native/viewport_navigation.h"
#include "native/tutorial_widgets.h"
#include "native/pokemon_settings_editor.h"
#include "native/imgui_renderer.h"
#include "native/theme.h"
#include "assets/battle_profile.h"
#include "assets/material_document.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <bx/math.h>
#include <sstream>
namespace studio {
void PokemonSettingsEditor::bind(const ModelDocument &model) {
    project_.unbind();
    if (sendout_job_.valid())
        sendout_job_.wait();
    sendout_job_ = {};
    sendout_model_.reset();
    if (stage_job_.valid())
        stage_job_.wait();
    stage_job_ = {};
    stage_.reset();
    model_source_.reset();
    document_.reset();
    path_.clear();
    message_.clear();
    active = false;
    stage_dirty_ = true;
    if (dump_ != model.dump) {
        if (catalog_job_.valid())
            catalog_job_.wait();
        catalog_job_ = {};
        arenas_.clear();
        trainers_.clear();
        catalog_started_ = false;
    }
    dump_ = model.dump;
    archive_ = model.archive_sources.resolve(dump_, TargetProfile::pokemon_archive);
    if (!model.is_pokemon() || model.shadow_model || !model.independent_asset.empty())
        return;
    try {
        Archive archive(archive_);
        auto member = 1 + model.pokemon.data_index * TargetProfile::pokemon_stride +
                      TargetProfile::pokemon_settings_slot;
        document_ =
            std::make_unique<PokemonSettingsDocument>(model.pokemon, archive.decoded(member));
        project_parameters_ = project_model_parameters(model);
        bind_project();
        camera_mode_ = 0;
        camera_fov_ = 30;
        camera_.target = {0, 80, 220};
        camera_.distance = 700;
        camera_.pitch = .2f;
        camera_.yaw = 2.65f;
    } catch (const std::exception &e) {
        message_ = e.what();
    }
}
void PokemonSettingsEditor::dialog(int kind) {
    if (kind == 1 && project_store())
        save_editor_project();
    kind_ = kind;
    if (kind == 3) {
        choose_folder(window_, dialog_, nullptr);
        return;
    }
    auto *owner = new std::shared_ptr<FolderSelection>(dialog_);
    static SDL_DialogFileFilter filter[] = {{"Pokemon settings", "usum-pokemon"}};
    auto callback = [](void *data, const char *const *paths, int) {
        std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
            static_cast<std::shared_ptr<FolderSelection> *>(data));
        auto &result = **owner;
        std::lock_guard lock(result.mutex);
        result.path.clear();
        result.error.clear();
        if (!paths)
            result.error = SDL_GetError();
        else if (paths[0])
            result.path = paths[0];
        result.ready = true;
    };
    if (kind == 1)
        SDL_ShowSaveFileDialog(callback, owner, window_, filter, 1, nullptr);
    else
        SDL_ShowOpenFileDialog(callback, owner, window_, filter, 1, nullptr, false);
}
void PokemonSettingsEditor::save() {
    if (save_editor_project()) {
        message_ = "Project saved. Stage Project builds the overlay.";
        return;
    }
    if (path_.empty()) {
        dialog(1);
        return;
    }
    auto text = document_->serialize();
    write_file_atomic(path_,
                      View(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
    document_->mark_saved();
    message_ = "Pokemon settings saved.";
    if (save_leave_) {
        save_leave_ = false;
        auto action = std::move(leave_action_);
        if (action)
            action();
    }
}
void PokemonSettingsEditor::open(const std::filesystem::path &path) {
    require(bool(document_), "Open the originating Pokemon first");
    require(std::filesystem::file_size(path) <= 2 * 1024 * 1024, "Settings document is too large");
    document_->restore(text(read_file(path)));
    project_.imported();
    path_ = path;
    message_ = "Pokemon settings reopened.";
}
void PokemonSettingsEditor::request_leave(std::function<void()> action) {
    if (project_store()) {
        try {
            save_editor_project();
            action();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
        return;
    }
    if (kind_ || export_.valid()) {
        message_ = "Wait for the Pokemon settings file operation to finish.";
        return;
    }
    if (document_ && document_->dirty()) {
        document_->commit();
        leave_action_ = std::move(action);
        leave_ = true;
    } else
        action();
}
void PokemonSettingsEditor::update() {
    if (export_.valid() && export_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            export_.get();
            message_ = "Pokemon GARC exported; only this form's settings member changed.";
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    std::string path, error;
    bool ready = false;
    {
        std::lock_guard lock(dialog_->mutex);
        if (dialog_->ready) {
            ready = true;
            dialog_->ready = false;
            path = dialog_->path;
            error = dialog_->error;
        }
    }
    if (ready) {
        auto kind = kind_;
        kind_ = 0;
        try {
            require(error.empty(), error);
            if (path.empty()) {
                save_leave_ = false;
                return;
            }
            auto file = std::filesystem::u8path(path);
            if (kind == 1) {
                auto previous = path_;
                path_ = file;
                if (path_.extension() != ".usum-pokemon")
                    path_ += ".usum-pokemon";
                try {
                    save();
                } catch (...) {
                    path_ = previous;
                    throw;
                }
            } else if (kind == 2)
                open(file);
            else if (kind == 3) {
                auto snapshot = *document_;
                auto source = archive_;
                auto output = file / TargetProfile::pokemon_archive;
                export_ = std::async(std::launch::async, [snapshot, source, output] {
                    snapshot.export_archive(source, output);
                });
                message_ = "Exporting Pokemon settings...";
            }
        } catch (const std::exception &e) {
            save_leave_ = false;
            message_ = e.what();
        }
    }
    if (leave_) {
        ImGui::OpenPopup("Unsaved Pokemon settings");
        leave_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Pokemon settings", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save this Pokemon's settings before continuing?");
        if (studio::TutorialWidgets::Button("pokemon_settings_editor",
                                            project_store() ? "Save Project" : "Save settings")) {
            ImGui::CloseCurrentPopup();
            save_leave_ = true;
            try {
                save();
            } catch (const std::exception &e) {
                message_ = e.what();
                save_leave_ = false;
            }
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Discard settings")) {
            ImGui::CloseCurrentPopup();
            document_->discard();
            auto action = std::move(leave_action_);
            if (action)
                action();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Cancel")) {
            ImGui::CloseCurrentPopup();
            leave_action_ = {};
        }
        ImGui::EndPopup();
    }
}
void PokemonSettingsEditor::draw() {
    if (!document_) {
        ImGui::TextWrapped("Settings unavailable: %s", message_.c_str());
        return;
    }
    bool busy = kind_ || export_.valid();
    ImGui::TextWrapped("%s", document_->pokemon.label.c_str());
    ImGui::TextUnformatted(document_->dirty() ? "Settings have unsaved changes"
                                              : "Settings saved / unchanged");
    ImGui::BeginDisabled(busy);
    if (primary_button(project_store() ? "Save Project" : "Save settings"))
        try {
            save();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    ImGui::SameLine();
    if (!project_store() &&
        studio::TutorialWidgets::Button("pokemon_settings_editor", "Save as..."))
        dialog(1);
    if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Open settings..."))
        request_leave([this] {
            dialog(2);
        });
    if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Export Pokemon GARC...",
                                        {-1, 0}))
        dialog(3);
    ImGui::BeginDisabled(!document_->can_undo());
    if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Undo##settings"))
        document_->undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_->can_redo());
    if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Redo##settings"))
        document_->redo();
    ImGui::EndDisabled();
    if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Reset all settings"))
        document_->reset();
    if (!project_store())
        ImGui::TextWrapped(
            "Save settings separately from Studio materials. Export writes to a new folder.");
    studio::TutorialWidgets::Checkbox("pokemon_settings_editor", "Battle arena preview", &active);
    ImGui::PushItemWidth(-1);
    auto next = document_->settings();
    bool changed = false, commit = false;
    auto done = [&](bool edit) {
        changed |= edit;
        commit |= ImGui::IsItemDeactivatedAfterEdit();
    };
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (studio::TutorialWidgets::CollapsingHeader("pokemon_settings_editor",
                                                  "Size and placement")) {
        ImGui::TextUnformatted("Base height (cm)");
        done(ImGui::InputInt("##base-height", &next.heights[0]));
        ImGui::TextUnformatted("Adjusted height (cm)");
        done(ImGui::InputInt("##adjusted-height", &next.heights[1]));
        ImGui::TextWrapped("Adjusted height 0 = automatic. Current battle scale: %.3fx",
                           pokemon_adjusted_scale(next));
        ImGui::TextUnformatted("Field height (cm)");
        done(ImGui::InputInt("##field-height", &next.heights[2]));
        ImGui::TextUnformatted("Size category");
        int size = int(next.size);
        if (ImGui::Combo("##size-category", &size, "Small\0Medium\0Large\0")) {
            next.size = unsigned(size);
            changed = commit = true;
        }
        ImGui::TextWrapped("Size category affects battle position. Ground clearance follows the "
                           "source model and animations.");
    }
    if (studio::TutorialWidgets::CollapsingHeader("pokemon_settings_editor", "Model bounds")) {
        ImGui::TextUnformatted("Minimum XYZ");
        done(ImGui::InputFloat3("##bounds-min", next.bounds.data()));
        ImGui::TextUnformatted("Maximum XYZ");
        done(ImGui::InputFloat3("##bounds-max", next.bounds.data() + 3));
        ImGui::TextWrapped("Stored bounds help framing and spacing. They do not move vertices or "
                           "raise the Pokemon.");
    }
    if (studio::TutorialWidgets::CollapsingHeader("pokemon_settings_editor",
                                                  "Model-viewer framing")) {
        const char *labels[] = {"Framing size", "Vertical framing offset", "Yaw (degrees)",
                                "Right-side yaw offset", "Camera pitch (degrees)"};
        for (unsigned i = 0; i < 5; ++i) {
            ImGui::PushID(int(i));
            ImGui::TextUnformatted(labels[i]);
            done(ImGui::InputFloat("##framing-value", &next.camera[i], .01f, .1f, "%.3f"));
            ImGui::PopID();
        }
        ImGui::TextWrapped("These settings control framed model viewers. They do not set the "
                           "battle camera or floor clearance.");
    }
    if (studio::TutorialWidgets::CollapsingHeader("pokemon_settings_editor", "Idle variations")) {
        ImGui::TextUnformatted("Idle B rate / 1000");
        done(ImGui::InputInt("##idle-b", &next.idle[0]));
        ImGui::TextUnformatted("Idle C rate / 1000");
        done(ImGui::InputInt("##idle-c", &next.idle[1]));
        ImGui::TextWrapped("The game tests these rates after Idle A. Select each idle in the "
                           "Motion list to inspect it.");
    }
    auto flags = [&](const char *title, auto &values, const std::array<const char *, 8> &labels) {
        if (studio::TutorialWidgets::CollapsingHeader("pokemon_settings_editor", title)) {
            for (unsigned i = 0; i < 8; ++i) {
                bool enabled = values[i] != 0;
                if (studio::TutorialWidgets::Checkbox("pokemon_settings_editor", labels[i],
                                                      &enabled)) {
                    values[i] = enabled ? 1 : 0;
                    changed = commit = true;
                }
            }
            ImGui::TextWrapped("Availability flags do not create missing expression animations.");
        }
    };
    flags("Eye expressions", next.eyes,
          {"Open", "Half open", "Closed", "Tightly closed", "Angry", "Smiling", "Sad",
           "Other eye expression"});
    flags("Mouth expressions", next.mouths,
          {"Closed mouth", "Small opening", "Large opening", "Neutral closed", "Triangle opening",
           "Downturned closed", "Other mouth 1", "Other mouth 2"});
    if (studio::TutorialWidgets::CollapsingHeader("pokemon_settings_editor",
                                                  "Shiny material colors")) {
        ImGui::TextWrapped("Offsets add to material constants, in addition to shiny textures. "
                           "Existing bindings are retained.");
        if (next.shiny.empty())
            ImGui::TextUnformatted("No shiny material-color entries in this form.");
        for (unsigned i = 0; i < next.shiny.size(); ++i) {
            ImGui::PushID(int(i));
            auto &color = next.shiny[i];
            if (ImGui::TreeNode(color.material.c_str())) {
                int slot = int(color.slot);
                ImGui::TextUnformatted("Constant slot");
                done(ImGui::InputInt("##constant-slot", &slot));
                color.slot = unsigned(slot);
                ImGui::TextUnformatted("RGBA offsets");
                done(ImGui::InputFloat4("##shiny-offsets", color.offset.data(), "%.4f"));
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    if (changed)
        try {
            document_->preview(next);
            message_.clear();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    if (commit)
        document_->commit();
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
}
void PokemonSettingsEditor::load_stage() {
    if (stage_job_.valid())
        return;
    auto dump = dump_;
    std::vector<unsigned> parts;
    for (int member : arena_)
        if (member >= 0)
            parts.push_back(unsigned(member));
    auto trainer = trainer_;
    stage_job_ = std::async(std::launch::async, [dump, parts, trainer] {
        return load_battle_stage(dump, parts, trainer);
    });
    stage_dirty_ = false;
}
void PokemonSettingsEditor::viewport(const ModelDocument &model, double seconds) {
    if (!document_) {
        ImGui::TextUnformatted("Pokemon settings unavailable.");
        return;
    }
    ImGui::PushID("battle-preview");
    auto motion_index = [&](unsigned slot) {
        for (unsigned i = 0; i < model.motions.size(); ++i) {
            auto &m = model.motions[i];
            if (m.group == 0 && m.slot == slot && m.error.empty() && m.skeletal.frames > 0 &&
                !m.skeletal.tracks.empty())
                return int(i);
        }
        return -1;
    };
    auto motion_frames = [&](unsigned slot) {
        int i = motion_index(slot);
        return i < 0 ? 0.f : model.motions[i].skeletal.frames;
    };
    if (sendout_job_.valid() &&
        sendout_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            sendout_ = sendout_job_.get();
            sendout_model_ = std::make_unique<ModelDocument>(model);
            sendout_model_->scene = std::make_shared<Environment>(*model.scene);
            sendout_model_->motion = -1;
            sendout_frame_ = 0;
            sendout_playing_ = true;
            far_ = false;
            camera_mode_ = 0;
            message_.clear();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    if (sendout_model_ && model_source_ && model_source_ != model.scene) {
        sendout_model_.reset();
        message_ = "Send-out stopped because the model changed.";
    }
    if (sendout_model_) {
        float duration =
            std::max(sendout_.frames, motion_frames(3) > 0 ? motion_frames(3) + 5 + motion_frames(5)
                                                           : motion_frames(6));
        ImGui::TextUnformatted("Single battle send-out: Pokemon and camera");
        if (studio::TutorialWidgets::Button("pokemon_settings_editor",
                                            sendout_playing_ ? "Pause send-out" : "Play send-out"))
            sendout_playing_ = !sendout_playing_;
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Restart send-out")) {
            sendout_frame_ = 0;
            sendout_playing_ = true;
        }
        if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Stop send-out")) {
            sendout_model_.reset();
            camera_mode_ = 0;
        } else {
            if (sendout_playing_ && renderer_ && renderer_->ready()) {
                sendout_frame_ = std::min(
                    duration, sendout_frame_ + std::min(ImGui::GetIO().DeltaTime, .1f) * 30);
                if (sendout_frame_ >= duration)
                    sendout_playing_ = false;
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##sendout-time", &sendout_frame_, 0, duration, "%.1f frames"))
                sendout_playing_ = false;
            ImGui::TextWrapped("Starts at Pokemon release. Trainer throw and ball effects are not "
                               "included. Framing currently uses model bounds.");
        }
    }

    if (!catalog_started_) {
        catalog_started_ = true;
        auto dump = dump_;
        catalog_job_ = std::async(std::launch::async, [dump] {
            return std::pair{load_battle_catalog(dump), load_battle_catalog(dump, true)};
        });
    }
    if (catalog_job_.valid() &&
        catalog_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            auto catalogs = catalog_job_.get();
            arenas_ = std::move(catalogs.first);
            trainers_ = std::move(catalogs.second);
            for (unsigned part = 0; part < 4; ++part) {
                const char *names[] = {"btl_G_kusa", "btl_N_kusa", "btl_F_kusa", "btl_B_kusa"};
                auto it = std::find_if(arenas_.begin(), arenas_.end(), [&](auto &entry) {
                    return entry.name == names[part];
                });
                arena_[part] = it == arenas_.end() ? -1 : int(it->member);
            }
            trainer_ = trainers_.empty() ? -1 : int(trainers_.front().member);
            stage_dirty_ = true;
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    if (studio::TutorialWidgets::CollapsingHeader("pokemon_settings_editor", "Arena and trainer")) {
        auto combo = [&](const char *title, const auto &entries, int &selected,
                         const char *prefix) {
            auto current = std::find_if(entries.begin(), entries.end(), [&](auto &e) {
                return int(e.member) == selected;
            });
            ImGui::SetNextItemWidth(220);
            if (ImGui::BeginCombo(title,
                                  current == entries.end() ? "None" : current->name.c_str())) {
                if (ImGui::Selectable("None", selected < 0)) {
                    selected = -1;
                    stage_dirty_ = true;
                }
                for (auto &e : entries)
                    if (!prefix || e.name.starts_with(prefix)) {
                        ImGui::PushID(int(e.member));
                        if (ImGui::Selectable(e.name.c_str(), selected == int(e.member))) {
                            selected = int(e.member);
                            stage_dirty_ = true;
                        }
                        ImGui::PopID();
                    }
                ImGui::EndCombo();
            }
        };
        combo("Ground", arenas_, arena_[0], "btl_G_");
        combo("Nearby scenery", arenas_, arena_[1], "btl_N_");
        combo("Distant scenery", arenas_, arena_[2], "btl_F_");
        combo("Arena structures", arenas_, arena_[3], "btl_B_");
        combo("Trainer", trainers_, trainer_, nullptr);
        ImGui::TextWrapped(
            "Game arena parts can be mixed for preview. Trainer uses a battle idle and standard "
            "single-battle placement; extra clearance for long models is approximate.");
    }
    if (stage_dirty_ && !arenas_.empty())
        load_stage();
    if (stage_job_.valid() &&
        stage_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            auto next = std::make_shared<Environment>(stage_job_.get());
            std::optional<ModelDocument> shadow;
            if (model.has_shadow_model) {
                auto source = model;
                source.shadow_model = true;
                shadow = reload_pokemon(source, {});
                MaterialDocument materials(*shadow);
                for (unsigned i = 0; i < shadow->scene->materials.size(); ++i) {
                    auto color = materials.edits()[i].colors[0];
                    color[0] = .5f;
                    auto &material = shadow->scene->materials[i];
                    for (unsigned slot = 0; slot < 6; ++slot)
                        if (material.constant_assignments[slot] == 5)
                            material.combiner.stages[slot].constant = color;
                }
            }
            auto range =
                append_battle_pokemon(*next, *model.scene, shadow ? shadow->scene.get() : nullptr);
            if (!renderer_)
                renderer_ = std::make_unique<EnvironmentRenderer>(shaders_);
            renderer_->lighting.enabled = true;
            renderer_->lighting.game = false;
            renderer_->lighting.soft = true;
            renderer_->lighting.ambient = .75f;
            renderer_->lighting.strength = .25f;
            renderer_->fog_enabled = false;
            renderer_->sky_enabled = false;
            renderer_->set_scene(next);
            stage_ = std::move(next);
            range_ = range;
            model_source_ = model.scene;
            message_.clear();
        } catch (const std::exception &e) {
            stage_.reset();
            model_source_.reset();
            message_ = std::string("Battle preview failed: ") + e.what();
        }
    if (stage_ && model_source_ != model.scene) {
        stage_.reset();
        stage_dirty_ = true;
    }
    if (!stage_ || !renderer_) {
        ImGui::TextWrapped("%s",
                           message_.empty() ? "Loading game battle assets..." : message_.c_str());
        if (!message_.empty() && !stage_job_.valid() &&
            studio::TutorialWidgets::Button("pokemon_settings_editor", "Retry battle preview"))
            stage_dirty_ = true;
        ImGui::PopID();
        return;
    }
    if (!sendout_model_) {
        ImGui::BeginDisabled(sendout_job_.valid());
        if (studio::TutorialWidgets::Button("pokemon_settings_editor", sendout_job_.valid()
                                                                           ? "Loading send-out..."
                                                                           : "Preview send-out")) {
            try {
                bool jump = motion_frames(3) > 0;
                require((jump && motion_frames(4) > 0 && motion_frames(5) > 0) ||
                            (!jump && motion_frames(6) > 0),
                        "This Pokemon lacks a complete entrance animation set");
                auto dump = dump_;
                sendout_job_ = std::async(std::launch::async, [dump, jump] {
                    return load_battle_sendout(dump, !jump);
                });
            } catch (const std::exception &e) {
                message_ = e.what();
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::BeginDisabled(range_.shadow_count == 0);
    studio::TutorialWidgets::Checkbox("pokemon_settings_editor", "Pokemon shadow", &shadows);
    ImGui::EndDisabled();
    if (range_.shadow_count == 0)
        ImGui::TextUnformatted("No shadow model for this Pokemon.");
    if (range_.shadow_count &&
        studio::TutorialWidgets::TreeNode("pokemon_settings_editor", "Shadow lighting")) {
        ImGui::SliderFloat("Elevation", &shadow_elevation_, 5, 90, "%.0f degrees");
        ImGui::SliderFloat("Azimuth", &shadow_azimuth_, -180, 180, "%.0f degrees");
        if (studio::TutorialWidgets::Button("pokemon_settings_editor", "Game indoor light")) {
            shadow_elevation_ = 50;
            shadow_azimuth_ = 40;
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("pokemon_settings_editor", "High sun")) {
            shadow_elevation_ = 70;
            shadow_azimuth_ = 0;
        }
        ImGui::TextWrapped("Preview only. Outdoor shadow direction changes with the game's sun and "
                           "time. Projects the animated shadow mesh onto flat ground.");
        ImGui::TreePop();
    }
    ImGui::BeginDisabled(bool(sendout_model_));
    ImGui::SetNextItemWidth(160);
    bool camera_changed =
        ImGui::Combo("Camera", &camera_mode_, "Game battle\0Front\0Side\0Overview\0Free camera\0");
    if (ImGui::GetContentRegionAvail().x >= 360)
        ImGui::SameLine();
    camera_changed |=
        studio::TutorialWidgets::Checkbox("pokemon_settings_editor", "Far side", &far_);
    ImGui::EndDisabled();
    if (ImGui::GetContentRegionAvail().x >= 450)
        ImGui::SameLine();
    studio::TutorialWidgets::Checkbox("pokemon_settings_editor", "Outlines", &renderer_->outlines);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Game-style material outlines. Preview only; does not change exported assets.");
    ImGui::BeginDisabled(bool(sendout_model_));
    if (camera_mode_ == 0) {
        ImGui::SetNextItemWidth(160);
        ImGui::Combo("Other side size", &other_size_, "Small\0Medium\0Large\0");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Preview only: the other Pokemon's size category selects the game "
                              "camera. No opposing model is added.");
        auto own = document_->settings().size;
        auto preset = BattleProfile::single_camera(far_ ? unsigned(other_size_) : own,
                                                   far_ ? own : unsigned(other_size_));
        camera_.target = preset.target;
        float x = preset.eye[0] - preset.target[0], y = preset.eye[1] - preset.target[1],
              z = preset.eye[2] - preset.target[2];
        camera_.distance = std::sqrt(x * x + y * y + z * z);
        camera_.yaw = std::atan2(x, z);
        camera_.pitch = std::asin(y / camera_.distance);
        camera_fov_ = preset.fov;
    } else if (camera_changed && camera_mode_ != 4) {
        camera_fov_ = 45;
        camera_.target = {0, 90, far_ ? -250.f : 250.f};
        if (camera_mode_ == 1) {
            camera_.yaw = far_ ? 0 : 3.14159265f;
            camera_.pitch = .1f;
            camera_.distance = 650;
        } else if (camera_mode_ == 2) {
            camera_.yaw = 1.5707963f;
            camera_.pitch = .05f;
            camera_.distance = 800;
        } else {
            camera_.target = {0, 90, 0};
            camera_.yaw = .6f;
            camera_.pitch = .4f;
            camera_.distance = 1200;
        }
    }
    ImGui::EndDisabled();
    try {
        auto &settings = document_->settings();
        std::optional<BattleCameraPose> shot;
        float center = std::max(0.f, (settings.bounds[1] + settings.bounds[4]) * .5f *
                                         pokemon_adjusted_scale(settings));
        if (sendout_model_) {
            seconds = sendout_frame_ / 30.;
            auto phase = battle_sendout_phase(sendout_frame_, motion_frames(3), motion_frames(5),
                                              motion_frames(6), center);
            int motion = motion_index(phase.slot);
            if (motion != sendout_model_->motion)
                sendout_model_->select_motion(motion, phase.loop);
            update_battle_pokemon(*stage_, *sendout_model_->scene, range_, settings, model.shiny,
                                  false);
            for (auto i = range_.skeletons; i < stage_->skeletons.size(); ++i)
                stage_->skeletons[i].seconds_offset = -phase.start / 30.;
            for (auto i = range_.material_motions; i < stage_->material_animations.size(); ++i)
                stage_->material_animations[i].seconds_offset = -phase.start / 30.;
            for (auto i = range_.visibility; i < stage_->visibility_animations.size(); ++i)
                stage_->visibility_animations[i].seconds_offset = -phase.start / 30.;
            auto &transform = stage_->placement_transforms.back();
            transform[0] *= phase.growth;
            transform[5] *= phase.growth;
            transform[10] *= phase.growth;
            transform[7] += phase.height;
            if (sendout_frame_ < sendout_.camera_end) {
                auto *event = &sendout_.cameras.front();
                for (auto &c : sendout_.cameras)
                    if (c.start <= sendout_frame_)
                        event = &c;
                shot = event->motion.sample((sendout_frame_ - event->start) * event->speed);
                float height = (settings.heights[0] ? settings.heights[0] * .01f : 1.f) *
                               pokemon_adjusted_scale(settings);
                float scale = event->scale ? (1 + height) * .5f : 1;
                float anchor = event->node == 50   ? (motion_frames(3) > 0 ? 200 : center)
                               : event->node == 15 ? center
                                                   : 0;
                for (unsigned i = 0; i < 3; ++i) {
                    float offset = i == 1 ? anchor
                                   : i == 2
                                       ? TargetProfile::battle_pokemon_distances.at(settings.size)
                                       : 0;
                    shot->eye[i] = shot->eye[i] * scale + offset;
                    shot->target[i] = shot->target[i] * scale + offset;
                }
            }
        } else
            update_battle_pokemon(*stage_, *model.scene, range_, settings, model.shiny, far_);
        update_battle_shadow(*stage_, range_, far_, shadow_elevation_, shadow_azimuth_);
        for (auto i = range_.shadow_draws; i < range_.shadow_draws + range_.shadow_count; ++i)
            renderer_->set_draw_visible(i, shadows);
        renderer_->refresh_materials();
        renderer_->upload_step();
        renderer_->playback.seconds = seconds;
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
        auto size = ImGui::GetContentRegionAvail();
        size.x = std::max(1.f, size.x);
        size.y = std::max(1.f, size.y);
        if (camera_mode_ == 0) {
            auto available = size;
            size.x = std::min(available.x, available.y * 5.f / 3.f);
            size.y = size.x * 3.f / 5.f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - size.x) * .5f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (available.y - size.y) * .5f);
        }
        auto &io = ImGui::GetIO();
        unsigned width = unsigned(std::max(1.f, size.x * io.DisplayFramebufferScale.x)),
                 height = unsigned(std::max(1.f, size.y * io.DisplayFramebufferScale.y));
        float view[16], projection[16];
        auto eye = shot ? shot->eye : camera_.eye(), target = shot ? shot->target : camera_.target,
             up = shot ? shot->up : std::array<float, 3>{0, 1, 0};
        bx::mtxLookAt(view, {eye[0], eye[1], eye[2]}, {target[0], target[1], target[2]},
                      {up[0], up[1], up[2]}, bx::Handedness::Right);
        bx::mtxProj(projection, shot ? shot->fov : camera_fov_, float(width) / height,
                    shot ? shot->near_clip : 1, shot ? shot->far_clip : 100000,
                    bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
        auto texture = renderer_->render(width, height, view, projection, true, false, 0, false);
        bool flip = bgfx::getCaps()->originBottomLeft;
        ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false)),
                     {std::max(1.f, size.x), std::max(1.f, size.y)}, {0, flip ? 1.f : 0.f},
                     {1, flip ? 0.f : 1.f});
        if (!sendout_model_ && ImGui::IsItemHovered() && !io.WantTextInput) {
            if (viewport_navigating() || io.MouseWheel != 0)
                camera_mode_ = 4;
            viewport_navigation(camera_, window_, true);
        }
    } catch (const std::exception &e) {
        message_ = std::string("Battle preview failed: ") + e.what();
        stage_.reset();
        model_source_.reset();
    }
    ImGui::PopID();
}
std::string PokemonSettingsEditor::report() const {
    std::ostringstream out;
    if (document_)
        out << "Pokemon settings member: " << document_->member()
            << "\nPokemon settings dirty: " << document_->dirty()
            << "\nBattle scale: " << pokemon_adjusted_scale(document_->settings()) << '\n';
    out << "Battle camera mode: " << camera_mode_ << "\nBattle camera FOV: " << camera_fov_
        << "\nBattle other size: " << other_size_ << "\nBattle camera eye: " << camera_.eye()[0]
        << "," << camera_.eye()[1] << "," << camera_.eye()[2] << '\n';
    out << "Send-out active: " << bool(sendout_model_) << "\nSend-out frame: " << sendout_frame_
        << '\n';
    out << "Battle preview: " << active << '\n';
    if (stage_)
        out << "Battle arena draws: " << range_.shadow_draws
            << "\nBattle shadow draws: " << range_.shadow_count
            << "\nBattle shadow enabled: " << shadows << "\nBattle trainer member: " << trainer_
            << '\n';
    if (!message_.empty())
        out << message_ << '\n';
    return out.str();
}
void PokemonSettingsEditor::bind_project() {
    if (!document_)
        return;
    project_.bind(
        "pokemon-settings", "pokemon-settings/" + std::to_string(document_->pokemon.data_index),
        document_->pokemon.label + " settings", project_parameters_,
        [this] {
            return document_ && document_->dirty();
        },
        [this] {
            require(!kind_ && !export_.valid(),
                    "Finish the Pokemon settings operation before saving");
            document_->commit();
            return project_text(document_->serialize());
        },
        [this] {
            document_->mark_saved();
        });
    project_.ready([this] {
        if (document_) {
            require(!kind_ && !export_.valid(),
                    "Finish the Pokemon settings operation before saving");
        }
    });
    if (auto file = project_.document(); !file.empty()) {
        document_->restore(text(read_file(file)));
    }
}
}
