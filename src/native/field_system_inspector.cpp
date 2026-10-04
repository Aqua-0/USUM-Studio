#include "native/tutorial_widgets.h"
#include "native/field_system_inspector.h"
#include "field/overworld_document.h"
#include "native/inspector_selector.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <cfloat>
#include <cmath>
namespace studio {
namespace {
std::string lowercase(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}
void FieldSystemInspector::set_scene(std::shared_ptr<Environment> scene,
                                     const std::filesystem::path &dump, unsigned area) {
    activity_binding_.unbind();
    activities_.reset();
    activity_selection_ = -1;
    activity_pending_ = false;
    audio_.reset();
    dump_ = dump;
    audio_entry_ = -1;
    scene_ = std::move(scene);
    pedestrians.set_scene(scene_, area, dump);
    entries_.clear();
    error_.clear();
    selected_ = -1;
    if (!scene_)
        return;
    try {
        activities_ = std::make_unique<FieldActivityDocument>(scene_->placement_source);
        activity_binding_.bind(
            "field-activities", "field-activities/" + std::to_string(area),
            "Field activities in area " + std::to_string(area), std::to_string(area),
            [this] {
                return activities_ && (activities_->dirty() || activity_pending_);
            },
            [this] {
                return project_text(activities_->serialize());
            },
            [this] {
                activities_->mark_saved();
            });
        activity_binding_.ready([this] {
            if (activity_pending_)
                open_ = true;
            require(!activity_pending_,
                    "Apply or discard the field activity settings before saving");
        });
        activity_binding_.autosave_when([this] {
            return !activity_pending_;
        });
        if (auto path = activity_binding_.document(); !path.empty()) {
            activities_->restore(text(read_file(path)));
            activity_binding_.restored();
        }
        refresh_activities();
    } catch (const std::exception &e) {
        error_ = e.what();
        activities_.reset();
        activity_binding_.unbind();
    }
}
void FieldSystemInspector::draw(ViewportCamera &camera, bool launcher) {
    if (scene_ && pedestrian_revision_ != pedestrians.revision) {
        auto current = selected_ >= 0 && std::size_t(selected_) < entries_.size()
                           ? std::optional(entries_[selected_])
                           : std::nullopt;
        std::erase_if(entries_, [](auto &e) {
            return e.category == 9;
        });
        auto compiled = pedestrians.compiled();
        if (!compiled.empty())
            for (auto &entry : inspect_field_systems(compiled))
                if (entry.category == 9)
                    entries_.push_back(std::move(entry));
        if (current)
            for (unsigned i = 0; i < entries_.size(); ++i)
                if (entries_[i].category == current->category &&
                    entries_[i].local_zone == current->local_zone &&
                    entries_[i].row == current->row)
                    selected_ = int(i);
        if (current && current->category == 9)
            if (auto route = pedestrians.selected_route())
                for (unsigned i = 0; i < entries_.size(); ++i)
                    if (entries_[i].category == 9 && entries_[i].local_zone == route->first &&
                        entries_[i].row == route->second)
                        selected_ = int(i);
        pedestrian_revision_ = pedestrians.revision;
    }
    int map_entry = -1;
    if (scene_ && renderer_.spatial.selected >= 0 &&
        std::size_t(renderer_.spatial.selected) < scene_->spatial.regions.size()) {
        auto &region = scene_->spatial.regions[renderer_.spatial.selected];
        if (region.overworld)
            for (unsigned i = 0; i < entries_.size(); ++i) {
                auto &e = entries_[i];
                auto &r = *region.overworld;
                if (e.category == r.category && e.local_zone == r.local_zone && e.row == r.row) {
                    map_entry = int(i);
                    break;
                }
            }
    }
    if (launcher) {
        ImGui::Begin("Map inspector");
        ImGui::BeginDisabled(!scene_);
        if (ImGui::Button(map_entry >= 0 ? "Inspect field behavior..." : "Browse field systems...",
                          {-1, 0})) {
            open_ = true;
            if (map_entry >= 0 && !activity_pending_) {
                selected_ = map_entry;
                category_ = int(entries_[selected_].category);
                search_[0] = 0;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Routes, fishing, berries, field actions, contact Pokemon, puzzles "
                              "and ambient sounds.");
        ImGui::EndDisabled();
        ImGui::End();
    }
    if (!open_)
        return;
    ImGui::SetNextWindowSize({950, 680}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Field systems", &open_)) {
        ImGui::TextDisabled("Field behavior | %zu records", entries_.size());
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        if (ImGui::BeginTable("Field system layout", 2, ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Records", ImGuiTableColumnFlags_WidthFixed, 280);
            ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::map<unsigned, std::pair<std::string, unsigned>> categories;
            for (auto &e : entries_) {
                auto &item = categories[e.category];
                item.first = e.category == 1   ? "NPCs"
                             : e.category == 7 ? "Trainers"
                                               : spatial_kind_name(e.region.kind);
                ++item.second;
            }
            const char *preview = category_ < 0 ? "All field systems"
                                  : categories.contains(unsigned(category_))
                                      ? categories[unsigned(category_)].first.c_str()
                                      : "Category not in this map";
            InspectorSelectorStyle selector("System");
            bool expanded = ImGui::BeginCombo("##field-system", preview);
            selector.end();
            if (expanded) {
                if (ImGui::Selectable("All field systems", category_ < 0))
                    category_ = -1;
                for (auto &[key, item] : categories) {
                    auto label = item.first + " (" + std::to_string(item.second) + ")";
                    if (ImGui::Selectable(label.c_str(), category_ == int(key)))
                        category_ = int(key);
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##field-search", "Search records or settings", search_,
                                     sizeof(search_));
            auto needle = lowercase(search_);
            ImGui::BeginChild("Field records", {0, 0});
            for (unsigned i = 0; i < entries_.size(); ++i) {
                auto &e = entries_[i];
                if (category_ >= 0 && unsigned(category_) != e.category)
                    continue;
                bool match = needle.empty() || lowercase(e.name).find(needle) != std::string::npos;
                if (!match)
                    for (auto &p : e.properties)
                        if (lowercase(p.name + " " + p.value).find(needle) != std::string::npos) {
                            match = true;
                            break;
                        }
                if (!match)
                    continue;
                ImGui::PushID(int(i));
                auto label =
                    e.name + (e.category == 16 ? " / area"
                                               : " / local zone " + std::to_string(e.local_zone));
                ImGui::BeginDisabled(activity_pending_ && selected_ != int(i));
                if (ImGui::Selectable(label.c_str(), selected_ == int(i)))
                    selected_ = int(i);
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("Field details", {0, 0});
            if (selected_ >= 0 && std::size_t(selected_) < entries_.size()) {
                if (audio_entry_ != selected_) {
                    audio_.reset();
                    audio_entry_ = selected_;
                }
                auto e = entries_[selected_];
                if (e.category == 9)
                    pedestrians.select(e.local_zone, e.row);
                ImGui::TextUnformatted(e.name.c_str());
                if (ImGui::Button("Frame")) {
                    SpatialPoint low{INFINITY, INFINITY, INFINITY},
                        high{-INFINITY, -INFINITY, -INFINITY};
                    auto geometry = e.category == 9 ? pedestrians.route_geometry() : e.region;
                    for (auto &v : geometry.vertices)
                        for (unsigned k = 0; k < 3; ++k) {
                            low[k] = std::min(low[k], v.position[k]);
                            high[k] = std::max(high[k], v.position[k]);
                        }
                    if (!geometry.vertices.empty()) {
                        camera.fit(low, high);
                        camera.bounds_low = scene_->low;
                        camera.bounds_high = scene_->high;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Show on map")) {
                    renderer_.spatial.enabled[unsigned(e.region.kind)] = true;
                    renderer_.spatial.pick_overlays = true;
                    for (unsigned i = 0; i < scene_->spatial.regions.size(); ++i) {
                        auto &r = scene_->spatial.regions[i];
                        if (r.overworld && r.overworld->category == e.category &&
                            r.overworld->local_zone == e.local_zone && r.overworld->row == e.row) {
                            renderer_.spatial.selected = int(i);
                            break;
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Copy details")) {
                    std::string copy = e.name + "\n";
                    for (auto &p : e.properties)
                        copy += p.group + " / " + p.name + ": " + p.value + "\n";
                    if (e.category == 9)
                        copy = pedestrians.copy_details();
                    ImGui::SetClipboardText(copy.c_str());
                }
                if (e.category == 9) {
                    pedestrians.select(e.local_zone, e.row);
                    pedestrians.draw(camera);
                    if (ImGui::CollapsingHeader("Route conditions (read-only)"))
                        for (auto &p : e.properties)
                            if (p.group == "Conditions")
                                ImGui::TextWrapped("%s: %s", p.name.c_str(), p.value.c_str());
                } else {
                    bool editable = e.category == 11 || e.category == 12 || e.category == 14;
                    if (editable)
                        activity_controls(e);
                    else
                        ImGui::TextWrapped(
                            "Read-only. Saved story state and AMX actions are not executed.");
                    if (!e.notice.empty())
                        ImGui::TextWrapped("%s", e.notice.c_str());
                    if (!editable || ImGui::CollapsingHeader("Stored activity details")) {
                        std::vector<std::string> groups;
                        for (auto &p : e.properties)
                            if (std::find(groups.begin(), groups.end(), p.group) == groups.end())
                                groups.push_back(p.group);
                        for (auto &group : groups)
                            if (ImGui::CollapsingHeader(
                                    group.c_str(), group == "Related data" || group == "Movement" ||
                                                           group == "Spawning"
                                                       ? ImGuiTreeNodeFlags_DefaultOpen
                                                       : 0)) {
                                ImGui::PushID(group.c_str());
                                for (auto &p : e.properties)
                                    if (p.group == group) {
                                        ImGui::TextDisabled("%s", p.name.c_str());
                                        if (e.category == 16 && p.name == "Sound ID") {
                                            ImGui::TextWrapped("%s", p.value.c_str());
                                            audio_.draw(dump_, unsigned(std::stoul(p.value)));
                                            continue;
                                        }
                                        if (group.starts_with("Sound parameters") &&
                                            (p.name == "Volume" || p.name == "Pitch" ||
                                             p.name == "Low-pass" || p.name == "High-pass")) {
                                            std::istringstream values(p.value);
                                            std::vector<float> curve;
                                            float value;
                                            while (values >> value) {
                                                curve.push_back(value);
                                                if (values.peek() == ',')
                                                    values.get();
                                            }
                                            ImGui::PushID(p.name.c_str());
                                            if (!curve.empty())
                                                ImGui::PlotLines("##curve", curve.data(),
                                                                 int(curve.size()), 0,
                                                                 "128 stored samples", FLT_MAX,
                                                                 FLT_MAX, {-1, 70});
                                            if (ImGui::TreeNode("Stored values")) {
                                                ImGui::TextWrapped("%s", p.value.c_str());
                                                ImGui::TreePop();
                                            }
                                            ImGui::PopID();
                                        } else
                                            ImGui::TextWrapped("%s", p.value.c_str());
                                    }
                                ImGui::PopID();
                            }
                    }
                }
                if (ImGui::CollapsingHeader("Source record"))
                    ImGui::Text("Category %u / local group %u / row %u", e.category, e.local_zone,
                                e.row);
            } else
                ImGui::TextWrapped(
                    "Choose a field system on the left to inspect its settings and related data.");
            ImGui::EndChild();
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
}
namespace studio {
void FieldSystemInspector::refresh_activities() {
    if (!activities_ || !scene_)
        return;
    entries_ = inspect_field_systems(activities_->bytes());
    pedestrian_revision_ = pedestrians.revision - 1;
    std::map<unsigned, int> zones;
    for (auto &r : scene_->spatial.regions)
        if (r.overworld && r.zone >= 0)
            zones[r.overworld->local_zone] = r.zone;
    std::erase_if(scene_->spatial.regions, [](auto &r) {
        return r.kind == SpatialKind::Berry || r.kind == SpatialKind::Fishing ||
               r.kind == SpatialKind::Contact;
    });
    SpatialScene decoded;
    decode_field_system_regions(decoded, activities_->bytes(), zones);
    for (auto &r : decoded.regions)
        if (r.kind == SpatialKind::Berry || r.kind == SpatialKind::Fishing ||
            r.kind == SpatialKind::Contact)
            scene_->spatial.regions.push_back(std::move(r));
    renderer_.spatial.selected = -1;
    renderer_.spatial.invalidate_geometry();
    renderer_.invalidate_selection_readback();
    activity_selection_ = -1;
}
void FieldSystemInspector::activity_controls(const FieldSystemEntry &e) {
    if (!activities_)
        return;
    if (activity_selection_ != selected_) {
        activity_draft_ = activities_->record(e.category, e.local_zone, e.row);
        activity_selection_ = selected_;
        activity_pending_ = false;
    }
    ImGui::BeginDisabled(!project_store());
    SpatialPoint position{};
    for (unsigned i = 0; i < 3; ++i)
        position[i] = f32(activity_draft_, 4 + i * 4);
    if (ImGui::InputFloat3("Activity position", position.data())) {
        for (unsigned i = 0; i < 3; ++i)
            put_float(activity_draft_, 4 + i * 4, position[i]);
        activity_pending_ = true;
    }
    if (TutorialWidgets::CollapsingHeader("field_activities", "Activity settings")) {
        for (auto &f : field_activity_settings(e.category)) {
            unsigned value = f.width == 1   ? activity_draft_[f.offset]
                             : f.width == 2 ? u16(activity_draft_, f.offset)
                                            : u32(activity_draft_, f.offset);
            if (f.name == "Event ID") {
                ImGui::Text("Event ID: %u", value);
                continue;
            }
            if (ImGui::InputScalar(f.name.c_str(), ImGuiDataType_U32, &value)) {
                if (f.width == 1)
                    activity_draft_[f.offset] = std::uint8_t(std::min(value, 255u));
                else if (f.width == 2)
                    put16(activity_draft_, f.offset, std::uint16_t(std::min(value, 65535u)));
                else
                    put32(activity_draft_, f.offset, value);
                activity_pending_ = true;
            }
        }
    }
    if (e.category == 11)
        ImGui::TextWrapped("Persistent pile IDs share saved depletion state. A copied pile retains "
                           "its source ID until you change it.");
    if (e.category == 14)
        ImGui::TextWrapped(
            "Copying retains character, script, NPC and saved-state references. Overlays update "
            "immediately; character models refresh when you stage and reload the map.");
    auto action = [&](auto operation) {
        try {
            operation();
            activity_pending_ = false;
            refresh_activities();
            error_.clear();
        } catch (const std::exception &error) {
            error_ = error.what();
        }
    };
    ImGui::BeginDisabled(!activity_pending_);
    if (TutorialWidgets::Button("field_activities", "Apply activity settings"))
        action([&] {
            activities_->update(e.category, e.local_zone, e.row, activity_draft_);
        });
    ImGui::SameLine();
    if (TutorialWidgets::Button("field_activities", "Discard activity settings")) {
        activity_selection_ = -1;
        activity_pending_ = false;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(activity_pending_);
    if (ImGui::Button("Save Project"))
        try {
            save_editor_project();
        } catch (const std::exception &error) {
            error_ = error.what();
        }
    ImGui::BeginDisabled(!activities_->can_undo());
    if (TutorialWidgets::Button("field_activities", "Undo activity edit"))
        action([&] {
            activities_->undo();
        });
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!activities_->can_redo());
    if (TutorialWidgets::Button("field_activities", "Redo activity edit"))
        action([&] {
            activities_->redo();
        });
    ImGui::EndDisabled();
    if (TutorialWidgets::Button("field_activities", "Create activity from selected...")) try {
        new_position_ = position;
        new_position_[0] += 100;
        new_event_ = e.region.overworld->event + 1;
        for (auto &entry : entries_)
            if (entry.local_zone == e.local_zone && entry.region.overworld)
                new_event_ = std::max(new_event_, entry.region.overworld->event + 1);
        OverworldDocument placements(0, activities_->bytes(), {}, {});
        for (auto &entry : placements.entries())
            if (entry.zone == e.local_zone)
                new_event_ = std::max(new_event_, entry.event + 1);
        ImGui::OpenPopup("New field activity");
    } catch (const std::exception &error) { error_ = error.what(); }
    if (ImGui::BeginPopup("New field activity")) {
        ImGui::TextWrapped(
            "Copies this record in the same local zone, including its resource, script and "
            "saved-state references. Its range shapes move with the new position.");
        ImGui::InputScalar("New event ID", ImGuiDataType_U32, &new_event_);
        ImGui::InputFloat3("New activity position", new_position_.data());
        if (TutorialWidgets::Button("field_activities", "Create activity"))
            try {
                auto row = activities_->duplicate(e.category, e.local_zone, e.row, new_event_,
                                                  new_position_);
                refresh_activities();
                for (unsigned i = 0; i < entries_.size(); ++i)
                    if (entries_[i].category == e.category &&
                        entries_[i].local_zone == e.local_zone && entries_[i].row == row)
                        selected_ = int(i);
                error_.clear();
                ImGui::CloseCurrentPopup();
            } catch (const std::exception &error) {
                error_ = error.what();
            }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (activity_pending_)
        ImGui::TextDisabled("Unapplied activity settings");
    else if (activities_->dirty())
        ImGui::TextDisabled("Unsaved activity edits");
}
}
