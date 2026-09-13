#include "native/tutorial_widgets.h"
#include "native/camera_editor.h"
#include "formats/archive.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <set>
#include "native/imgui_renderer.h"
#include <bx/math.h>
namespace studio {
void CameraEditor::set_scene(std::shared_ptr<Environment> scene, unsigned area,
                             const std::filesystem::path &dump) {
    scene_ = std::move(scene);
    view_.fit(scene_->low, scene_->high);
    renderer_.player.camera_override = -1;
    renderer_.player.camera_variables.clear();
    dump_ = dump;
    project_area_ = area;
    path_.clear();
    player_started_ = false;
    group_.clear();
    message_.clear();
    document_.reset();
    try {
        Archive source(scene_->archive_sources.resolve(dump, TargetProfile::field_archive));
        document_ = std::make_unique<CameraDocument>(
            area, source.decoded(area * TargetProfile::area_stride + TargetProfile::camera_slot));
        bind_project();
    } catch (const std::exception &e) {
        message_ = e.what();
    }
}
void CameraEditor::open_patch(const std::filesystem::path &path) {
    require(bool(document_), "Load the originating camera map first");
    require(std::filesystem::file_size(path) < 64 * 1024 * 1024, "Camera patch is too large");
    document_->restore(text(read_file(path)));
    project_.imported();
    path_ = path;
    synchronize();
    message_ = "Camera patch opened.";
}
void CameraEditor::synchronize() {
    if (!document_)
        return;
    document_->apply(scene_->spatial);
    renderer_.spatial.invalidate_geometry();
    renderer_.invalidate_selection_readback();
}
void CameraEditor::dialog(int kind) {
    if (kind == 1 && project_store())
        save_editor_project();
    kind_ = kind;
    if (kind == 3) {
        choose_folder(window_, dialog_, nullptr);
        return;
    }
    auto *owner = new std::shared_ptr<FolderSelection>(dialog_);
    static SDL_DialogFileFilter filter[] = {{"Camera patch", "usum-camera"}};
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
void CameraEditor::save() {
    if (save_editor_project()) {
        message_ = "Project saved. Stage Project builds the overlay.";
        return;
    }
    if (path_.empty()) {
        dialog(1);
        return;
    }
    document_->save(path_);
    message_ = "Camera patch saved.";
    if (save_leave_) {
        save_leave_ = false;
        auto action = std::move(leave_action_);
        if (action)
            action();
    }
}
void CameraEditor::request_leave(std::function<void()> action) {
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
        message_ = "Wait for the camera file operation to finish.";
        return;
    }
    if (document_ && document_->dirty()) {
        focus_requested_ = true;
        document_->commit();
        leave_action_ = std::move(action);
        leave_ = true;
    } else
        action();
}
void CameraEditor::update() {
    if (export_.valid() && export_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            export_.get();
            message_ = "Camera field GARC exported. In-game validation remains pending.";
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
                path_ = file;
                if (path_.extension() != ".usum-camera")
                    path_ += ".usum-camera";
                save();
            } else if (kind == 2) {
                require(std::filesystem::file_size(file) < 64 * 1024 * 1024,
                        "Camera patch is too large");
                document_->restore(text(read_file(file)));
                project_.imported();
                path_ = file;
                synchronize();
                message_ = "Camera patch opened.";
            } else if (kind == 3) {
                auto snapshot = *document_;
                auto dump = dump_;
                auto sources = scene_->archive_sources;
                export_ = std::async(std::launch::async, [snapshot, dump, sources, file] {
                    snapshot.export_to(dump, sources, file);
                });
            }
        } catch (const std::exception &e) {
            message_ = e.what();
            save_leave_ = false;
        }
    }
    if (leave_) {
        ImGui::OpenPopup("Unsaved camera edits");
        leave_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved camera edits", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save camera edits before continuing?");
        if (studio::TutorialWidgets::Button("camera_editor", "Save")) {
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
        if (studio::TutorialWidgets::Button("camera_editor", "Discard")) {
            ImGui::CloseCurrentPopup();
            auto action = std::move(leave_action_);
            if (action)
                action();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("camera_editor", "Cancel")) {
            leave_action_ = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
void CameraEditor::draw(bool loading, ViewportCamera &view) {
    ImGui::Begin("Camera viewport");
    if (!document_) {
        ImGui::TextWrapped("Load a map with camera data to edit its cameras.");
        ImGui::End();
        return;
    }
    bool busy = loading || kind_ || export_.valid();
    ImGui::BeginDisabled(busy);
    auto &player = renderer_.player;
    if (studio::TutorialWidgets::Button("camera_editor",
                                        player.active ? "Free camera" : "Player preview")) {
        if (player.active) {
            player.active = false;
        } else if (player_started_) {
            player.active = true;
        } else {
            auto spawn = scene_->start_position();
            if (spawn)
                player_started_ = player.start(scene_->spatial, *spawn, -1);
            else
                message_ = "Choose a map start in Area before starting player mode.";
        }
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("camera_editor", "Restart player")) {
        if (auto spawn = scene_->start_position())
            player_started_ = player.start(scene_->spatial, *spawn, -1);
    }
    if (player.active)
        player_started_ = true;
    ImGui::SameLine();
    studio::TutorialWidgets::Checkbox("camera_editor", "Pause player", &player.paused);
    ImGui::SameLine();
    studio::TutorialWidgets::Checkbox("camera_editor", "Follow active camera", &follow_);
    if (player.active)
        ImGui::Text("Region %d | Camera %d | Blend %.2f", player.camera_region,
                    player.camera_setting, player.camera_ratio);
    ImGui::EndDisabled();
    ImGui::End();
    ImGui::Begin("Camera status");
    ImGui::BeginDisabled(busy);
    try {
        ImGui::BeginDisabled(!document_->can_undo());
        if (studio::TutorialWidgets::Button("camera_editor", "Undo")) {
            document_->undo();
            synchronize();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_->can_redo());
        if (studio::TutorialWidgets::Button("camera_editor", "Redo")) {
            document_->redo();
            synchronize();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("camera_editor",
                                            project_store() ? "Save Project" : "Save patch"))
            save();
        ImGui::SameLine();
        if (!project_store() && studio::TutorialWidgets::Button("camera_editor", "Save as..."))
            dialog(1);
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("camera_editor", "Open patch..."))
            request_leave([this] {
                dialog(2);
            });
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_->changed());
        if (studio::TutorialWidgets::Button("camera_editor", "Export camera GARC...")) {
            document_->commit();
            dialog(3);
        }
        ImGui::EndDisabled();
    } catch (const std::exception &e) {
        message_ = e.what();
    }
    ImGui::TextUnformatted(document_->dirty() ? "Unsaved camera changes"
                                              : "Camera patch saved / unchanged");
    ImGui::EndDisabled();
    ImGui::End();
    ImGui::Begin("Camera editing");
    ImGui::BeginDisabled(busy);
    const char *categories[] = {"Cameras", "Circles", "Trigger triangles", "Scroll stops",
                                "Replacement rules"};
    ImGui::Combo("Edit", &category_, categories, 5);
    std::set<std::string> groups;
    for (auto &f : document_->fields()) {
        bool match = category_ == 0   ? f.group.starts_with("Camera ")
                     : category_ == 1 ? f.group.starts_with("Circle ")
                     : category_ == 2 ? f.group.starts_with("Trigger ")
                     : category_ == 3
                         ? (f.group.starts_with("Clamp ") || f.group.starts_with("Keep-out "))
                         : f.group.starts_with("Replacement ");
        if (match)
            groups.insert(f.group);
    }
    if (follow_ && category_ == 0 && player.active && player.camera_setting >= 0)
        for (auto &group : groups)
            if (group.starts_with("Camera " + std::to_string(player.camera_setting) + " /"))
                group_ = group;
    if (!groups.contains(group_))
        group_ = groups.empty() ? "" : *groups.begin();
    if (ImGui::BeginCombo("Selection", group_.c_str())) {
        for (auto &group : groups)
            if (ImGui::Selectable(group.c_str(), group == group_)) {
                group_ = group;
                follow_ = false;
            }
        ImGui::EndCombo();
    }
    if (category_ == 0 && player.camera_setting == 65535)
        ImGui::TextWrapped("The player is using a shared zone default. Shared defaults are "
                           "read-only here; select a local camera setting.");
    if (category_ == 0 &&
        studio::TutorialWidgets::TreeNode("camera_editor", "Preview game variables")) {
        static int variable = 1, value = 0;
        ImGui::InputInt("Variable", &variable);
        ImGui::InputInt("Value", &value);
        if (studio::TutorialWidgets::Button("camera_editor", "Set simulated value") &&
            variable > 0 && value >= 0)
            player.camera_variables[variable] = unsigned(value);
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("camera_editor", "Clear simulated values"))
            player.camera_variables.clear();
        ImGui::TextWrapped(
            "Unspecified variables are 0. These preview values are not saved into game files.");
        ImGui::TreePop();
    }
    if (category_ == 0 && !group_.empty()) {
        auto number = std::stoi(group_.substr(7));
        bool preview = player.camera_override >= 0;
        if (studio::TutorialWidgets::Checkbox("camera_editor", "Preview selected camera",
                                              &preview)) {
            player.camera_override = preview ? number : -1;
            if (preview)
                follow_ = false;
        }
        if (preview) {
            player.camera_override = number;
            ImGui::SliderFloat("A to B preview", &player.camera_override_blend, 0, 1);
            ImGui::TextWrapped("Manual preview overrides trigger selection and game conditions.");
        }
        if (studio::TutorialWidgets::TreeNode("camera_editor", "Capture view")) {
            static int endpoint = 0;
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##Capture endpoint", &endpoint, "A\0B\0");
            if (studio::TutorialWidgets::Button("camera_editor", "Capture current view")) {
                try {
                    auto eye = view.eye();
                    auto target = view.target;
                    auto prefix = std::string(endpoint ? "B" : "A");
                    for (auto &f : document_->fields())
                        if (f.group == group_) {
                            auto component = std::string("XYZ").find(f.label.back());
                            if (f.label == prefix + " uses zone default")
                                document_->set(f, 0);
                            else if (f.label == "Track player")
                                document_->set(f, 0);
                            else if (f.label == prefix + " distance")
                                document_->set(f, view.distance);
                            else if (f.label == prefix + " FOV" || f.label == "FOV")
                                document_->set(f, player.active ? player.camera.fov : 45);
                            else if (component < 3) {
                                if (f.label.starts_with(prefix + " target offset"))
                                    document_->set(f, target[component] -
                                                          player.position[component] -
                                                          (component == 1 ? 133 : 0));
                                else if (f.label.starts_with(prefix + " rotation"))
                                    document_->set(f, component == 0   ? -view.pitch * 57.2957795
                                                      : component == 1 ? view.yaw * 57.2957795
                                                                       : 0);
                                else if (f.label.starts_with("Position"))
                                    document_->set(f, eye[component]);
                                else if (f.label.starts_with("Fixed target"))
                                    document_->set(f, target[component]);
                            }
                        }
                    document_->commit();
                    synchronize();
                } catch (const std::exception &e) {
                    document_->cancel();
                    synchronize();
                    message_ = e.what();
                }
            }
            ImGui::TextWrapped("Capture uses the current player position as the follow-camera "
                               "anchor. Shared zone defaults remain read-only.");
            ImGui::TreePop();
        }
    }
    ImGui::Separator();
    if (group_.empty())
        ImGui::TextUnformatted("No records in this category.");
    for (unsigned section = 0; section < (category_ == 0 ? 3u : 1u); ++section) {
        if (category_ == 0 &&
            !studio::TutorialWidgets::CollapsingHeader(
                "camera_editor",
                std::array<const char *, 3>{"Camera pose", "Activation and transitions",
                                            "Automatic adjustments"}[section],
                section == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0))
            continue;
        for (auto &field : document_->fields())
            if (field.group == group_) {
                bool support = field.label.starts_with("Support") ||
                               field.label.starts_with("Pull back") ||
                               field.label.starts_with("Return"),
                     activation = field.label == "Transition frames" || field.label == "Priority" ||
                                  field.label == "Game variable" || field.label == "Required value";
                if (category_ == 0 && (support ? 2u : activation ? 1u : 0u) != section)
                    continue;
                ImGui::PushID(int(field.part));
                ImGui::PushID(int(field.offset));
                double value = document_->value(field);
                bool edited = false;
                if (field.type == CameraField::Boolean) {
                    bool flag = value != 0;
                    edited = studio::TutorialWidgets::Checkbox("camera_editor", field.label.c_str(),
                                                               &flag);
                    value = flag;
                } else {
                    ImGui::TextWrapped("%s", field.label.c_str());
                    ImGui::SetNextItemWidth(-1);
                    edited = ImGui::InputDouble("##value", &value,
                                                field.type == CameraField::Float ? .1 : 1, 10,
                                                field.type == CameraField::Float ? "%.4f" : "%.0f");
                }
                try {
                    if (edited) {
                        document_->set(field, value);
                        synchronize();
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() ||
                        (edited && field.type == CameraField::Boolean))
                        document_->commit();
                } catch (const std::exception &e) {
                    message_ = e.what();
                }
                ImGui::PopID();
                ImGui::PopID();
            }
    }
    ImGui::EndDisabled();
    ImGui::End();
}
bool CameraEditor::draw_workspace(bool loading) {
    bool maps = false;
    ImGui::Begin("Camera regions");
    if (studio::TutorialWidgets::Button("camera_editor", "Choose map in Maps"))
        maps = true;
    studio::TutorialWidgets::Checkbox("camera_editor", "Overhead editing view", &overview_);
    if (studio::TutorialWidgets::Button("camera_editor", "Frame map"))
        view_.fit(scene_ ? scene_->low : view_.bounds_low,
                  scene_ ? scene_->high : view_.bounds_high);
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("camera_editor", "Frame player"))
        view_.focus_start(renderer_.player.position);
    ImGui::TextWrapped("Player: WASD, Ctrl to run. Free view: middle-drag to orbit, right-drag to "
                       "pan, wheel to zoom.");
    if (scene_) {
        std::set<std::string> groups;
        if (document_)
            for (auto &field : document_->fields())
                groups.insert(field.group);
        for (auto &group : groups)
            if (ImGui::Selectable(group.c_str(), group_ == group)) {
                group_ = group;
                follow_ = false;
                category_ = group.starts_with("Camera ")        ? 0
                            : group.starts_with("Circle ")      ? 1
                            : group.starts_with("Trigger ")     ? 2
                            : group.starts_with("Replacement ") ? 4
                                                                : 3;
            }
    }
    ImGui::End();
    draw(loading, view_);
    ImGui::Begin("Camera viewport");
    if (!scene_) {
        ImGui::TextUnformatted("Load a map to preview cameras.");
        ImGui::End();
        return maps;
    }
    auto &io = ImGui::GetIO();
    auto &player = renderer_.player;
    bool input = hovered_ && !(io.KeyCtrl && io.KeyShift) && !loading && !kind_ &&
                 !export_.valid() && !io.WantTextInput &&
                 (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS);
    if (player.active) {
        if (input && ImGui::IsKeyPressed(ImGuiKey_Escape))
            player.active = false;
        float side = input
                         ? float(ImGui::IsKeyDown(ImGuiKey_D)) - float(ImGui::IsKeyDown(ImGuiKey_A))
                         : 0,
              forward =
                  input ? float(ImGui::IsKeyDown(ImGuiKey_W)) - float(ImGui::IsKeyDown(ImGuiKey_S))
                        : 0;
        player.step(scene_->spatial, side, forward, input && io.KeyCtrl, input && io.KeyShift,
                    io.DeltaTime);
    }
    auto size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 1.f);
    size.y = std::max(size.y, 1.f);
    unsigned width = unsigned(std::clamp(size.x * io.DisplayFramebufferScale.x, 1.f, 4096.f)),
             height = unsigned(std::clamp(size.y * io.DisplayFramebufferScale.y, 1.f, 4096.f));
    float view[16], projection[16];
    float fov = 45;
    auto camera = view_;
    if (overview_)
        camera.pitch = 1.5f;
    if (player.active && !overview_) {
        auto &pose = player.camera;
        camera.target = pose.target;
        SpatialPoint delta{};
        float length = 0;
        for (unsigned i = 0; i < 3; ++i) {
            delta[i] = pose.eye[i] - pose.target[i];
            length += delta[i] * delta[i];
        }
        camera.distance = std::sqrt(length);
        camera.yaw = std::atan2(delta[0], delta[2]);
        camera.pitch =
            std::asin(std::clamp(delta[1] / std::max(camera.distance, .001f), -1.f, 1.f));
        view_ = camera;
        bx::mtxLookAt(view, {pose.eye[0], pose.eye[1], pose.eye[2]},
                      {pose.target[0], pose.target[1], pose.target[2]},
                      {pose.up[0], pose.up[1], pose.up[2]}, bx::Handedness::Right);
        fov = pose.fov;
    } else {
        auto eye = camera.eye();
        bx::mtxLookAt(view, {eye[0], eye[1], eye[2]},
                      {camera.target[0], camera.target[1], camera.target[2]}, {0, 1, 0},
                      bx::Handedness::Right);
    }
    bx::mtxProj(projection, fov, float(width) / height, camera.near_clip(), camera.far_clip(),
                bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
    auto enabled = renderer_.spatial.enabled;
    auto selected = renderer_.spatial.selected;
    auto only = renderer_.spatial.selected_only;
    renderer_.spatial.enabled.fill(false);
    renderer_.spatial.enabled[unsigned(SpatialKind::Camera)] = overview_ || !player.active;
    renderer_.spatial.enabled[unsigned(SpatialKind::ScrollStop)] = overview_ || !player.active;
    renderer_.spatial.selected = player.camera_region;
    renderer_.spatial.selected_only = false;
    auto texture = renderer_.render(width, height, view, projection, true, false, 0, false);
    renderer_.spatial.enabled = enabled;
    renderer_.spatial.selected = selected;
    renderer_.spatial.selected_only = only;
    bool flip = bgfx::getCaps()->originBottomLeft;
    ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false)), size, {0, flip ? 1.f : 0.f},
                 {1, flip ? 0.f : 1.f});
    hovered_ = ImGui::IsItemHovered();
    if (hovered_ && (!player.active || overview_)) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            view_.rotate(io.MouseDelta.x, io.MouseDelta.y, false);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            view_.pan(io.MouseDelta.x, io.MouseDelta.y);
        view_.wheel(io.MouseWheel, false);
    }
    ImGui::End();
    ImGui::Begin("Camera status");
    ImGui::TextWrapped("%s", message_.c_str());
    ImGui::TextWrapped(
        "Preview supports follow, blended follow, fixed cameras, conditions, replacement rules and "
        "distance support. Path playback and direction-support behavior remain unverified. Save "
        "patches to retain edits; export writes a separate field GARC.");
    ImGui::End();
    return maps;
}

void CameraEditor::bind_project() {
    if (!document_)
        return;
    project_.bind(
        "cameras", "cameras/" + std::to_string(project_area_),
        "Cameras " + std::to_string(project_area_), std::to_string(project_area_),
        [this] {
            return document_ && document_->dirty();
        },
        [this] {
            require(!kind_ && !export_.valid(), "Finish the camera operation before saving");
            document_->commit();
            return project_text(document_->serialize());
        },
        [this] {
            document_->mark_saved();
        });
    project_.ready([this] {
        if (document_) {
            require(!kind_ && !export_.valid(), "Finish the camera operation before saving");
        }
    });
    if (auto file = project_.document(); !file.empty()) {
        open_patch(file);
        project_.restored();
    }
}
}
