#include "native/overworld_editor.h"
#include "native/undo_shortcuts.h"
#include "native/tutorial_widgets.h"
#include <algorithm>
#include <cmath>

namespace studio {
bool OverworldEditor::shape_active() const {
    if (!open_ || !editing_ || loading_ || !document_ || !project_store() ||
        renderer_.player.active || draft_.action == OverworldOperation::Action::Remove ||
        shape_group_ < 0 || shape_group_ >= int(shape_groups_.size()) || shape_index_ < 0)
        return false;
    const auto g = unsigned(shape_group_);
    const auto &shapes = draft_.shapes.contains(g) ? draft_.shapes.at(g) : shape_groups_[g].shapes;
    return shape_index_ < int(shapes.size()) && shapes[shape_index_].type < 3;
}
void OverworldEditor::remember_shapes(ShapeEdits before) {
    if (before == draft_.shapes)
        return;
    shape_undo_.push_back(std::move(before));
    shape_redo_.clear();
}
void OverworldEditor::cancel_shape_drag() {
    if (drag_axis_ < 0)
        return;
    if (editing_ && draft_.entry == drag_entry_)
        draft_.shapes = drag_shapes_;
    drag_axis_ = -1;
    gizmo_blocked_ = true;
}
void OverworldEditor::undo_shape(bool redo) {
    cancel_shape_drag();
    auto &from = redo ? shape_redo_ : shape_undo_;
    auto &to = redo ? shape_undo_ : shape_redo_;
    if (from.empty())
        return;
    to.push_back(draft_.shapes);
    draft_.shapes = std::move(from.back());
    from.pop_back();
}
bool OverworldEditor::gizmo(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
                            bool hovered) {
    if (patrol_active())
        return patrol_gizmo(view, projection, origin, size, hovered);
    if (patrol_axis_ >= 0) {
        patrol_draft_ = patrol_drag_;
        patrol_axis_ = -1;
    }
    auto &io = ImGui::GetIO();
    if (!shape_active())
        cancel_shape_drag();
    if (gizmo_blocked_) {
        gizmo_blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    if (!shape_active())
        return false;
    if (drag_axis_ >= 0 && (io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        cancel_shape_drag();
        return true;
    }
    if (hovered && drag_axis_ < 0 && io.KeyCtrl && !io.WantTextInput && !UndoShortcuts::consumed) {
        bool undo = ImGui::IsKeyPressed(ImGuiKey_Z, false),
             redo = ImGui::IsKeyPressed(ImGuiKey_Y, false);
        if (undo || redo) {
            undo_shape(redo || io.KeyShift);
            UndoShortcuts::consumed = true;
            return true;
        }
    }
    if (hovered && drag_axis_ < 0 && !io.WantTextInput && !io.KeyCtrl &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (ImGui::IsKeyPressed(ImGuiKey_G))
            shape_mode_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_S))
            shape_mode_ = 1;
    }
    unsigned g = unsigned(shape_group_);
    auto shapes = draft_.shapes.contains(g) ? draft_.shapes.at(g) : shape_groups_[g].shapes;
    auto volume = shapes[shape_index_];
    auto project = [&](SpatialPoint p, ImVec2 &out) {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] = view[i] * p[0] + view[4 + i] * p[1] + view[8 + i] * p[2] + view[12 + i];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[j * 4 + i] * a[j];
        if (!std::isfinite(b[3]) || b[3] <= .001f)
            return false;
        float x = b[0] / b[3], y = b[1] / b[3];
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 100 || std::abs(y) > 100)
            return false;
        out = {origin.x + (x * .5f + .5f) * size.x, origin.y + (.5f - y * .5f) * size.y};
        return true;
    };
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    try {
        auto geometry = preview_placement_volumes({volume}, draft_.position);
        for (const auto &r : geometry.regions)
            for (size_t i = 0; i + 1 < r.lines.size(); i += 2) {
                const auto &p = r.vertices.at(r.lines[i]).position;
                const auto &q = r.vertices.at(r.lines[i + 1]).position;
                if (volume.type == 0) {
                    float x = draft_.position[0] + volume.values[0],
                          z = draft_.position[2] + volume.values[2];
                    auto center = [&](SpatialPoint v) {
                        return std::abs(v[0] - x) < .01f && std::abs(v[2] - z) < .01f;
                    };
                    if (center(p) || center(q))
                        continue;
                    if (std::abs(p[1] - q[1]) > .01f && std::abs(p[0] - x) > .01f &&
                        std::abs(p[2] - z) > .01f)
                        continue;
                }
                ImVec2 a, b;
                if (project(p, a) && project(q, b))
                    draw->AddLine(a, b, IM_COL32(100, 230, 255, 220), 1.5f);
            }
    } catch (const std::exception &) {
        draw->PopClipRect();
        cancel_shape_drag();
        return false;
    }
    unsigned component = volume.type == 2 && shape_endpoint_ ? 3u : 0u;
    SpatialPoint point = draft_.position;
    for (unsigned k = 0; k < 3; ++k)
        point[k] += volume.values[component + k];
    ImVec2 center;
    if (!project(point, center)) {
        draw->PopClipRect();
        cancel_shape_drag();
        return false;
    }
    draw->AddText({center.x + 8, center.y + 8}, IM_COL32(150, 240, 255, 255), "Draft shape");
    float depth = -(view[2] * point[0] + view[6] * point[1] + view[10] * point[2] + view[14]);
    float length = std::clamp(depth * .12f, 15.f, 2000.f);
    auto rotate = [&](SpatialPoint p) {
        if (volume.type != 1 || !shape_mode_)
            return p;
        auto &v = volume.values;
        float norm = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5] + v[6] * v[6]);
        float x = v[3] / norm, y = v[4] / norm, z = v[5] / norm, w = v[6] / norm;
        SpatialPoint t{2 * (y * p[2] - z * p[1]), 2 * (z * p[0] - x * p[2]),
                       2 * (x * p[1] - y * p[0])};
        return SpatialPoint{p[0] + w * t[0] + y * t[2] - z * t[1],
                            p[1] + w * t[1] + z * t[0] - x * t[2],
                            p[2] + w * t[2] + x * t[1] - y * t[0]};
    };
    ImVec2 starts[3]{}, ends[3]{}, directions[3]{};
    bool visible[3]{};
    int hot = -1;
    float nearest = 9;
    const ImU32 colors[]{IM_COL32(245, 85, 80, 255), IM_COL32(90, 230, 115, 255),
                         IM_COL32(80, 160, 255, 255)};
    for (unsigned k = 0; k < 3; ++k) {
        if (shape_mode_ && ((volume.type == 0 && k == 2) || (volume.type == 2 && k != 1)))
            continue;
        SpatialPoint direction{};
        direction[k] = 1;
        direction = rotate(direction);
        auto start = point;
        if (shape_mode_) {
            unsigned index = volume.type == 0 ? 3 + k : volume.type == 1 ? 7 + k : 6;
            for (unsigned n = 0; n < 3; ++n)
                start[n] += direction[n] * volume.values[index];
        }
        auto end = start;
        for (unsigned n = 0; n < 3; ++n)
            end[n] += direction[n] * length;
        if (!project(start, starts[k]) || !project(end, ends[k]))
            continue;
        auto d = ImVec2{ends[k].x - starts[k].x, ends[k].y - starts[k].y};
        float square = d.x * d.x + d.y * d.y;
        if (square < 144)
            continue;
        visible[k] = true;
        directions[k] = d;
        float t = std::clamp(
            ((io.MousePos.x - starts[k].x) * d.x + (io.MousePos.y - starts[k].y) * d.y) / square,
            0.f, 1.f);
        float distance = std::hypot(io.MousePos.x - starts[k].x - t * d.x,
                                    io.MousePos.y - starts[k].y - t * d.y);
        if (distance < nearest) {
            nearest = distance;
            hot = int(k);
        }
    }
    if (!hovered || io.KeyAlt || io.KeyCtrl || io.KeyShift)
        hot = -1;
    for (unsigned k = 0; k < 3; ++k)
        if (visible[k]) {
            auto color =
                (hot == int(k) || drag_axis_ == int(k)) ? IM_COL32(255, 225, 110, 255) : colors[k];
            draw->AddLine(starts[k], ends[k], color, 3);
            if (shape_mode_)
                draw->AddRectFilled({ends[k].x - 5, ends[k].y - 5}, {ends[k].x + 5, ends[k].y + 5},
                                    color);
            else {
                float n = std::hypot(directions[k].x, directions[k].y);
                ImVec2 d{directions[k].x / n, directions[k].y / n};
                draw->AddTriangleFilled(
                    ends[k], {ends[k].x - 12 * d.x + 5 * d.y, ends[k].y - 12 * d.y - 5 * d.x},
                    {ends[k].x - 12 * d.x - 5 * d.y, ends[k].y - 12 * d.y + 5 * d.x}, color);
            }
            const char *label = k == 0 ? "X" : k == 1 ? "Y" : "Z";
            if (shape_mode_ && volume.type == 0)
                label = k == 0 ? "Radius" : "Height";
            if (shape_mode_ && volume.type == 2)
                label = "Height";
            draw->AddText({ends[k].x + 7, ends[k].y - 7}, color, label);
        }
    draw->PopClipRect();
    if (drag_axis_ < 0 && hot >= 0 && ImGui::IsMouseClicked(0)) {
        drag_axis_ = hot;
        drag_entry_ = draft_.entry;
        drag_shapes_ = draft_.shapes;
        drag_volume_ = volume;
        drag_mouse_ = io.MousePos;
        drag_direction_ = directions[hot];
        handle_length_ = length;
    }
    bool captured = drag_axis_ >= 0 || hot >= 0;
    if (drag_axis_ >= 0) {
        float square =
            drag_direction_.x * drag_direction_.x + drag_direction_.y * drag_direction_.y;
        float delta = ((io.MousePos.x - drag_mouse_.x) * drag_direction_.x +
                       (io.MousePos.y - drag_mouse_.y) * drag_direction_.y) /
                      square * handle_length_;
        if (io.KeyCtrl)
            delta = std::round(delta);
        if (std::isfinite(delta)) {
            auto next = drag_volume_;
            unsigned index = component + unsigned(drag_axis_);
            if (shape_mode_)
                index = volume.type == 0   ? 3 + unsigned(drag_axis_)
                        : volume.type == 1 ? 7 + unsigned(drag_axis_)
                                           : 6;
            next.values[index] += delta;
            if (shape_mode_)
                next.values[index] = std::max(0.f, next.values[index]);
            shapes[shape_index_] = next;
            draft_.shapes[g] = std::move(shapes);
        }
        if (!ImGui::IsMouseDown(0)) {
            drag_axis_ = -1;
            remember_shapes(std::move(drag_shapes_));
        }
    }
    return captured;
}
bool OverworldEditor::patrol_active() const {
    return open_ && editing_ && patrol_editing_ && !loading_ && document_ && project_store() &&
           !renderer_.player.active && draft_.action != OverworldOperation::Action::Remove &&
           document_->entries()[draft_.entry].kind == OverworldKind::Trainer &&
           !patrol_draft_.path.points.empty();
}
void OverworldEditor::remember_patrol(TrainerPatrol before) {
    if (before == patrol_draft_)
        return;
    patrol_undo_.push_back(std::move(before));
    patrol_redo_.clear();
    draft_.patrol = patrol_draft_;
}
void OverworldEditor::undo_patrol(bool redo) {
    auto &from = redo ? patrol_redo_ : patrol_undo_;
    auto &to = redo ? patrol_undo_ : patrol_redo_;
    if (from.empty() || patrol_axis_ >= 0)
        return;
    to.push_back(patrol_draft_);
    patrol_draft_ = std::move(from.back());
    from.pop_back();
    draft_.patrol = patrol_draft_;
}
void OverworldEditor::patrol_controls(const SpatialPoint *cursor) {
    if (!TutorialWidgets::CollapsingHeader("trainer_patrol", "Trainer patrol"))
        return;
    ImGui::BeginDisabled(patrol_axis_ >= 0);
    ImGui::BeginDisabled(patrol_undo_.empty());
    if (TutorialWidgets::Button("trainer_patrol", "Undo patrol"))
        undo_patrol(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(patrol_redo_.empty());
    if (TutorialWidgets::Button("trainer_patrol", "Redo patrol"))
        undo_patrol(true);
    ImGui::EndDisabled();
    auto before = patrol_draft_;
    auto &path = patrol_draft_.path;
    if (path.points.empty()) {
        if (TutorialWidgets::Button("trainer_patrol", "Add patrol route")) {
            path.points = {{0, 0, 0}, {100, 0, 0}};
            path.follow_ground = true;
            patrol_draft_.movement = 8;
            patrol_draft_.actions.clear();
            patrol_draft_.signals.clear();
            patrol_draft_.motion = 0;
            patrol_draft_.frame = 0;
            patrol_editing_ = true;
        }
    } else {
        if (TutorialWidgets::Button("trainer_patrol", "Delete patrol route")) {
            path.points.clear();
            patrol_draft_.actions.clear();
            patrol_draft_.signals.clear();
            patrol_draft_.movement = 4;
            patrol_draft_.motion = 0;
            patrol_draft_.frame = 0;
            patrol_editing_ = false;
        }
        if (!path.points.empty()) {
            TutorialWidgets::Checkbox("trainer_patrol", "Edit route in viewport", &patrol_editing_);
            if (patrol_editing_)
                shape_group_ = shape_index_ = -1;
            ImGui::Checkbox("Curved", &path.curved);
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &path.loop);
            ImGui::SameLine();
            ImGui::Checkbox("Follow ground", &path.follow_ground);
            patrol_point_ = std::clamp(patrol_point_, 0, int(path.points.size()) - 1);
            ImGui::SliderInt("Point", &patrol_point_, 0, int(path.points.size()) - 1);
            ImGui::InputFloat3("Point offset", path.points[patrol_point_].data());
            ImGui::TextDisabled(
                "Offsets follow the trainer placement. Ctrl: snap; Esc: cancel drag.");
            if (cursor && TutorialWidgets::Button("trainer_patrol", "Place point at 3D cursor"))
                for (unsigned i = 0; i < 3; ++i)
                    path.points[patrol_point_][i] = (*cursor)[i] - draft_.position[i];
            if (TutorialWidgets::Button("trainer_patrol", "Insert point")) {
                auto point = path.points[patrol_point_];
                if (patrol_point_ + 1 < int(path.points.size()))
                    for (unsigned i = 0; i < 3; ++i)
                        point[i] = (point[i] + path.points[patrol_point_ + 1][i]) * .5f;
                else
                    point[0] += 100;
                path.points.insert(path.points.begin() + ++patrol_point_, point);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(path.points.size() <= 2);
            if (TutorialWidgets::Button("trainer_patrol", "Remove point"))
                path.points.erase(path.points.begin() + patrol_point_);
            ImGui::EndDisabled();
        }
    }
    if (ImGui::TreeNode("Movement and patrol actions")) {
        auto number = [](const char *label, unsigned &value) {
            ImGui::InputScalar(label, ImGuiDataType_U32, &value);
        };
        number("Movement type (raw)", patrol_draft_.movement);
        number(path.points.empty() ? "Idle motion" : "Movement motion", patrol_draft_.motion);
        ImGui::InputFloat("Start frame", &patrol_draft_.frame);
        if (path.points.empty())
            ImGui::TextWrapped("Choose an idle motion for this model after removing its route.");
        for (unsigned i = 0; i < patrol_draft_.actions.size(); ++i) {
            ImGui::PushID(int(i));
            auto &a = patrol_draft_.actions[i];
            if (ImGui::TreeNode("Action", "Patrol action %u", i + 1)) {
                ImGui::InputFloat("Route progress", &a.progress);
                number("Motion", a.motion);
                ImGui::InputFloat("Facing", &a.facing);
                number("Repeat count", a.repeats);
                ImGui::InputFloat("Start frame", &a.frame);
                if (ImGui::Button("Delete action")) {
                    patrol_draft_.actions.erase(patrol_draft_.actions.begin() + i);
                    --i;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (!path.points.empty() && ImGui::Button("Add patrol action"))
            patrol_draft_.actions.emplace_back();
        ImGui::TreePop();
    }
    remember_patrol(std::move(before));
    ImGui::EndDisabled();
}
bool OverworldEditor::patrol_gizmo(const float *view, const float *projection, ImVec2 origin,
                                   ImVec2 size, bool hovered) {
    auto &io = ImGui::GetIO();
    if (gizmo_blocked_) {
        gizmo_blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    if (patrol_axis_ >= 0 && (io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        patrol_draft_ = patrol_drag_;
        patrol_axis_ = -1;
        gizmo_blocked_ = true;
        return true;
    }
    if (hovered && patrol_axis_ < 0 && io.KeyCtrl && !io.WantTextInput &&
        !UndoShortcuts::consumed &&
        (ImGui::IsKeyPressed(ImGuiKey_Z, false) || ImGui::IsKeyPressed(ImGuiKey_Y, false))) {
        undo_patrol(io.KeyShift || ImGui::IsKeyPressed(ImGuiKey_Y, false));
        UndoShortcuts::consumed = true;
        return true;
    }
    auto project = [&](SpatialPoint p, ImVec2 &out) {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] = view[i] * p[0] + view[4 + i] * p[1] + view[8 + i] * p[2] + view[12 + i];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[j * 4 + i] * a[j];
        if (!std::isfinite(b[3]) || b[3] <= .001f)
            return false;
        float x = b[0] / b[3], y = b[1] / b[3];
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 100 || std::abs(y) > 100)
            return false;
        out = {origin.x + (x * .5f + .5f) * size.x, origin.y + (.5f - y * .5f) * size.y};
        return true;
    };
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    auto path = patrol_draft_.path;
    for (auto &point : path.points)
        for (unsigned i = 0; i < 3; ++i)
            point[i] += draft_.position[i];
    SpatialRegion geometry;
    append_route_geometry(geometry, path);
    for (unsigned i = 0; i + 1 < geometry.lines.size(); i += 2) {
        ImVec2 a, b;
        if (project(geometry.vertices[geometry.lines[i]].position, a) &&
            project(geometry.vertices[geometry.lines[i + 1]].position, b))
            draw->AddLine(a, b, IM_COL32(90, 220, 255, 220), 2);
    }
    bool captured = patrol_axis_ >= 0;
    for (unsigned i = 0; i < path.points.size(); ++i) {
        ImVec2 p;
        if (!project(path.points[i], p))
            continue;
        draw->AddCircleFilled(p, i == unsigned(patrol_point_) ? 6.f : 4.f,
                              IM_COL32(255, 210, 85, 255));
        if (hovered && patrol_axis_ < 0 && !io.KeyCtrl && ImGui::IsMouseClicked(0) &&
            std::hypot(p.x - io.MousePos.x, p.y - io.MousePos.y) < 8) {
            patrol_point_ = int(i);
            captured = true;
        }
    }
    patrol_point_ = std::clamp(patrol_point_, 0, int(path.points.size()) - 1);
    auto point = path.points[patrol_point_];
    ImVec2 center;
    if (!project(point, center)) {
        draw->PopClipRect();
        return captured;
    }
    float depth = -(view[2] * point[0] + view[6] * point[1] + view[10] * point[2] + view[14]);
    float length = std::clamp(depth * .12f, 15.f, 2000.f);
    const ImU32 colors[]{IM_COL32(245, 85, 80, 255), IM_COL32(90, 230, 115, 255),
                         IM_COL32(80, 160, 255, 255)};
    for (unsigned axis = 0; axis < 3; ++axis) {
        auto end = point;
        end[axis] += length;
        ImVec2 tip;
        if (!project(end, tip))
            continue;
        ImVec2 d{tip.x - center.x, tip.y - center.y};
        float square = d.x * d.x + d.y * d.y;
        if (square < 144)
            continue;
        float t = std::clamp(((io.MousePos.x - center.x) * d.x + (io.MousePos.y - center.y) * d.y) /
                                 square,
                             0.f, 1.f);
        bool hot =
            hovered && !io.KeyAlt && !io.KeyCtrl &&
            std::hypot(io.MousePos.x - center.x - t * d.x, io.MousePos.y - center.y - t * d.y) < 8;
        draw->AddLine(
            center, tip,
            hot || patrol_axis_ == int(axis) ? IM_COL32(255, 225, 110, 255) : colors[axis], 3);
        draw->AddText(tip, colors[axis], axis == 0 ? "X" : axis == 1 ? "Y" : "Z");
        if (hot && patrol_axis_ < 0 && ImGui::IsMouseClicked(0)) {
            patrol_axis_ = int(axis);
            patrol_drag_ = patrol_draft_;
            patrol_mouse_ = io.MousePos;
            patrol_direction_ = d;
            patrol_handle_ = length;
            captured = true;
        }
    }
    draw->PopClipRect();
    if (patrol_axis_ >= 0) {
        float square =
            patrol_direction_.x * patrol_direction_.x + patrol_direction_.y * patrol_direction_.y;
        float delta = ((io.MousePos.x - patrol_mouse_.x) * patrol_direction_.x +
                       (io.MousePos.y - patrol_mouse_.y) * patrol_direction_.y) /
                      square * patrol_handle_;
        if (io.KeyCtrl)
            delta = std::round(delta);
        if (std::isfinite(delta))
            patrol_draft_.path.points[patrol_point_][patrol_axis_] =
                patrol_drag_.path.points[patrol_point_][patrol_axis_] + delta;
        if (!ImGui::IsMouseDown(0)) {
            patrol_axis_ = -1;
            remember_patrol(std::move(patrol_drag_));
        }
    }
    return captured;
}

}
