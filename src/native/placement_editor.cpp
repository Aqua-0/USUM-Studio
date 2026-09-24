#include "native/tutorial_widgets.h"
#include "native/placement_editor.h"
#include "native/undo_shortcuts.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <cmath>
#include <algorithm>
namespace studio {
namespace {
void save_patch_file(const std::filesystem::path &path, const std::string &text) {
    auto temporary = path;
    temporary += ".saving";
    auto backup = path;
    backup += ".previous";
    require(!std::filesystem::exists(temporary) && !std::filesystem::exists(backup),
            "A previous patch save is unfinished; recover the .saving/.previous files first");
    write_new_file(temporary,
                   View(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
    bool existed = std::filesystem::exists(path);
    try {
        if (existed)
            std::filesystem::rename(path, backup);
        std::filesystem::rename(temporary, path);
    } catch (...) {
        if (existed && std::filesystem::exists(backup) && !std::filesystem::exists(path))
            std::filesystem::rename(backup, path);
        std::filesystem::remove(temporary);
        throw;
    }
    if (existed)
        std::filesystem::remove(backup);
}
float distance(ImVec2 a, ImVec2 b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
float line_distance(ImVec2 p, ImVec2 a, ImVec2 b) {
    float x = b.x - a.x, y = b.y - a.y, n = x * x + y * y;
    if (n < .01f)
        return distance(p, a);
    float t = std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / n, 0.f, 1.f);
    return distance(p, {a.x + t * x, a.y + t * y});
}
}
PlacementEditor::~PlacementEditor() {
    if (export_job_.valid())
        export_job_.wait();
}
void PlacementEditor::set_scene(std::shared_ptr<Environment> scene, unsigned area,
                                const std::filesystem::path &dump) {
    working_ = nullptr;
    scene_ = std::move(scene);
    dump_ = dump;
    patch_path_.clear();
    selected_ = -1;
    drag_axis_ = -1;
    message_.clear();
    document_ = std::make_unique<PlacementDocument>(area, scene_->placement_source);
    require(document_->entries().size() == scene_->placement_transforms.size(),
            "Placement scene bindings disagree with source records");
    original_spatial_ = scene_->spatial;
    revision_ = document_->revision();
    synchronized_transforms_.assign(document_->entries().size(), pose_identity());
    bind_project();
}
void PlacementEditor::set_working_scene(std::shared_ptr<Environment> scene,
                                        OverworldDocument *document) {
    if (!document)
        return;
    project_.unbind();
    project_.ready([this] {
        require(!dragging(), "Finish moving the placement before saving");
    });
    working_ = document;
    scene_ = std::move(scene);
    document_ = std::make_unique<PlacementDocument>(document_->area(), scene_->placement_source);
    original_spatial_ = scene_->spatial;
    revision_ = document_->revision();
    selected_ = -1;
    drag_axis_ = -1;
    committed_.clear();
    base_turns_.clear();
    synchronized_transforms_.assign(document_->entries().size(), pose_identity());
    const auto entries = working_->working_entries();
    for (unsigned i = 0; i < document_->entries().size(); ++i) {
        committed_.push_back(document_->state(i));
        const auto &source = document_->entries()[i];
        auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
            return entry.entry.kind == (source.trainer     ? OverworldKind::Trainer
                                        : source.character ? OverworldKind::Character
                                                           : OverworldKind::StaticObject) &&
                   entry.entry.zone == source.zone && entry.entry.event == source.source.event;
        });
        base_turns_.push_back(
            found == entries.end()
                ? 0
                : working_->working_draft(found->id, OverworldOperation::Action::Update).turn);
    }
}
void PlacementEditor::refresh_working_transforms() {
    if (!working_)
        return;
    const auto entries = working_->working_entries();
    for (unsigned i = 0; i < document_->entries().size(); ++i) {
        const auto &source = document_->entries()[i];
        auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
            return entry.entry.kind == (source.trainer     ? OverworldKind::Trainer
                                        : source.character ? OverworldKind::Character
                                                           : OverworldKind::StaticObject) &&
                   entry.entry.zone == source.zone && entry.entry.event == source.source.event;
        });
        if (found == entries.end())
            continue;
        const auto edit = working_->working_draft(found->id, OverworldOperation::Action::Update);
        PlacementState state{edit.position, edit.turn - base_turns_[i]};
        if (state != document_->state(i))
            document_->preview(i, state);
        committed_[i] = state;
    }
    document_->commit();
    synchronize();
}
void PlacementEditor::commit() {
    if (working_) {
        auto entries = working_->working_entries();
        for (unsigned i = 0; i < committed_.size(); ++i) {
            const auto &state = document_->state(i);
            if (state == committed_[i])
                continue;
            const auto &source = document_->entries()[i];
            auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
                return entry.entry.kind == (source.trainer     ? OverworldKind::Trainer
                                            : source.character ? OverworldKind::Character
                                                               : OverworldKind::StaticObject) &&
                       entry.entry.zone == source.zone && entry.entry.event == source.source.event;
            });
            require(found != entries.end(), "Placement no longer exists in the working map");
            auto edit = working_->working_draft(found->id, OverworldOperation::Action::Update);
            edit.position = state.position;
            edit.turn += state.turn - committed_[i].turn;
            working_->apply_working(found->id, edit);
            committed_[i] = state;
        }
    }
    document_->commit();
}
void PlacementEditor::synchronize() {
    if (!document_ || revision_ == document_->revision())
        return;
    revision_ = document_->revision();
    std::vector<bool> changed(scene_->placement_transforms.size());
    for (unsigned i = 0; i < scene_->placement_transforms.size(); ++i) {
        auto transform = document_->transform(i);
        if (transform == synchronized_transforms_.at(i))
            continue;
        changed[i] = true;
        synchronized_transforms_[i] = transform;
        scene_->placement_transforms[i] = transform;
    }
    for (unsigned i = 0; i < scene_->spatial.regions.size(); ++i) {
        auto &r = scene_->spatial.regions[i];
        if (r.placement < 0 || !changed.at(r.placement))
            continue;
        r.vertices = original_spatial_.regions[i].vertices;
        if (r.overworld) {
            auto reference =
                std::make_shared<OverworldReference>(*original_spatial_.regions[i].overworld);
            reference->position = document_->state(r.placement).position;
            const auto &state = document_->state(r.placement);
            const auto &q = original_spatial_.regions[i].overworld->rotation;
            float sine = std::sin(state.turn * .00872664626f),
                  cosine = std::cos(state.turn * .00872664626f);
            reference->rotation = {cosine * q[0] + sine * q[2], cosine * q[1] + sine * q[3],
                                   cosine * q[2] - sine * q[0], cosine * q[3] - sine * q[1]};
            r.overworld = std::move(reference);
        }
        auto &m = scene_->placement_transforms.at(r.placement);
        for (auto &v : r.vertices) {
            auto p = v.position;
            for (unsigned k = 0; k < 3; ++k)
                v.position[k] =
                    m[k * 4] * p[0] + m[k * 4 + 1] * p[1] + m[k * 4 + 2] * p[2] + m[k * 4 + 3];
        }
    }
    renderer_.spatial.invalidate_geometry();
    renderer_.invalidate_selection_readback();
}
void PlacementEditor::request_leave(std::function<void()> action) {
    if (project_store()) {
        try {
            save_editor_project();
            action();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
        return;
    }
    if (busy()) {
        message_ = "Finish the current file operation before changing maps or closing.";
        return;
    }
    if (document_)
        commit();
    if (!document_ || !document_->dirty())
        action();
    else {
        leave_action_ = std::move(action);
        leave_modal_ = true;
    }
}
void PlacementEditor::open_dialog(int kind) {
    if (kind == 1 && project_store())
        save_editor_project();
    if (busy())
        return;
    dialog_kind_ = kind;
    std::string initial = patch_path_.empty() ? "placements.usum-map" : patch_path_.string();
    if (kind == 3)
        choose_folder(window_, dialog_,
                      patch_path_.empty() ? nullptr : patch_path_.parent_path().string().c_str());
    else
        choose_patch(window_, dialog_, initial.c_str(), kind == 1);
}
void PlacementEditor::save() {
    if (save_editor_project()) {
        message_ = "Project saved. Stage Project builds the overlay.";
        return;
    }
    if (!document_)
        return;
    commit();
    if (patch_path_.empty()) {
        open_dialog(1);
        return;
    }
    save_patch_file(patch_path_, document_->serialize());
    document_->mark_saved();
    message_ = "Patch saved: " + patch_path_.string();
    if (save_then_leave_) {
        save_then_leave_ = false;
        auto action = std::move(leave_action_);
        if (action)
            action();
    }
}
void PlacementEditor::open_patch(const std::filesystem::path &path) {
    require(bool(document_), "Load a map before opening a patch");
    require(std::filesystem::file_size(path) < 4 * 1024 * 1024, "Patch file is too large");
    auto bytes = read_file(path);
    document_->restore(text(bytes));
    project_.imported();
    patch_path_ = path;
    message_ = "Patch opened: " + path.string();
    synchronize();
}
void PlacementEditor::poll() {
    if (export_job_.valid() &&
        export_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            message_ = export_job_.get();
        } catch (const std::exception &e) {
            message_ = std::string("Export failed: ") + e.what();
        }
    }
    std::string path, error;
    int kind = 0;
    {
        std::lock_guard lock(dialog_->mutex);
        if (dialog_->ready) {
            dialog_->ready = false;
            path = dialog_->path;
            error = dialog_->error;
            kind = dialog_kind_;
            dialog_kind_ = 0;
        }
    }
    if (!kind)
        return;
    if (!error.empty()) {
        message_ = error;
        save_then_leave_ = false;
        return;
    }
    if (path.empty()) {
        save_then_leave_ = false;
        leave_action_ = {};
        return;
    }
    try {
        if (kind == 1) {
            auto target = std::filesystem::u8path(path);
            if (target.extension() != ".usum-map")
                target += ".usum-map";
            auto previous = patch_path_;
            patch_path_ = target;
            try {
                save();
            } catch (...) {
                patch_path_ = previous;
                throw;
            }
        } else if (kind == 2) {
            open_patch(std::filesystem::u8path(path));
        } else {
            commit();
            auto snapshot = *document_;
            snapshot.compile();
            auto dump = dump_;
            auto archives = scene_->archive_sources;
            auto folder = std::filesystem::u8path(path);
            message_ = "Exporting and verifying the game archive...";
            export_job_ = std::async(std::launch::async, [dump, folder, snapshot, archives]() {
                export_placements(dump, folder, snapshot, archives);
                return "Export verified: " + folder.string() +
                       ". Install its romfs folder as a game override; remove it to revert.";
            });
        }
    } catch (const std::exception &e) {
        message_ = e.what();
        save_then_leave_ = false;
    }
}
void PlacementEditor::draw(const MaterialSelection &selection, const SpatialPoint *cursor,
                           bool show_controls) {
    poll();
    if (show_controls && scene_ && selection.focus && selection.draw >= 0 &&
        scene_->draws.at(selection.draw).placement >= 0)
        ImGui::SetNextWindowFocus();
    ImGui::Begin("Map inspector");
    if (!document_) {
        if (show_controls)
            ImGui::TextDisabled("Load a map to edit placements.");
        ImGui::End();
        return;
    }
    auto &io = ImGui::GetIO();
    int selected = selection.draw >= 0 ? scene_->draws.at(selection.draw).placement : -1;
    if (selected != selected_ && !dragging()) {
        commit();
        selected_ = selected;
    }
    if (show_controls) {
        if (working_)
            ImGui::TextDisabled(working_->dirty()     ? "Unsaved changes"
                                : working_->changed() ? "Saved — needs staging"
                                                      : "Staged");
        else
            ImGui::Text("%zu changed placement%s%s", document_->changed_count(),
                        document_->changed_count() == 1 ? "" : "s",
                        document_->dirty() ? "  * Unsaved" : "");

        bool locked = busy() || renderer_.player.active;
        ImGui::BeginDisabled(locked);
        ImGui::BeginDisabled(working_ ? !working_->can_undo() : !document_->can_undo());
        if (studio::TutorialWidgets::Button("placement_editor", "Undo"))
            working_ ? working_->undo() : document_->undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(working_ ? !working_->can_redo() : !document_->can_redo());
        if (studio::TutorialWidgets::Button("placement_editor", "Redo"))
            working_ ? working_->redo() : document_->redo();
        ImGui::EndDisabled();
        ImGui::Separator();
        if (selected_ >= 0) {
            auto &e = document_->entries().at(selected_);
            ImGui::SeparatorText(e.trainer     ? "Trainer placement"
                                 : e.character ? "NPC placement"
                                               : "Placement");
            ImGui::BeginDisabled(!e.restriction.empty() || dragging());
            auto state = document_->state(selected_);
            try {
                if (cursor &&
                    studio::TutorialWidgets::Button("placement_editor", "Move to 3D cursor")) {
                    state.position = *cursor;
                    document_->preview(selected_, state);
                    commit();
                }
                ImGui::TextUnformatted("Position (X / Y / Z)");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat3("##placement-position", state.position.data(), 1.f, -9999999,
                                      9999999, "%.2f", ImGuiSliderFlags_AlwaysClamp))
                    document_->preview(selected_, state);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    commit();
                float turn = state.turn + (working_ ? base_turns_[selected_] : 0);
                if (ImGui::DragFloat("Turn Y", &turn, .5f, -360000, 360000, "%.1f deg",
                                     ImGuiSliderFlags_AlwaysClamp)) {
                    state.turn = turn - (working_ ? base_turns_[selected_] : 0);
                    document_->preview(selected_, state);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    commit();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Turn around the vertical axis from the original orientation; "
                        "existing tilt is preserved.");
                if (studio::TutorialWidgets::Button("placement_editor", "Reset placement")) {
                    if (working_) {
                        const auto entries = working_->working_entries();
                        auto found =
                            std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
                                return entry.entry.kind == (e.trainer ? OverworldKind::Trainer
                                                            : e.character
                                                                ? OverworldKind::Character
                                                                : OverworldKind::StaticObject) &&
                                       entry.entry.zone == e.zone &&
                                       entry.entry.event == e.source.event;
                            });
                        require(found != entries.end(), "Placement no longer exists");
                        auto edit =
                            working_->working_draft(found->id, OverworldOperation::Action::Update);
                        state.position = working_->entries()[edit.entry].position;
                        state.turn = -base_turns_[selected_];
                        document_->preview(selected_, state);
                    } else
                        document_->reset(selected_);
                    commit();
                }
            } catch (const std::exception &error) {
                document_->cancel();
                message_ = error.what();
            }
            studio::TutorialWidgets::RadioButton("placement_editor", "Move (G)", &mode_, 0);
            ImGui::SameLine();
            studio::TutorialWidgets::RadioButton("placement_editor", "Rotate (R)", &mode_, 1);
            studio::TutorialWidgets::Checkbox("placement_editor", "Snap", &snap_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Ctrl while dragging also snaps");
            if (snap_) {
                ImGui::SetNextItemWidth(100);
                ImGui::DragFloat(mode_ ? "Degrees" : "Units", mode_ ? &angle_step_ : &step_, 1, 1,
                                 1000, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            }
            ImGui::EndDisabled();
            if (!e.restriction.empty())
                ImGui::TextWrapped("%s", e.restriction.c_str());
            if (ImGui::TreeNode("Placement details")) {
                ImGui::Text("Zone group %zu | Resource %u | Event %u", e.zone,
                            e.character ? e.character_model : e.source.model, e.source.event);
                ImGui::TextWrapped(
                    (e.source.collision || !e.shapes.empty())
                        ? "Authored collision shapes move with this placement."
                        : "No authored collision tail. Terrain collision remains where it "
                          "is; resident collision preview is not available.");
                ImGui::TreePop();
            }
        }
        ImGui::Separator();
        try {
            if (studio::TutorialWidgets::Button("placement_editor",
                                                project_store() ? "Save Project" : "Save patch") ||
                (!locked && !io.WantTextInput && io.KeyCtrl && io.KeyShift && !io.KeyAlt &&
                 (!project_store() && ImGui::IsKeyPressed(ImGuiKey_S, false))))
                save();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Save patch (Ctrl+Shift+S)");
            ImGui::SameLine();
            if (!project_store() &&
                studio::TutorialWidgets::Button("placement_editor", "Save as..."))
                open_dialog(1);
            if (!working_ && ImGui::TreeNode("Patch import / export")) {
                if (studio::TutorialWidgets::Button("placement_editor", "Open patch..."))
                    request_leave([this] {
                        open_dialog(2);
                    });
                ImGui::BeginDisabled(document_->changed_count() == 0);
                if (studio::TutorialWidgets::Button("placement_editor",
                                                    "Export placement override...", {-1, 0})) {
                    document_->compile();
                    open_dialog(3);
                }
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
        } catch (const std::exception &e) {
            message_ = e.what();
        }
        ImGui::EndDisabled();
        if (renderer_.player.active)
            ImGui::TextWrapped("Return to the free camera to edit objects.");
        if (!project_store())
            ImGui::TextWrapped(
                "Save keeps an editable patch. Export creates a separate game override.");
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
    }
    synchronize();
    if (leave_modal_) {
        ImGui::OpenPopup("Unsaved placement changes");
        leave_modal_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved placement changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save your placement changes before continuing?");
        if (studio::TutorialWidgets::Button("placement_editor",
                                            project_store() ? "Save Project" : "Save patch")) {
            save_then_leave_ = true;
            ImGui::CloseCurrentPopup();
            try {
                save();
            } catch (const std::exception &e) {
                message_ = e.what();
                save_then_leave_ = false;
            }
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("placement_editor", "Discard")) {
            auto action = std::move(leave_action_);
            ImGui::CloseCurrentPopup();
            if (action)
                action();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("placement_editor", "Cancel")) {
            leave_action_ = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}
bool PlacementEditor::gizmo(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
                            bool hovered) {
    if (!document_ || selected_ < 0 || busy() || renderer_.player.active ||
        !document_->entries()[selected_].restriction.empty())
        return false;
    auto &io = ImGui::GetIO();
    if (hovered && working_ && !dragging() && !io.WantTextInput && io.KeyCtrl &&
        !UndoShortcuts::consumed) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false) || ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            if (io.KeyShift || ImGui::IsKeyPressed(ImGuiKey_Y, false))
                working_->redo();
            else
                working_->undo();
            UndoShortcuts::consumed = true;
            return true;
        }
    }
    auto state = document_->state(selected_);
    auto project = [&](std::array<float, 3> p, ImVec2 &out) {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] = view[i] * p[0] + view[4 + i] * p[1] + view[8 + i] * p[2] + view[12 + i];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[j * 4 + i] * a[j];
        if (b[3] <= .001f)
            return false;
        out = {origin.x + (b[0] / b[3] * .5f + .5f) * size.x,
               origin.y + (.5f - b[1] / b[3] * .5f) * size.y};
        return true;
    };
    if (mouse_blocked_) {
        mouse_blocked_ = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        return true;
    }
    ImVec2 center{};
    if (!project(state.position, center)) {
        if (dragging()) {
            document_->cancel();
            drag_axis_ = -1;
            mouse_blocked_ = true;
            synchronize();
            return true;
        }
        return false;
    }
    float depth = -(view[2] * state.position[0] + view[6] * state.position[1] +
                    view[10] * state.position[2] + view[14]);
    float length = std::clamp(depth * .12f, 15.f, 2000.f);
    if (dragging())
        length = handle_length_;
    int hot = -1;
    float nearest = 9;
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    ImVec2 endpoints[3];
    const ImU32 colors[]{IM_COL32(255, 95, 85, 255), IM_COL32(110, 240, 130, 255),
                         IM_COL32(100, 160, 255, 255)};
    if (mode_ == 0) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            auto p = state.position;
            p[axis] += length;
            if (!project(p, endpoints[axis]))
                continue;
            float d = line_distance(io.MousePos, center, endpoints[axis]);
            if (d < nearest && distance(center, endpoints[axis]) > 12) {
                nearest = d;
                hot = int(axis);
            }
            auto color = drag_axis_ == int(axis) ? IM_COL32(255, 225, 110, 255) : colors[axis];
            draw->AddLine(center, endpoints[axis], color, 3);
            draw->AddCircleFilled(endpoints[axis], 5, color);
            draw->AddText({endpoints[axis].x + 7, endpoints[axis].y - 7}, colors[axis],
                          axis == 0   ? "X"
                          : axis == 1 ? "Y"
                                      : "Z");
        }
    } else {
        for (unsigned k = 0; k < 64; ++k) {
            float a = k * 6.283185307f / 64, b = (k + 1) * 6.283185307f / 64;
            auto p = state.position, q = p;
            p[0] += std::cos(a) * length;
            p[2] += std::sin(a) * length;
            q[0] += std::cos(b) * length;
            q[2] += std::sin(b) * length;
            ImVec2 x, y;
            if (project(p, x) && project(q, y)) {
                float d = line_distance(io.MousePos, x, y);
                if (d < nearest) {
                    nearest = d;
                    hot = 3;
                }
                draw->AddLine(x, y, drag_axis_ == 3 ? IM_COL32(255, 225, 110, 255) : colors[1], 2);
            }
        }
    }
    draw->AddCircleFilled(center, 4, IM_COL32(255, 255, 255, 255));
    draw->PopClipRect();
    if (hovered && !dragging() && !io.WantTextInput && !io.KeyCtrl &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (ImGui::IsKeyPressed(ImGuiKey_G))
            mode_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            mode_ = 1;
    }
    auto ring_angle = [&](const PlacementState &value) {
        ImVec2 c, x, z;
        auto p = value.position, q = p;
        p[0] += length;
        q[2] += length;
        if (!project(value.position, c) || !project(p, x) || !project(q, z))
            return 0.f;
        float ax = x.x - c.x, ay = x.y - c.y, bx = z.x - c.x, by = z.y - c.y,
              dx = io.MousePos.x - c.x, dy = io.MousePos.y - c.y, det = ax * by - ay * bx;
        if (std::abs(det) < .01f)
            return 0.f;
        return std::atan2((ax * dy - ay * dx) / det, (dx * by - dy * bx) / det);
    };
    if (!dragging() && hovered && hot >= 0 && !io.KeyCtrl &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        commit();
        drag_axis_ = hot;
        drag_start_ = state;
        mouse_start_ = io.MousePos;
        handle_length_ = length;
        if (hot < 3)
            axis_screen_ = {endpoints[hot].x - center.x, endpoints[hot].y - center.y};
        else
            angle_start_ = ring_angle(state);
    }
    bool captured = dragging() || (hovered && hot >= 0 && !io.KeyCtrl);
    if (dragging()) {
        try {
            auto next = drag_start_;
            if (drag_axis_ < 3) {
                float squared = axis_screen_.x * axis_screen_.x + axis_screen_.y * axis_screen_.y;
                float delta = ((io.MousePos.x - mouse_start_.x) * axis_screen_.x +
                               (io.MousePos.y - mouse_start_.y) * axis_screen_.y) *
                              handle_length_ / std::max(squared, .01f);
                if (snap_ || io.KeyCtrl)
                    delta = std::round(delta / step_) * step_;
                next.position[drag_axis_] += delta;
            } else {
                float delta = std::remainder(ring_angle(drag_start_) - angle_start_, 6.283185307f) *
                              57.29577951f;
                if (snap_ || io.KeyCtrl)
                    delta = std::round(delta / angle_step_) * angle_step_;
                next.turn -= delta;
            }
            document_->preview(selected_, next);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || io.AppFocusLost) {
                document_->cancel();
                drag_axis_ = -1;
                mouse_blocked_ = true;
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                commit();
                drag_axis_ = -1;
            }
        } catch (const std::exception &e) {
            document_->cancel();
            drag_axis_ = -1;
            message_ = e.what();
        }
        synchronize();
    }
    return captured;
}
void PlacementEditor::bind_project() {
    if (!document_)
        return;
    project_.bind(
        "placements", "placements/" + std::to_string(document_->area()),
        "Field placements " + std::to_string(document_->area()), std::to_string(document_->area()),
        [this] {
            return document_ && document_->dirty();
        },
        [this] {
            require(!busy() && !dragging(), "Finish the placement operation before saving");
            commit();
            return project_text(document_->serialize());
        },
        [this] {
            document_->mark_saved();
        });
    project_.ready([this] {
        if (document_) {
            require(!busy() && !dragging(), "Finish the placement operation before saving");
        }
    });
    if (auto file = project_.document(); !file.empty()) {
        open_patch(file);
        project_.restored();
    }
}
}
