#include "native/field_system_inspector.h"
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
        entries_ = inspect_field_systems(scene_->placement_source);
    } catch (const std::exception &e) {
        error_ = e.what();
    }
}
void FieldSystemInspector::draw(ViewportCamera &camera, bool launcher) {
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
            if (map_entry >= 0) {
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
                if (ImGui::Selectable(label.c_str(), selected_ == int(i)))
                    selected_ = int(i);
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
                auto &e = entries_[selected_];
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
                    ImGui::TextWrapped(
                        "Read-only. Values describe loaded records; saved story state "
                        "and AMX actions are not executed.");
                    if (!e.notice.empty())
                        ImGui::TextWrapped("%s", e.notice.c_str());
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
                                            ImGui::PlotLines(
                                                "##curve", curve.data(), int(curve.size()), 0,
                                                "128 stored samples", FLT_MAX, FLT_MAX, {-1, 70});
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
