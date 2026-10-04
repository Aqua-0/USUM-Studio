#include "native/tutorial_widgets.h"
#include "native/pokemon_effect_point_editor.h"
#include "assets/material_document.h"
#include "assets/pokemon_decoration_points.h"
#include "native/viewport_navigation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace studio {
namespace {
PokemonEffectPoints display_points(const ModelDocument &model, unsigned group, bool decorations,
                                  std::vector<PokemonDecorationPoint> &locators) {
    if (!decorations)
        return pokemon_effect_points(model, group);
    locators = pokemon_decoration_points(model, group);
    PokemonEffectPoints result;
    for (std::size_t i = 0; i < locators.size(); ++i) {
        const auto &p = locators[i];
        result.points.push_back({i, p.bone, {p.local[3], p.local[7], p.local[11]}, 0, 1});
    }
    return result;
}
std::string display_label(const PokemonEffectPoint &point, const std::vector<PokemonDecorationPoint> &locators) {
    return locators.empty() ? pokemon_effect_point_label(point) : locators.at(point.record).name;
}
}

void PokemonEffectPointEditor::reset() {
    cancel_drag();
    drag_blocked_ = false;
    group_ = 0;
    selected_ = 0;
    reveal_selection_ = false;
    error_.clear();
}
void PokemonEffectPointEditor::draw(const ModelDocument &model, MaterialDocument *edit) {
    ImGui::SeparatorText(decorations_ ? "Decoration points" : "Effect points");
    TutorialWidgets::item("pokemon_points", decorations_ ? "Decoration points" : "Effect points");
    std::vector<PokemonDecorationPoint> locators;
    int group = int(group_);
    if (ImGui::Combo("Motion set", &group, "Battle\0Refresh\0Field\0")) {
        cancel_drag();
        group_ = unsigned(group);
        selected_ = 0;
        error_.clear();
    }
    TutorialWidgets::Checkbox("pokemon_points", "Labels", &labels_);
    ImGui::SameLine();
    TutorialWidgets::Checkbox("pokemon_points", "Selected only", &selected_only_);
    try {
        auto pack = display_points(edit ? edit->model : model, group_, decorations_, locators);
        const auto &current = edit ? edit->model : model;
        auto bone_picker = [&](const char *label, std::string &bone) {
            bool changed = false;
            if (ImGui::BeginCombo(label, bone.c_str())) {
                if (current.scene && !current.scene->skeletons.empty())
                    for (const auto &joint : current.scene->skeletons.front().joints)
                        if (ImGui::Selectable(joint.name.c_str(), joint.name == bone)) {
                            bone = joint.name;
                            changed = true;
                        }
                ImGui::EndCombo();
            }
            return changed;
        };
        if (pack.points.empty()) {
            selected_ = 0;
            ImGui::TextUnformatted(decorations_ ? "This motion set has no decoration points." : "This motion set has no effect points.");
        } else {
            selected_ = std::clamp(selected_, 0, int(pack.points.size()) - 1);
            if (ImGui::BeginListBox("##effect-points", {-1, 190})) {
                for (unsigned i = 0; i < pack.points.size(); ++i) {
                    auto label = display_label(pack.points[i], locators);
                    if (ImGui::Selectable(label.c_str(), selected_ == int(i)))
                        selected_ = int(i);
                    if (reveal_selection_ && selected_ == int(i))
                        ImGui::SetScrollHereY(.5f);
                }
                reveal_selection_ = false;
                ImGui::EndListBox();
            }
            auto point = pack.points[std::size_t(selected_)];
            auto locator = decorations_ ? locators.at(point.record) : PokemonDecorationPoint{};
            bool changed = false;
            ImGui::BeginDisabled(!edit);
            if (decorations_) {
                char name[64]{};
                std::copy(locator.name.begin(), locator.name.end(), name);
                if (ImGui::InputText("Point name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    locator.name = name;
                    changed = true;
                }
            }
            ImGui::SetNextItemWidth(-90);
            changed |= bone_picker("Parent bone", point.bone);
            ImGui::SetNextItemWidth(-90);
            bool moved = ImGui::InputFloat3("Local XYZ", point.offset.data(), "%.3f",
                                           ImGuiInputTextFlags_EnterReturnsTrue);
            TutorialWidgets::item("pokemon_points", "Local XYZ", moved);
            changed |= moved;
            if (decorations_) {
                locator.bone = point.bone;
                if (moved)
                    for (unsigned axis = 0; axis < 3; ++axis)
                        locator.local[axis * 4 + 3] = point.offset[axis];
                if (ImGui::TreeNode("Local transform")) {
                    ImGui::TextWrapped("Rows of the local 4 by 4 matrix. XYZ is the last column of the first three rows.");
                    for (unsigned row = 0; row < 4; ++row) {
                        ImGui::PushID(int(row));
                        changed |= ImGui::InputFloat4("##matrix-row", locator.local.data() + row * 4,
                                                      "%.4f", ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::EndDisabled();
            if (current.scene && !current.scene->skeletons.empty()) {
                const auto &joints = current.scene->skeletons.front().joints;
                if (std::none_of(joints.begin(), joints.end(), [&](const auto &j) { return j.name == point.bone; }))
                    ImGui::TextWrapped("Parent bone is missing: %s. Choose an existing bone to repair this point.", point.bone.c_str());
            }
            if (changed && edit) {
                if (decorations_) edit->edit_decoration_point(group_, point.record, locator);
                else edit->edit_effect_point(group_, point);
                error_.clear();
            }
        }
        ImGui::BeginDisabled(!edit);
        if (TutorialWidgets::Button("pokemon_points", "Add point...")) {
            new_bone_ = pack.points.empty() ? std::string{} : pack.points[std::size_t(selected_)].bone;
            if (new_bone_.empty() && current.scene && !current.scene->skeletons.empty() &&
                !current.scene->skeletons.front().joints.empty())
                new_bone_ = current.scene->skeletons.front().joints.front().name;
            new_offset_ = {};
            new_category_ = pack.points.empty() ? 0 : pack.points[std::size_t(selected_)].category;
            new_index_ = 1;
            for (unsigned suffix = 1;; ++suffix) {
                std::snprintf(new_name_, sizeof(new_name_), "PdPoint%02u", suffix);
                if (std::none_of(locators.begin(), locators.end(), [&](const auto &p) { return p.name == new_name_; }))
                    break;
            }
            error_.clear();
            ImGui::OpenPopup("New point");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(pack.points.empty());
        bool remove = TutorialWidgets::Button("pokemon_points", "Remove point");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (remove && edit && !pack.points.empty()) {
            auto record = pack.points[std::size_t(selected_)].record;
            if (decorations_) edit->remove_decoration_point(group_, record);
            else edit->remove_effect_point(group_, record);
            selected_ = std::max(0, selected_ - 1);
            reveal_selection_ = true;
            error_.clear();
        }
        if (ImGui::BeginPopup("New point")) {
            if (decorations_) {
                ImGui::InputText("Point name", new_name_, sizeof(new_name_));
            } else {
                if (ImGui::BeginCombo("Category", pokemon_effect_point_category(new_category_))) {
                    for (unsigned category = 0; category < 19; ++category)
                        if (ImGui::Selectable(pokemon_effect_point_category(category), new_category_ == category))
                            new_category_ = category;
                    ImGui::EndCombo();
                }
                auto used = [&](int index) {
                    return std::any_of(pack.points.begin(), pack.points.end(), [&](const auto &p) {
                        return p.category == new_category_ && p.index == unsigned(index);
                    });
                };
                if (used(new_index_))
                    for (int index = 1; index <= 8; ++index)
                        if (!used(index)) { new_index_ = index; break; }
                if (ImGui::BeginCombo("Index", std::to_string(new_index_).c_str())) {
                    for (int index = 1; index <= 8; ++index) {
                        ImGui::BeginDisabled(used(index));
                        if (ImGui::Selectable(std::to_string(index).c_str(), index == new_index_))
                            new_index_ = index;
                        ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
            }
            bone_picker("Parent bone", new_bone_);
            ImGui::InputFloat3("Local XYZ", new_offset_.data(), "%.3f");
            bool available = !new_bone_.empty();
            if (decorations_) {
                available &= new_name_[0] != 0 && std::none_of(locators.begin(), locators.end(),
                    [&](const auto &p) { return p.name == new_name_; });
                if (!available) ImGui::TextWrapped("Choose a parent and a unique, nonempty point name.");
            } else {
                available &= std::none_of(pack.points.begin(), pack.points.end(), [&](const auto &p) {
                    return p.category == new_category_ && p.index == unsigned(new_index_);
                });
                if (!available) ImGui::TextWrapped("Choose a parent and an unused index. Each category supports eight points.");
            }
            ImGui::BeginDisabled(!edit || !available);
            bool add = TutorialWidgets::Button("pokemon_points", "Create point");
            ImGui::EndDisabled();
            if (add && edit) {
                try {
                    if (decorations_) {
                        PokemonDecorationPoint point{new_name_, new_bone_, pose_identity()};
                        for (unsigned axis = 0; axis < 3; ++axis)
                            point.local[axis * 4 + 3] = new_offset_[axis];
                        edit->add_decoration_point(group_, point);
                        selected_ = int(locators.size());
                    } else {
                        PokemonEffectPoint point{0, new_bone_, new_offset_, new_category_, unsigned(new_index_)};
                        edit->add_effect_point(group_, point);
                        auto updated = pokemon_effect_points(edit->model, group_).points;
                        for (unsigned i = 0; i < updated.size(); ++i)
                            if (updated[i].category == new_category_ && updated[i].index == unsigned(new_index_))
                                selected_ = int(i);
                    }
                    reveal_selection_ = true;
                    error_.clear();
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception &e) { error_ = e.what(); }
            }
            ImGui::SameLine();
            if (TutorialWidgets::Button("pokemon_points", "Cancel")) ImGui::CloseCurrentPopup();
            if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
            ImGui::EndPopup();
        }
        if (edit) {
            TutorialWidgets::Checkbox("pokemon_points", "Move gizmo", &move_gizmo_);
            ImGui::TextWrapped("Drag X/Y/Z along the parent bone axes. Ctrl: snap to 1 local unit. Esc: cancel. A drag pauses playback and creates one undo step.");
            ImGui::TextWrapped("Press Enter to apply values. Undo, Save Project and game export use the Studio document controls. Changes affect forms sharing this motion set.");
        } else {
            ImGui::TextWrapped("Send this model to Studio to edit, add or remove points.");
        }
        ImGui::TextWrapped("Points follow the parent bone. Changing the parent keeps the local transform. Markers show through the model.");
        if (decorations_)
            ImGui::TextWrapped("Retail retains these anchors but skips their decoration block during normal model setup.");
        if (current.motion >= 0 && current.motions[std::size_t(current.motion)].group != group_)
            ImGui::TextWrapped("The playing animation belongs to another motion set; these markers use the selected set's points.");
    } catch (const std::exception &e) {
        error_ = e.what();
    }
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
}
bool PokemonEffectPointEditor::viewport(const ModelDocument &model, double seconds, bool animated,
                                       const float *view, const float *projection,
                                       ImVec2 origin, ImVec2 size, bool hovered,
                                       MaterialDocument *edit, bool *playing) {
    if (!model.scene || model.scene->skeletons.empty() || size.x <= 0 || size.y <= 0) {
        cancel_drag();
        return false;
    }
    PokemonEffectPoints pack;
    std::vector<PokemonDecorationPoint> locators;
    try {
        pack = display_points(model, group_, decorations_, locators);
    } catch (const std::exception &) {
        cancel_drag();
        return false;
    }
    auto &io = ImGui::GetIO();
    bool input = !io.WantTextInput && !io.AppFocusLost &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (dragging() && (!edit || !move_gizmo_ || !input || viewport_navigating() ||
        (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape) || selected_ != drag_selection_ || group_ != drag_group_ ||
        model.motion != drag_motion_ || animated != drag_animated_ || seconds != drag_seconds_ ||
        edit->identity() != drag_identity_ || edit->revision() != drag_revision_)) {
        cancel_drag();
        drag_blocked_ = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    }
    if (drag_blocked_) {
        drag_blocked_ = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        return true;
    }
    if (dragging()) {
        if (playing) *playing = false;
        float pixels = (io.MousePos.x - drag_mouse_.x) * drag_direction_.x +
                       (io.MousePos.y - drag_mouse_.y) * drag_direction_.y;
        unsigned component = std::abs(drag_direction_.x) >= std::abs(drag_direction_.y) ? 0 : 1;
        float screen = component == 0 ? drag_center_.x + pixels * drag_direction_.x
                                      : drag_center_.y + pixels * drag_direction_.y;
        float ndc = component == 0 ? 2 * (screen - origin.x) / size.x - 1
                                   : 1 - 2 * (screen - origin.y) / size.y;
        float denominator = drag_axis_clip_[component] - ndc * drag_axis_clip_[3];
        if (std::abs(denominator) > 1e-8f) {
            float delta = (ndc * drag_clip_[3] - drag_clip_[component]) / denominator;
            if (io.KeyCtrl) delta = std::round(delta);
            if (std::isfinite(delta) && drag_clip_[3] + delta * drag_axis_clip_[3] > .001f) {
                drag_offset_ = drag_original_;
                drag_offset_[unsigned(drag_axis_)] += delta;
            }
        }
        if (selected_ >= 0 && std::size_t(selected_) < pack.points.size())
            pack.points[std::size_t(selected_)].offset = drag_offset_;
    }
    auto &rig = model.scene->skeletons.front();
    auto pose = evaluate_skeleton(rig, seconds, 12, animated);
    auto project = [&](const std::array<float, 3> &p) -> std::optional<ImVec2> {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] = view[i] * p[0] + view[i + 4] * p[1] + view[i + 8] * p[2] + view[i + 12];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[j * 4 + i] * a[j];
        if (b[3] <= .001f || !std::isfinite(b[3]))
            return {};
        ImVec2 screen{origin.x + (b[0] / b[3] * .5f + .5f) * size.x,
                      origin.y + (.5f - b[1] / b[3] * .5f) * size.y};
        if (screen.x < origin.x || screen.y < origin.y || screen.x > origin.x + size.x || screen.y > origin.y + size.y)
            return {};
        return screen;
    };
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    int hit = -1;
    float nearest = 100;
    for (unsigned i = 0; i < pack.points.size(); ++i) {
        if (selected_only_ && int(i) != selected_)
            continue;
        const auto &point = pack.points[i];
        auto world = pokemon_effect_point_position(point, rig, pose);
        auto screen = world ? project(*world) : std::nullopt;
        if (!screen)
            continue;
        bool selected = int(i) == selected_;
        auto color = selected ? IM_COL32(255, 205, 70, 255)
                     : point.category >= 10 && point.category <= 17 ? IM_COL32(255, 115, 100, 255)
                                                                    : IM_COL32(90, 225, 245, 255);
        draw->AddCircleFilled(*screen, selected ? 6.f : 4.f, color);
        draw->AddCircle(*screen, selected ? 7.f : 5.f, IM_COL32(0, 0, 0, 220));
        if (labels_ || selected) {
            auto label = display_label(point, locators);
            draw->AddText({screen->x + 10, screen->y - 6}, IM_COL32(0, 0, 0, 230), label.c_str());
            draw->AddText({screen->x + 9, screen->y - 7}, color, label.c_str());
        }
        if (selected) {
            auto bone = point;
            bone.offset = {};
            auto parent = pokemon_effect_point_position(bone, rig, pose);
            if (auto p = parent ? project(*parent) : std::nullopt)
                draw->AddLine(*p, *screen, color);
        }
        auto mouse = ImGui::GetIO().MousePos;
        float distance = (screen->x - mouse.x) * (screen->x - mouse.x) +
                         (screen->y - mouse.y) * (screen->y - mouse.y);
        if (distance < nearest) {
            nearest = distance;
            hit = int(i);
        }
    }
    bool gizmo_captured = dragging();
    if (edit && move_gizmo_ && selected_ >= 0 && std::size_t(selected_) < pack.points.size()) {
        const auto &point = pack.points[std::size_t(selected_)];
        auto joint = std::find_if(rig.joints.begin(), rig.joints.end(), [&](const auto &j) { return j.name == point.bone; });
        auto index = std::size_t(joint - rig.joints.begin());
        if (joint != rig.joints.end() && index < pose.size()) {
            auto basis = pose_multiply(pose_multiply(pose[index], rig.placement), joint->bind);
            auto world = pokemon_effect_point_position(point, rig, pose);
            auto center = world ? project(*world) : std::nullopt;
            auto clip = [&](const std::array<float, 3> &p, bool position) {
                std::array<float, 4> a{}, b{};
                for (unsigned r = 0; r < 4; ++r)
                    a[r] = view[r] * p[0] + view[r + 4] * p[1] + view[r + 8] * p[2] + (position ? view[r + 12] : 0);
                for (unsigned r = 0; r < 4; ++r)
                    for (unsigned c = 0; c < 4; ++c) b[r] += projection[c * 4 + r] * a[c];
                return b;
            };
            if (center) {
                auto base_clip = clip(*world, true);
                int hot = -1;
                float nearest_axis = 8;
                std::array<ImVec2, 3> directions{};
                const ImU32 colors[]{IM_COL32(245, 95, 85, 255), IM_COL32(100, 235, 130, 255), IM_COL32(95, 155, 255, 255)};
                for (unsigned axis = 0; axis < 3; ++axis) {
                    auto vector = clip({basis[axis], basis[4 + axis], basis[8 + axis]}, false);
                    ImVec2 direction{(vector[0] * base_clip[3] - base_clip[0] * vector[3]) * size.x * .5f,
                                     -(vector[1] * base_clip[3] - base_clip[1] * vector[3]) * size.y * .5f};
                    float length = std::hypot(direction.x, direction.y);
                    if (length < 1e-6f) continue;
                    direction.x /= length;
                    direction.y /= length;
                    directions[axis] = direction;
                    ImVec2 end{center->x + 68 * direction.x, center->y + 68 * direction.y};
                    ImVec2 start{center->x + 12 * direction.x, center->y + 12 * direction.y};
                    float along = std::clamp((io.MousePos.x - center->x) * direction.x +
                                            (io.MousePos.y - center->y) * direction.y, 12.f, 68.f);
                    float distance = std::hypot(io.MousePos.x - center->x - along * direction.x,
                                                io.MousePos.y - center->y - along * direction.y);
                    if (distance < nearest_axis) { nearest_axis = distance; hot = int(axis); }
                    auto color = drag_axis_ == int(axis) ? IM_COL32(255, 220, 80, 255) : colors[axis];
                    draw->AddLine(start, end, color, 3);
                    draw->AddTriangleFilled(end, {end.x - 10 * direction.x - 4 * direction.y, end.y - 10 * direction.y + 4 * direction.x},
                                                 {end.x - 10 * direction.x + 4 * direction.y, end.y - 10 * direction.y - 4 * direction.x}, color);
                    const char *names[]{"X", "Y", "Z"};
                    draw->AddText({end.x + 4, end.y - 7}, color, names[axis]);
                }
                if (!dragging() && hovered && input && !viewport_navigating() && hot >= 0) {
                    gizmo_captured = true;
                    ImGui::SetTooltip("Move local %s | Ctrl: snap | Esc: cancel", hot == 0 ? "X" : hot == 1 ? "Y" : "Z");
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        drag_axis_ = hot;
                        drag_selection_ = selected_;
                        drag_group_ = group_;
                        drag_motion_ = model.motion;
                        drag_seconds_ = seconds;
                        drag_animated_ = animated;
                        drag_revision_ = edit->revision();
                        drag_identity_ = edit->identity();
                        drag_mouse_ = io.MousePos;
                        drag_center_ = *center;
                        drag_direction_ = directions[unsigned(hot)];
                        drag_clip_ = base_clip;
                        drag_axis_clip_ = clip({basis[hot], basis[4 + hot], basis[8 + hot]}, false);
                        drag_original_ = drag_offset_ = point.offset;
                        if (playing) *playing = false;
                        error_.clear();
                    }
                }
            }
        }
    }
    draw->PopClipRect();
    if (dragging() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        auto point = pack.points[std::size_t(selected_)];
        bool changed = false;
        for (unsigned axis = 0; axis < 3; ++axis)
            changed |= std::abs(drag_offset_[axis] - drag_original_[axis]) > 1e-5f;
        cancel_drag();
        if (changed) {
            try {
                if (decorations_) {
                    auto locator = locators.at(point.record);
                    for (unsigned axis = 0; axis < 3; ++axis) locator.local[axis * 4 + 3] = point.offset[axis];
                    edit->edit_decoration_point(group_, point.record, locator);
                } else edit->edit_effect_point(group_, point);
                error_.clear();
            } catch (const std::exception &e) { error_ = e.what(); }
        }
        return true;
    }
    if (gizmo_captured) return true;
    if (hovered && hit >= 0 && !viewport_navigating()) {
        ImGui::SetTooltip("%s\nParent: %s", display_label(pack.points[std::size_t(hit)], locators).c_str(),
                          pack.points[std::size_t(hit)].bone.c_str());
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < ImGui::GetIO().MouseDragThreshold * ImGui::GetIO().MouseDragThreshold)
        {
            selected_ = hit;
            reveal_selection_ = true;
        }
        return true;
    }
    return false;
}
}
