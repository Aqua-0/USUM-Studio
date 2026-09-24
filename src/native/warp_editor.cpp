#include "native/tutorial_widgets.h"
#include "native/warp_editor.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
namespace studio {
void WarpEditor::set_scene(std::shared_ptr<Environment> scene, unsigned area,
                           const std::filesystem::path &dump) {
    scene_ = std::move(scene);
    area_ = area;
    dump_ = dump;
    project_.unbind();
    drag_axis_ = -1;
    document_.reset();
    destinations_.clear();
    message_.clear();
    behaviors_.clear();
    behavior_error_.clear();
    try {
        behaviors_ = load_entrance_behaviors(dump, scene_->archive_sources);
    } catch (const std::exception &e) {
        behavior_error_ = e.what();
    }
    try {
        document_ = std::make_unique<WarpDocument>(area, scene_->placement_source);
        project_.bind(
            "warps", "warps/" + std::to_string(area), "Entrances in area " + std::to_string(area),
            std::to_string(area),
            [this] {
                return document_ && document_->dirty();
            },
            [this] {
                document_->commit();
                return project_text(document_->serialize());
            },
            [this] {
                document_->mark_saved();
            });
        project_.autosave_when([this] {
            return drag_axis_ < 0;
        });
        if (auto file = project_.document(); !file.empty()) {
            document_->restore(text(read_file(file)));
            project_.restored();
            synchronize();
        }
    } catch (const std::exception &e) {
        document_.reset();
        message_ = e.what();
    }
}
void WarpEditor::synchronize() {
    SpatialScene next;
    decode_overworld_regions(next, document_->compile(), {});
    auto matches = [](const auto &a, const auto &b) {
        return a.kind == SpatialKind::Entrance && b.kind == SpatialKind::Entrance && a.overworld &&
               b.overworld && a.overworld->local_zone == b.overworld->local_zone &&
               a.overworld->event == b.overworld->event;
    };
    std::vector<SpatialRegion> regions;
    int selected = -1;
    for (unsigned i = 0; i < scene_->spatial.regions.size(); ++i) {
        const auto &old = scene_->spatial.regions[i];
        auto replacement = std::find_if(next.regions.begin(), next.regions.end(), [&](auto &r) {
            return matches(old, r);
        });
        if (old.kind == SpatialKind::Entrance && replacement == next.regions.end())
            continue;
        if (renderer_.spatial.selected == int(i))
            selected = int(regions.size());
        regions.push_back(replacement == next.regions.end() ? old : *replacement);
    }
    for (const auto &r : next.regions)
        if (r.kind == SpatialKind::Entrance &&
            std::none_of(regions.begin(), regions.end(), [&](const auto &old) {
                return matches(old, r);
            }))
            regions.push_back(r);
    scene_->spatial.regions = std::move(regions);
    renderer_.spatial.selected = selected;
    renderer_.spatial.invalidate_geometry();
    renderer_.invalidate_selection_readback();
}
int WarpEditor::selected_record() const {
    int selected = renderer_.spatial.selected;
    if (!document_ || !scene_ || selected < 0 ||
        std::size_t(selected) >= scene_->spatial.regions.size())
        return -1;
    auto &r = scene_->spatial.regions[selected];
    if (r.kind != SpatialKind::Entrance || !r.overworld)
        return -1;
    for (unsigned i = 0; i < document_->records().size(); ++i) {
        auto &e = document_->records()[i];
        if (e.local_zone == r.overworld->local_zone && e.row == r.overworld->row)
            return int(i);
    }
    return -1;
}
void WarpEditor::select(unsigned index) {
    const auto &e = document_->records().at(index);
    for (unsigned i = 0; i < scene_->spatial.regions.size(); ++i) {
        auto &r = scene_->spatial.regions[i];
        if (r.kind == SpatialKind::Entrance && r.overworld &&
            r.overworld->local_zone == e.local_zone && r.overworld->row == e.row) {
            renderer_.spatial.selected = int(i);
            break;
        }
    }
    renderer_.spatial.enabled[unsigned(SpatialKind::Entrance)] = true;
    renderer_.spatial.pick_overlays = true;
}
void WarpEditor::refresh_destinations() {
    destinations_ = load_warp_destinations(dump_, scene_->archive_sources);
    std::erase_if(destinations_, [&](auto &d) {
        return d.area == area_;
    });
    for (auto &r : document_->records())
        destinations_.push_back({area_, r.zone, r.event,
                                 "Zone " + std::to_string(r.zone) + " / entrance " +
                                     std::to_string(r.event) + " (this map)"});
}
void WarpEditor::draw(bool loading, ViewportCamera &camera, const SpatialPoint *cursor,
                      bool show_launcher) {
    loading_ = loading;
    int index = -1;
    auto selected = renderer_.spatial.selected;
    if (document_ && scene_ && selected >= 0 &&
        std::size_t(selected) < scene_->spatial.regions.size()) {
        auto &r = scene_->spatial.regions[selected];
        if (r.kind == SpatialKind::Entrance && r.overworld)
            for (unsigned i = 0; i < document_->records().size(); ++i) {
                auto &entry = document_->records()[i];
                if (entry.local_zone == r.overworld->local_zone && entry.row == r.overworld->row) {
                    index = int(i);
                    break;
                }
            }
    }
    if (show_launcher) {
        ImGui::Begin("Map inspector");
        ImGui::BeginDisabled(!document_ || loading);
        if (studio::TutorialWidgets::Button("warp_editor", "Entrances...", ImVec2(-1, 0)))
            open_ = true;
        ImGui::EndDisabled();
        if (!document_ && !message_.empty())
            ImGui::TextWrapped("Warp editing unavailable: %s", message_.c_str());
        ImGui::End();
    }
    if (!open_)
        return;
    ImGui::SetNextWindowSize({510, 680}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Entrances", &open_)) {
        if (document_ &&
            ImGui::BeginCombo(
                "Entrance",
                index >= 0 ? ("Zone " + std::to_string(document_->records()[index].zone) +
                              " / entrance " + std::to_string(document_->records()[index].event))
                                 .c_str()
                           : "Choose an entrance")) {
            for (unsigned i = 0; i < document_->records().size(); ++i) {
                const auto &record = document_->records()[i];
                const auto label = "Zone " + std::to_string(record.zone) + " / entrance " +
                                   std::to_string(record.event);
                if (ImGui::Selectable(label.c_str(), index == int(i))) {
                    index = int(i);
                    for (unsigned region = 0; region < scene_->spatial.regions.size(); ++region) {
                        const auto &candidate = scene_->spatial.regions[region];
                        if (candidate.kind == SpatialKind::Entrance && candidate.overworld &&
                            candidate.overworld->local_zone == record.local_zone &&
                            candidate.overworld->row == record.row) {
                            renderer_.spatial.selected = int(region);
                            renderer_.spatial.enabled[std::size_t(SpatialKind::Entrance)] = true;
                            break;
                        }
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (document_) {
            ImGui::BeginDisabled(!project_store() || loading || drag_axis_ >= 0);
            auto update = [&](auto action) {
                try {
                    const auto before = document_->records();
                    action();
                    synchronize();
                    destinations_.clear();
                    int selection = selected_record();
                    for (unsigned i = 0; i < document_->records().size(); ++i) {
                        const auto &r = document_->records()[i];
                        if (std::none_of(before.begin(), before.end(), [&](const auto &old) {
                                return old.local_zone == r.local_zone && old.event == r.event;
                            })) {
                            selection = int(i);
                            break;
                        }
                    }
                    if (selection < 0 && !document_->records().empty())
                        selection = std::clamp(index, 0, int(document_->records().size()) - 1);
                    if (selection >= 0)
                        select(unsigned(selection));
                    index = selected_record();
                } catch (const std::exception &e) {
                    message_ = e.what();
                }
            };
            ImGui::BeginDisabled(!document_->can_undo());
            if (studio::TutorialWidgets::Button("warp_editor", "Undo"))
                update([&] {
                    document_->undo();
                    message_.clear();
                });
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!document_->can_redo());
            if (studio::TutorialWidgets::Button("warp_editor", "Redo"))
                update([&] {
                    document_->redo();
                    message_.clear();
                });
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("warp_editor", "Save Project"))
                update([&] {
                    save_editor_project();
                    message_ = "Saved. Stage Project updates game resources.";
                });
            ImGui::SameLine();
            ImGui::BeginDisabled(index < 0);
            if (ImGui::Button("Delete entrance"))
                update([&] {
                    document_->remove(unsigned(index));
                    message_ = "Entrance and trigger deleted. Undo restores them. Staging checks "
                               "incoming entrance links; script references must be reviewed separately.";
                });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Removes this entrance and its trigger geometry. Door scenery "
                                  "and other entrances are retained. Undo is available.");
            ImGui::EndDisabled();
            ImGui::TextUnformatted(document_->dirty()     ? "Unsaved warp edits"
                                   : document_->changed() ? "Warp edits saved in project"
                                                          : "No warp edits");
            ImGui::EndDisabled();
        }
        ImGui::TextWrapped("Move the visible door in Scene and its trigger and arrival here.");
        if (!project_store())
            ImGui::TextWrapped("Open an editor project to save and stage warp edits. Inspection is "
                               "available here without a project.");
        if (index < 0)
            ImGui::TextWrapped(document_ && document_->records().empty()
                                   ? "No entrances in this area. Undo can restore a deleted entrance."
                                   : "Choose an entrance above or select one in Maps > Scene or Layers.");
        else {
            ImGui::BeginDisabled(!project_store() || loading || drag_axis_ >= 0);
            if (ImGui::Button("New from selected")) {
                try {
                    index = int(document_->duplicate(unsigned(index)));
                    auto v = document_->values(unsigned(index));
                    auto position = cursor ? *cursor : v.position;
                    if (!cursor)
                        position[0] += 100;
                    for (unsigned k = 0; k < 3; ++k) {
                        v.arrival[k] += position[k] - v.position[k];
                        v.position[k] = position[k];
                    }
                    document_->set(unsigned(index), v);
                    document_->commit();
                    synchronize();
                    select(unsigned(index));
                    destinations_.clear();
                    message_ =
                        "New entrance created with its own trigger and event ID. Transition style, "
                        "camera links and conditions were copied; choose its destination.";
                } catch (const std::exception &e) {
                    message_ = e.what();
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Creates an entrance in the same zone, at the 3D cursor or 100 units beside "
                    "the selected template. The visible door is separate.");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Frame")) {
                auto v = document_->values(unsigned(index));
                auto low = v.position, high = v.arrival;
                for (unsigned k = 0; k < 3; ++k) {
                    low[k] = std::min(v.position[k], v.arrival[k]) - 100;
                    high[k] = std::max(v.position[k], v.arrival[k]) + 150;
                }
                camera.fit(low, high);
                camera.bounds_low = scene_->low;
                camera.bounds_high = scene_->high;
                select(unsigned(index));
            }
            ImGui::BeginDisabled(drag_axis_ >= 0);
            int target = mode_ >= 3 ? 2 : mode_ == 1 ? 1 : 0;
            if (ImGui::Combo("Gizmo target", &target, "Trigger\0Arrival\0Shape\0"))
                mode_ = target == 2 ? 3 : target;
            if (ImGui::RadioButton("Move (G)", mode_ == 0 || mode_ == 1 || mode_ == 3))
                mode_ = target == 2 ? 3 : target;
            ImGui::SameLine();
            ImGui::BeginDisabled(target != 0);
            if (ImGui::RadioButton("Rotate (R)", mode_ == 2))
                mode_ = 2;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(target != 2);
            if (ImGui::RadioButton("Scale (S)", mode_ == 4))
                mode_ = 4;
            ImGui::EndDisabled();
            auto shape_count = document_->records()[index].shapes.size();
            if (shape_count && mode_ >= 3) {
                shape_ = std::clamp(shape_, 0, int(shape_count) - 1);
                ImGui::SliderInt("Trigger shape", &shape_, 0, int(shape_count) - 1, "%d");
                if (document_->records()[index].shapes[shape_].type == 2 && mode_ == 3)
                    ImGui::Combo("Endpoint", &endpoint_, "Start\0End\0");
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled("Ctrl: snap | Esc: cancel");
            if (mode_ >= 3 &&
                (!shape_count || (document_->records()[index].shapes[shape_].type > 2)))
                ImGui::TextWrapped("Choose Trigger to move this shape with the entrance.");
            auto record = document_->records()[index];
            auto v = document_->values(unsigned(index));
            ImGui::Text("Zone %u / entrance %u", record.zone, record.event);
            ImGui::BeginDisabled(!project_store() || loading || drag_axis_ >= 0);
            auto attempt = [&](auto action) {
                try {
                    action();
                } catch (const std::exception &e) {
                    message_ = e.what();
                }
            };
            try {
                auto changed = [&](bool update) {
                    if (update)
                        attempt([&] {
                            document_->set(unsigned(index), v);
                            synchronize();
                            message_.clear();
                        });
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        document_->commit();
                };
                changed(ImGui::DragFloat3("Trigger position", v.position.data(), 1));
                ImGui::TextWrapped("Arrival here is used when another entrance leads to this one.");
                changed(ImGui::DragFloat3("Arrival here", v.arrival.data(), 1));
                float yaw =
                    std::atan2(
                        2 * (v.rotation[0] * v.rotation[2] + v.rotation[1] * v.rotation[3]),
                        1 - 2 * (v.rotation[0] * v.rotation[0] + v.rotation[1] * v.rotation[1])) *
                    57.295779513f;
                bool turn = ImGui::DragFloat("Facing (degrees)", &yaw, .5f);
                if (turn) {
                    float radians = yaw * .01745329252f;
                    v.rotation = {0, std::sin(radians / 2), 0, std::cos(radians / 2)};
                }
                changed(turn);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Sets upright entrance facing; it does not rotate the trigger shape.");
                ImGui::SeparatorText("Transition");
                const char *activation[] = {"Push / walk against", "Interact", "Enter region"};
                auto label = [&](unsigned type) {
                    return type < behaviors_.size() ? behaviors_[type].name + " / " +
                                                          activation[behaviors_[type].activation]
                                                    : "Unknown type " + std::to_string(type);
                };
                ImGui::BeginDisabled(behaviors_.empty());
                if (ImGui::BeginCombo("Transition type", label(v.transition_type).c_str())) {
                    for (unsigned type = 0; type < behaviors_.size(); ++type)
                        if (ImGui::Selectable(label(type).c_str(), v.transition_type == type))
                            attempt([&] {
                                v.transition_type = type;
                                document_->set(unsigned(index), v);
                                document_->commit();
                                synchronize();
                                message_.clear();
                            });
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                if (v.transition_type < behaviors_.size()) {
                    const auto &behavior = behaviors_[v.transition_type];
                    ImGui::Text("Activation: %s", activation[behavior.activation]);
                    ImGui::TextDisabled("Arrival: %s", behavior.center_arrival
                                                           ? "use entrance center"
                                                           : "preserve relative entry position");
                }
                if (!behavior_error_.empty())
                    ImGui::TextWrapped("Transition choices unavailable: %s",
                                       behavior_error_.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Selects behavior for this entrance. Door scenery and animations are "
                        "separate. C variants retain their game-defined behavior.");
                ImGui::SeparatorText("Destination");
                ImGui::Text("Zone %u / entrance %u", v.destination_zone, v.destination_event);
                if (studio::TutorialWidgets::Button("warp_editor", destinations_.empty()
                                                                       ? "Load destination list"
                                                                       : "Refresh destinations")) {
                    refresh_destinations();
                    message_ = std::to_string(destinations_.size()) + " entrance records loaded.";
                }
                if (!destinations_.empty()) {
                    ImGui::InputTextWithHint("##destination-filter",
                                             "Filter map name, zone or entrance", search_,
                                             sizeof(search_));
                    if (ImGui::BeginCombo("Destination entrance", "Choose existing entrance")) {
                        for (auto &d : destinations_)
                            if (!search_[0] || d.label.find(search_) != std::string::npos) {
                                ImGui::PushID(int(&d - destinations_.data()));
                                if (ImGui::Selectable(d.label.c_str(),
                                                      d.zone == v.destination_zone &&
                                                          d.event == v.destination_event)) {
                                    attempt([&] {
                                        v.destination_zone = d.zone;
                                        v.destination_event = d.event;
                                        document_->set(unsigned(index), v);
                                        document_->commit();
                                        synchronize();
                                        message_ =
                                            "Destination changed. The return link is independent.";
                                    });
                                }
                                ImGui::PopID();
                            }
                        ImGui::EndCombo();
                    }
                    if (studio::TutorialWidgets::Button("warp_editor", "Validate destinations")) {
                        document_->validate_destinations(destinations_);
                        message_ = "All entrance destinations in this area resolve uniquely.";
                    }
                }
                if (studio::TutorialWidgets::Button("warp_editor", "Open destination")) {
                    renderer_.spatial.destination_zone = int(v.destination_zone);
                    renderer_.spatial.destination_event = v.destination_event;
                }
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("warp_editor", "Check return link")) {
                    if (destinations_.empty())
                        refresh_destinations();
                    auto destination = std::find_if(
                        destinations_.begin(), destinations_.end(), [&](const auto &d) {
                            return d.zone == v.destination_zone && d.event == v.destination_event;
                        });
                    require(destination != destinations_.end(),
                            "The destination entrance could not be found");
                    Archive field(
                        scene_->archive_sources.resolve(dump_, GameProfile::field_archive(dump_)));
                    WarpDocument target(
                        destination->area,
                        field.decoded(destination->area * TargetProfile::area_stride +
                                      TargetProfile::placement_slot));
                    if (destination->area == area_)
                        target = *document_;
                    else if (auto project = project_store()) {
                        auto patch =
                            project->document("warps/" + std::to_string(destination->area));
                        if (!patch.empty())
                            target.restore(text(read_file(patch)));
                    }
                    auto entrance = std::find_if(
                        target.records().begin(), target.records().end(), [&](const auto &r) {
                            return r.zone == v.destination_zone && r.event == v.destination_event;
                        });
                    require(entrance != target.records().end(),
                            "The destination entrance no longer exists");
                    const auto returning =
                        target.values(unsigned(entrance - target.records().begin()));
                    message_ = returning.destination_zone == record.zone &&
                                       returning.destination_event == record.event
                                   ? "The destination returns to this entrance. Check both arrival "
                                     "positions before staging."
                                   : "The destination returns to zone " +
                                         std::to_string(returning.destination_zone) +
                                         ", entrance " +
                                         std::to_string(returning.destination_event) +
                                         ". Open destination to edit its return link.";
                }
                if (studio::TutorialWidgets::CollapsingHeader("warp_editor",
                                                              "Trigger dimensions and endpoints")) {
                    for (unsigned k = 0; k < record.shapes.size(); ++k) {
                        auto s = document_->records()[index].shapes[k];
                        ImGui::PushID(int(k));
                        const char *shape_names[] = {"Cylinder", "Box", "Line", "Triangle"};
                        int type = int(s.type);
                        if (ImGui::Combo("Shape type", &type, shape_names, 4))
                            attempt([&] {
                                document_->change_shape_type(unsigned(index), k, unsigned(type));
                                synchronize();
                                record = document_->records()[index];
                                s = record.shapes[k];
                                message_ = "Shape converted at its anchor with default dimensions. "
                                           "Adjust it below or Undo to restore.";
                            });
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Replaces this shape with default dimensions at its "
                                              "existing anchor. Other shapes are retained.");
                        const char *cylinder[] = {"Center X", "Center Y", "Center Z", "Radius",
                                                  "Height"};
                        const char *line[] = {"Start X", "Start Y", "Start Z", "End X",
                                              "End Y",   "End Z",   "Height"};
                        const char *box[] = {"Center X", "Center Y", "Center Z", "",       "",
                                             "",         "",         "Width",    "Height", "Depth"};
                        const char *triangle[] = {"A X", "A Y", "A Z", "B X", "B Y",
                                                  "B Z", "C X", "C Y", "C Z"};
                        auto labels = s.type == 0   ? cylinder
                                      : s.type == 1 ? box
                                      : s.type == 2 ? line
                                                    : triangle;
                        unsigned count = s.type == 0 ? 5 : s.type == 1 ? 10 : s.type == 2 ? 7 : 9;
                        for (unsigned c = 0; c < count; ++c) {
                            if (s.type == 1 && c >= 3 && c < 7)
                                continue;
                            float factor = s.type == 1 && (c == 7 || c == 9) ? 2.f : 1.f;
                            auto value = document_->shape_value(s, c) * factor;
                            if (ImGui::DragFloat(labels[c], &value, .5f))
                                attempt([&] {
                                    document_->set_shape(unsigned(index), k, c, value / factor);
                                    synchronize();
                                    message_.clear();
                                });
                            if (ImGui::IsItemDeactivatedAfterEdit())
                                document_->commit();
                        }
                        ImGui::PopID();
                    }
                }
            } catch (const std::exception &e) {
                message_ = e.what();
            }
            ImGui::EndDisabled();
            if (ImGui::CollapsingHeader("Transition behavior"))
                ImGui::TextWrapped(
                    "Existing IDs, camera links and activation conditions are "
                    "preserved. Transition type selects existing shared behavior; it does not "
                    "change the shared table. New entrances inherit the template behavior. Stage "
                    "and reload a "
                    "new destination in another map before linking to it. This preview does not "
                    "execute map transitions.");
        }
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
    }
    ImGui::End();
}
bool WarpEditor::gizmo(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
                       bool hovered) {
    int index = selected_record();
    auto cancel = [&] {
        if (document_) {
            document_->cancel();
            synchronize();
        }
        drag_axis_ = -1;
        blocked_ = true;
    };
    auto &io = ImGui::GetIO();
    if (!open_ || !document_ || loading_ || !project_store() || renderer_.player.active ||
        index < 0 || !renderer_.spatial.enabled[unsigned(SpatialKind::Entrance)] ||
        renderer_.spatial.locked[unsigned(SpatialKind::Entrance)]) {
        if (drag_axis_ >= 0)
            cancel();
        return false;
    }
    if (blocked_) {
        blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    if (drag_axis_ >= 0 && index != drag_record_) {
        cancel();
        return true;
    }
    if (hovered && drag_axis_ < 0 && !io.WantTextInput && !io.KeyCtrl &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (ImGui::IsKeyPressed(ImGuiKey_G))
            mode_ = mode_ >= 3 ? 3 : mode_ == 1 ? 1 : 0;
        if (ImGui::IsKeyPressed(ImGuiKey_R) && (mode_ == 0 || mode_ == 2))
            mode_ = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_S) && mode_ >= 3)
            mode_ = 4;
    }
    auto values = document_->values(unsigned(index));
    auto &record = document_->records()[index];
    shape_ = std::clamp(shape_, 0, std::max(0, int(record.shapes.size()) - 1));
    WarpShape shape;
    if (mode_ >= 3) {
        if (record.shapes.empty())
            return false;
        shape = record.shapes[shape_];
        if (shape.type > 2)
            return false;
    }
    auto point = mode_ == 1 ? values.arrival : values.position;
    unsigned component = mode_ == 3 && shape.type == 2 && endpoint_ ? 3u : 0u;
    if (mode_ >= 3)
        for (unsigned k = 0; k < 3; ++k)
            point[k] = document_->shape_value(shape, component + k);
    if (drag_axis_ >= 0)
        point = drag_point_;
    auto project = [&](SpatialPoint p, ImVec2 &out) {
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
    ImVec2 center;
    if (!project(point, center)) {
        if (drag_axis_ >= 0)
            cancel();
        return false;
    }
    float depth = -(view[2] * point[0] + view[6] * point[1] + view[10] * point[2] + view[14]);
    float length = drag_axis_ >= 0 ? handle_length_ : std::clamp(depth * .12f, 20.f, 2000.f);
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    auto distance = [](ImVec2 p, ImVec2 a, ImVec2 b) {
        float x = b.x - a.x, y = b.y - a.y,
              t = std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / std::max(.001f, x * x + y * y),
                             0.f, 1.f);
        return std::hypot(p.x - a.x - t * x, p.y - a.y - t * y);
    };
    ImVec2 trigger, arrival;
    if (project(values.position, trigger) && project(values.arrival, arrival)) {
        draw->AddLine(trigger, arrival, IM_COL32(255, 210, 90, 190), 1);
        draw->AddCircle(arrival, 7, IM_COL32(255, 210, 90, 255), 0, 2);
        draw->AddText({arrival.x + 9, arrival.y + 5}, IM_COL32(255, 210, 90, 255), "Arrival here");
        draw->AddText({trigger.x + 9, trigger.y - 18}, IM_COL32(120, 220, 255, 255), "Trigger");
    }
    ImVec2 ends[3]{};
    bool visible[3]{};
    int hot = -1;
    float nearest = 9, angle = 0;
    const ImU32 colors[] = {IM_COL32(245, 85, 80, 255), IM_COL32(90, 230, 115, 255),
                            IM_COL32(80, 160, 255, 255)};
    for (unsigned k = 0; k < 3; ++k) {
        auto p = point;
        SpatialPoint axis{};
        axis[k] = length;
        if (mode_ == 4 && shape.type == 1) {
            float q[4];
            float norm = 0;
            for (unsigned n = 0; n < 4; ++n) {
                q[n] = document_->shape_value(shape, n + 3);
                norm += q[n] * q[n];
            }
            if (norm > .000001f) {
                for (auto &v : q)
                    v /= std::sqrt(norm);
                SpatialPoint t{2 * (q[1] * axis[2] - q[2] * axis[1]),
                               2 * (q[2] * axis[0] - q[0] * axis[2]),
                               2 * (q[0] * axis[1] - q[1] * axis[0])};
                axis = {axis[0] + q[3] * t[0] + q[1] * t[2] - q[2] * t[1],
                        axis[1] + q[3] * t[1] + q[2] * t[0] - q[0] * t[2],
                        axis[2] + q[3] * t[2] + q[0] * t[1] - q[1] * t[0]};
            }
        }
        for (unsigned n = 0; n < 3; ++n)
            p[n] += axis[n];
        visible[k] = project(p, ends[k]);
    }
    if (mode_ == 2) {
        if (visible[0] && visible[2]) {
            auto x = ImVec2(ends[0].x - center.x, ends[0].y - center.y),
                 z = ImVec2(ends[2].x - center.x, ends[2].y - center.y);
            float det = x.x * z.y - x.y * z.x;
            if (std::abs(det) > 1) {
                float mx = io.MousePos.x - center.x, my = io.MousePos.y - center.y;
                angle = std::atan2((mx * z.y - my * z.x) / det, (x.x * my - x.y * mx) / det);
                for (unsigned i = 0; i < 64; ++i) {
                    float a = float(i) * 6.2831853f / 64, b = float(i + 1) * 6.2831853f / 64;
                    ImVec2 p{center.x + x.x * std::sin(a) + z.x * std::cos(a),
                             center.y + x.y * std::sin(a) + z.y * std::cos(a)},
                        q{center.x + x.x * std::sin(b) + z.x * std::cos(b),
                          center.y + x.y * std::sin(b) + z.y * std::cos(b)};
                    draw->AddLine(p, q, drag_axis_ == 3 ? IM_COL32(255, 225, 110, 255) : colors[1],
                                  2);
                    if (distance(io.MousePos, p, q) < nearest)
                        hot = 3;
                }
            }
        }
    } else {
        for (unsigned k = 0; k < 3; ++k) {
            if (!visible[k] ||
                (mode_ == 4 && shape.type != 1 && (k == 2 || (shape.type == 2 && k == 0))))
                continue;
            auto color = drag_axis_ == int(k) ? IM_COL32(255, 225, 110, 255) : colors[k];
            draw->AddLine(center, ends[k], color, 3);
            draw->AddCircleFilled(ends[k], 5, color);
            const char *label = mode_ == 4 ? (shape.type == 1 ? (k == 0   ? "Width"
                                                                 : k == 1 ? "Height"
                                                                          : "Depth")
                                              : k == 0        ? "Radius"
                                                              : "Height")
                                : k == 0   ? "X"
                                : k == 1   ? "Y"
                                           : "Z";
            draw->AddText({ends[k].x + 7, ends[k].y - 7}, colors[k], label);
            auto d = distance(io.MousePos, center, ends[k]);
            if (d < nearest) {
                nearest = d;
                hot = int(k);
            }
        }
    }
    float yaw = std::atan2(
        2 * (values.rotation[0] * values.rotation[2] + values.rotation[1] * values.rotation[3]),
        1 - 2 * (values.rotation[0] * values.rotation[0] +
                 values.rotation[1] * values.rotation[1]));
    auto facing = values.position;
    facing[0] += std::sin(yaw) * length * .8f;
    facing[2] += std::cos(yaw) * length * .8f;
    ImVec2 end;
    if (project(values.position, trigger) && project(facing, end)) {
        draw->AddLine(trigger, end, IM_COL32(255, 220, 110, 255), 2);
        draw->AddText(end, IM_COL32(255, 220, 110, 255), "Facing");
    }
    if (drag_axis_ < 0 && hovered && !io.KeyAlt && hot >= 0 && ImGui::IsMouseClicked(0)) {
        document_->commit();
        drag_axis_ = hot;
        drag_record_ = index;
        drag_values_ = values;
        drag_point_ = point;
        start_mouse_ = io.MousePos;
        handle_length_ = length;
        start_angle_ = angle;
    }
    bool captured = drag_axis_ >= 0;
    if (captured) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || io.AppFocusLost)
            cancel();
        else if (ImGui::IsMouseReleased(0)) {
            document_->commit();
            drag_axis_ = -1;
        } else if (ImGui::IsMouseDown(0))
            try {
                if (mode_ == 2) {
                    float old =
                        std::atan2(2 * (drag_values_.rotation[0] * drag_values_.rotation[2] +
                                        drag_values_.rotation[1] * drag_values_.rotation[3]),
                                   1 - 2 * (drag_values_.rotation[0] * drag_values_.rotation[0] +
                                            drag_values_.rotation[1] * drag_values_.rotation[1]));
                    auto v = drag_values_;
                    float yaw = old + angle - start_angle_;
                    if (io.KeyCtrl)
                        yaw = old + std::round((yaw - old) / .261799388f) * .261799388f;
                    v.rotation = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
                    document_->set(unsigned(index), v);
                } else if (visible[drag_axis_]) {
                    auto e = ends[drag_axis_];
                    float x = e.x - center.x, y = e.y - center.y;
                    float delta = ((io.MousePos.x - start_mouse_.x) * x +
                                   (io.MousePos.y - start_mouse_.y) * y) /
                                  std::max(.001f, x * x + y * y) * length;
                    if (io.KeyCtrl)
                        delta = std::round(delta);
                    if (mode_ < 2) {
                        auto v = drag_values_;
                        (mode_ == 1 ? v.arrival : v.position)[drag_axis_] += delta;
                        document_->set(unsigned(index), v);
                    } else {
                        unsigned c = mode_ == 4 ? (shape.type == 0   ? (drag_axis_ == 0 ? 3u : 4u)
                                                   : shape.type == 1 ? 7u + unsigned(drag_axis_)
                                                                     : 6u)
                                                : component + unsigned(drag_axis_);
                        document_->cancel();
                        auto original_shape = document_->records()[index].shapes[shape_];
                        float old = document_->shape_value(original_shape, c);
                        document_->set_shape(unsigned(index), unsigned(shape_), c,
                                             mode_ == 4 ? std::max(.01f, old + delta)
                                                        : old + delta);
                    }
                }
                synchronize();
                message_.clear();
            } catch (const std::exception &e) {
                message_ = e.what();
            }
    }
    draw->PopClipRect();
    return captured;
}

}
