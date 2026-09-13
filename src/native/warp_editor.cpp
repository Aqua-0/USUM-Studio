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
    document_.reset();
    destinations_.clear();
    message_.clear();
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
    for (auto &r : scene_->spatial.regions)
        if (r.kind == SpatialKind::Entrance && r.overworld) {
            auto it = std::find_if(next.regions.begin(), next.regions.end(), [&](auto &n) {
                return n.kind == r.kind && n.overworld &&
                       n.overworld->local_zone == r.overworld->local_zone &&
                       n.overworld->row == r.overworld->row;
            });
            if (it != next.regions.end())
                r = *it;
        }
    renderer_.spatial.invalidate_geometry();
}
void WarpEditor::draw(bool loading) {
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
    ImGui::Begin("Map editing");
    ImGui::BeginDisabled(!document_ || loading);
    if (studio::TutorialWidgets::Button("warp_editor", "Entrances...", ImVec2(-1, 0)))
        open_ = true;
    ImGui::EndDisabled();
    if (!document_ && !message_.empty())
        ImGui::TextWrapped("Warp editing unavailable: %s", message_.c_str());
    ImGui::End();
    if (!open_)
        return;
    ImGui::SetNextWindowSize({510, 680}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Warp editing", &open_)) {
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
        ImGui::TextWrapped("Move the visible door in Scene and its trigger and arrival here.");
        if (!project_store())
            ImGui::TextWrapped("Open an editor project to save and stage warp edits. Inspection is "
                               "available here without a project.");
        if (index < 0)
            ImGui::TextWrapped("Choose an entrance above or select one in Maps > Spatial.");
        else {
            auto &record = document_->records()[index];
            auto v = document_->values(unsigned(index));
            ImGui::Text("Zone %u / entrance %u", record.zone, record.event);
            ImGui::BeginDisabled(!project_store() || loading);
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
                ImGui::SeparatorText("Destination");
                ImGui::Text("Zone %u / entrance %u", v.destination_zone, v.destination_event);
                if (studio::TutorialWidgets::Button("warp_editor", destinations_.empty()
                                                                       ? "Load destination list"
                                                                       : "Refresh destinations")) {
                    destinations_ = load_warp_destinations(dump_, scene_->archive_sources);
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
                        destinations_ = load_warp_destinations(dump_, scene_->archive_sources);
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
                        auto s = record.shapes[k];
                        ImGui::PushID(int(k));
                        if (s.type == 0 || s.type == 2) {
                            ImGui::Text("%s %u", s.type == 0 ? "Cylinder" : "Line", k + 1);
                            unsigned count = s.type == 0 ? 5 : 7;
                            const char *cylinder[] = {"Center X", "Center Y", "Center Z", "Radius",
                                                      "Height"};
                            const char *line[] = {"Start X", "Start Y", "Start Z", "End X",
                                                  "End Y",   "End Z",   "Height"};
                            for (unsigned c = 0; c < count; ++c) {
                                auto value = document_->shape_value(s, c);
                                if (ImGui::DragFloat(s.type == 0 ? cylinder[c] : line[c], &value,
                                                     .5f)) {
                                    attempt([&] {
                                        document_->set_shape(unsigned(index), k, c, value);
                                        synchronize();
                                        message_.clear();
                                    });
                                }
                                if (ImGui::IsItemDeactivatedAfterEdit())
                                    document_->commit();
                            }
                        } else
                            ImGui::TextWrapped("Move this shape with the entrance. Its dimensions "
                                               "cannot be edited.");
                        ImGui::PopID();
                    }
                }
                ImGui::Separator();
                ImGui::BeginDisabled(!document_->can_undo());
                if (studio::TutorialWidgets::Button("warp_editor", "Undo")) {
                    attempt([&] {
                        document_->undo();
                        synchronize();
                    });
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!document_->can_redo());
                if (studio::TutorialWidgets::Button("warp_editor", "Redo")) {
                    attempt([&] {
                        document_->redo();
                        synchronize();
                    });
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (studio::TutorialWidgets::Button("warp_editor", "Save Project")) {
                    save_editor_project();
                    message_ = "Saved. Stage Project stores changed members; Build game export "
                               "creates installable game files.";
                }
                ImGui::TextUnformatted(document_->dirty()     ? "Unsaved warp edits"
                                       : document_->changed() ? "Warp edits saved in project"
                                                              : "No warp edits");
            } catch (const std::exception &e) {
                message_ = e.what();
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("Transition style, camera links, activation conditions and entrance "
                               "IDs are preserved. No in-game transition is simulated here.");
        }
        if (!message_.empty())
            ImGui::TextWrapped("%s", message_.c_str());
    }
    ImGui::End();
}
}
