#include "native/undo_shortcuts.h"
#include "native/zone_editor.h"
#include "field/area.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
namespace studio {
void ZoneEditor::load(const std::filesystem::path &dump) {
    if (document_ && dump_ == dump)
        return;
    project_.unbind();
    document_.reset();
    dump_ = dump;
    error_.clear();
    name_error_.clear();
    names_.clear();
    music_.clear();
    scene_ = nullptr;
    try {
        document_ =
            std::make_unique<ZoneDocument>(Archive(dump / TargetProfile::zone_archive).decoded(0));
        project_.bind(
            "zone_settings", "zone_settings/zones", "Zone behaviors", "",
            [this] {
                return document_ && document_->dirty();
            },
            [this] {
                return project_text(document_->serialize());
            },
            [this] {
                document_->mark_saved();
            });
        if (auto file = project_.document(); !file.empty()) {
            document_->restore(text(read_file(file)));
            project_.restored();
        }
    } catch (const std::exception &e) {
        error_ = e.what();
        document_.reset();
    }
    try {
        names_ = decode_location_text(Archive(dump / TargetProfile::location_text_archive)
                                          .decoded(TargetProfile::location_text_member));
    } catch (const std::exception &e) {
        name_error_ = e.what();
    }
    try {
        music_ = read_music_catalog(dump);
    } catch (const std::exception &) {
    }
}
std::string ZoneEditor::zone_label(unsigned zone) const {
    auto id = unsigned(document_->value(zone, 0));
    return (id < names_.size() && !names_[id].empty() ? names_[id] : "Unnamed location") +
           " (zone " + std::to_string(zone) + ")";
}
bool ZoneEditor::draw(const Environment *scene, int loaded_zone, bool launcher, bool loading) {
    bool weather = false;
    if (scene != scene_) {
        scene_ = scene;
        if (document_ && loaded_zone >= 0 && unsigned(loaded_zone) < document_->size())
            zone_ = unsigned(loaded_zone);
        else if (document_ && scene && !scene->locations.empty() &&
                 scene->locations.front().zone >= 0)
            zone_ = unsigned(scene->locations.front().zone);
    }
    if (launcher) {
        ImGui::Begin("Map inspector");
        ImGui::BeginDisabled(loading || !document_);
        if (ImGui::Button("Zone settings...", {-1, 0}))
            open_ = true;
        ImGui::EndDisabled();
        if (!document_ && !error_.empty())
            ImGui::TextWrapped("Zone settings: %s", error_.c_str());
        ImGui::End();
    }
    if (!open_)
        return false;
    ImGui::SetNextWindowSize({620, 660}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Zone settings", &open_)) {
        if (document_) {
            zone_ = std::min(zone_, document_->size() - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##zone", zone_label(zone_).c_str())) {
                for (unsigned z = 0; z < document_->size(); ++z) {
                    bool local = !scene || std::any_of(scene->locations.begin(),
                                                       scene->locations.end(), [z](auto &v) {
                                                           return v.zone == int(z);
                                                       });
                    if ((all_zones_ || local || z == zone_) &&
                        ImGui::Selectable(zone_label(z).c_str(), z == zone_))
                        zone_ = z;
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("Show all project zones", &all_zones_);
            ImGui::TextWrapped(
                "These are saved game settings. Stage and reload to refresh map data; gameplay "
                "permissions and transition effects are not simulated here.");
            ImGui::BeginDisabled(!project_store() || loading);
            auto attempt = [&](auto fn) {
                try {
                    fn();
                    error_.clear();
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            };
            ImGui::BeginDisabled(!document_->can_undo());
            if (studio::UndoShortcuts::button("zone_editor", "Undo"))
                document_->undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!document_->can_redo());
            if (studio::UndoShortcuts::button("zone_editor", "Redo"))
                document_->redo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Save Project"))
                attempt([&] {
                    save_editor_project();
                });
            ImGui::SameLine();
            if (ImGui::Button("Reset this zone"))
                document_->reset(zone_);
            ImGui::TextDisabled("%s", document_->dirty() ? "Unsaved zone settings"
                                                         : "No unsaved zone settings");
            ImGui::EndDisabled();
            const char *tabs[] = {"General", "Gameplay", "Presentation", "Map display", "Advanced"};
            if (ImGui::BeginTabBar("zone-pages")) {
                for (unsigned tab = 0; tab < 5; ++tab)
                    if (ImGui::BeginTabItem(tabs[tab])) {
                        if (tab == 2 && ImGui::Button("Open loaded map weather controls"))
                            weather = true;
                        if (tab == 4)
                            ImGui::TextWrapped(
                                "Resource links select existing source IDs. Changing a handler or "
                                "dialogue file does not author its scripts or messages.");
                        ImGui::BeginChild("zone-fields", {0, -45}, false);
                        ImGui::BeginDisabled(!project_store() || loading);
                        const auto &fields = zone_properties();
                        const char *last = "";
                        for (unsigned i = 0; i < fields.size(); ++i) {
                            const auto &f = fields[i];
                            // Keep legacy keys readable in existing project documents only.
                            std::string_view key = f.key;
                            if (key == "flash" || key == "cold_breath" || key == "bloom" ||
                                key == "stereo")
                                continue;
                            std::string_view group = f.group;
                            unsigned page = group == "Identity" || group == "Start position" ? 0
                                            : group == "Gameplay"                            ? 1
                                            : group == "Presentation" || group == "Display flags"
                                                ? 2
                                            : group == "Map display" ? 3
                                                                     : 4;
                            if (page != tab)
                                continue;
                            if (group != last) {
                                ImGui::SeparatorText(f.group);
                                last = f.group;
                            }
                            ImGui::PushID(int(i));
                            auto v = document_->value(zone_, i);
                            auto set = [&](std::int64_t value) {
                                attempt([&] {
                                    auto candidate = *document_;
                                    candidate.set(zone_, i, value);
                                    candidate.validate_resources(dump_);
                                    document_->set(zone_, i, value);
                                });
                            };
                            if (f.bit >= 0) {
                                bool enabled = v != 0;
                                if (ImGui::Checkbox(f.label, &enabled))
                                    set(enabled ? 1 : 0);
                            } else if (std::string_view(f.key) == "location_name") {
                                ImGui::TextUnformatted(f.label);
                                ImGui::SetNextItemWidth(-1);
                                auto label = [&](unsigned id) {
                                    return (id < names_.size() ? names_[id] : "Unknown location") +
                                           " / " + std::to_string(id);
                                };
                                if (ImGui::BeginCombo("##name", label(unsigned(v)).c_str())) {
                                    ImGui::InputTextWithHint("##find-name", "Find location name",
                                                             search_, sizeof(search_));
                                    for (unsigned id = 0; id < names_.size(); ++id)
                                        if ((!search_[0] ||
                                             names_[id].find(search_) != std::string::npos) &&
                                            ImGui::Selectable(label(id).c_str(), v == id))
                                            set(id);
                                    ImGui::EndCombo();
                                }
                                ImGui::TextWrapped("Selects an existing place label; it does not "
                                                   "rename shared text or change dialogue.");
                                if (!name_error_.empty())
                                    ImGui::TextWrapped("%s", name_error_.c_str());
                            } else if (std::string_view(f.key) == "transition_category") {
                                const char *types[] = {"Interior", "Exterior", "Public facility",
                                                       "Shop", "Dungeon"};
                                if (ImGui::BeginCombo(
                                        f.label, v >= 0 && v < 5 ? types[v] : "Unknown category")) {
                                    for (unsigned t = 0; t < 5; ++t)
                                        if (ImGui::Selectable(types[t], v == t))
                                            set(t);
                                    ImGui::EndCombo();
                                }
                                ImGui::TextWrapped(
                                    "Both zones' categories choose the fade shape and duration. "
                                    "This does not enable other dungeon or shop behavior.");
                            } else if (f.minimum < 0) {
                                if (ImGui::InputScalar(f.label, ImGuiDataType_S64, &v, nullptr,
                                                       nullptr, "%lld",
                                                       ImGuiInputTextFlags_EnterReturnsTrue))
                                    set(v);
                            } else {
                                ImGui::TextUnformatted(f.label);
                                auto label = [&](std::int64_t id) {
                                    std::string result = std::to_string(id);
                                    if (std::string_view(f.key).starts_with("music_")) {
                                        if (id == 0)
                                            result += " / No music";
                                        else
                                            try {
                                                auto it =
                                                    music_.find(map_music_sound(unsigned(id)));
                                                if (it != music_.end())
                                                    result += " / " + it->second.file;
                                            } catch (const std::exception &) {
                                                result += " / Unresolved music reference";
                                            }
                                    }
                                    return result;
                                };
                                ImGui::SetNextItemWidth(-1);
                                if (ImGui::BeginCombo("##resource", label(v).c_str())) {
                                    for (auto id : document_->source_values(i))
                                        if (ImGui::Selectable(label(id).c_str(), v == id))
                                            set(id);
                                    ImGui::EndCombo();
                                }
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip(
                                        "IDs already used by zones in this project's source. "
                                        "References must suit the selected map's resources.");
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndDisabled();
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                ImGui::EndTabBar();
            }
            if (!project_store())
                ImGui::TextWrapped("Open a project to edit zone settings.");
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
    }
    ImGui::End();
    return weather;
}
}
