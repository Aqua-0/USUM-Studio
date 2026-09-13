#include "native/tutorial_widgets.h"
#include "native/map_selector.h"
#include "native/project_binding.h"
#include "field/map_creation.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
namespace studio {
namespace {
std::string label(const MapLocation &map) {
    return map.name + " (" +
           (map.zone < 0 ? std::string{} : "zone " + std::to_string(map.zone) + ", ") + "area " +
           std::to_string(map.area) + ")";
}
std::string lower(std::string value) {
    for (auto &c : value)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
}
void MapSelector::refresh(const std::string &path, const ArchiveSources &archives) {
    requested_ = path;
    requested_archives_ = archives;
    pending_ = true;
    catalog_ = {};
    error_.clear();
    search_[0] = 0;
}
std::optional<MapLocation> MapSelector::draw(int area, int zone) {
    if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = job_.get();
            if (!pending_ && reading_ == requested_ && reading_archives_ == requested_archives_) {
                catalog_ = std::move(result);
                if (auto *project = project_store();
                    project && reading_ == project->source.string())
                    for (auto &[key, edit] : project->edits)
                        if (edit.kind == "field-map-created") {
                            auto plan = MapCreation::parse(text(read_file(project->document(key))));
                            for (auto &location : catalog_.locations)
                                if (location.zone == int(plan.zone) &&
                                    location.area == int(plan.area))
                                    location.name = plan.name;
                        }
            }
        } catch (const std::exception &e) {
            if (!pending_ && reading_ == requested_ && reading_archives_ == requested_archives_)
                error_ = e.what();
        }
    }
    if (pending_ && !job_.valid()) {
        pending_ = false;
        reading_ = requested_;
        reading_archives_ = requested_archives_;
        if (!reading_.empty())
            job_ = std::async(std::launch::async, [path = reading_, archives = reading_archives_] {
                return load_map_catalog(std::filesystem::u8path(path), archives);
            });
    }
    auto selected =
        std::find_if(catalog_.locations.begin(), catalog_.locations.end(), [&](const auto &m) {
            return m.area == area && m.zone == zone;
        });
    if (selected == catalog_.locations.end())
        selected =
            std::find_if(catalog_.locations.begin(), catalog_.locations.end(), [&](const auto &m) {
                return m.area == area;
            });
    std::string preview = selected == catalog_.locations.end()
                              ? "Field area " + std::to_string(area)
                              : label(*selected);
    std::optional<MapLocation> result;
    ImGui::TextUnformatted("Map");
    ImGui::SetNextItemWidth(-1);
    ImGui::BeginDisabled(catalog_.locations.empty());
    bool map_combo_open = ImGui::BeginCombo("##map", preview.c_str(), ImGuiComboFlags_HeightLarge);
    if (!map_combo_open)
        TutorialWidgets::item("map_selector", "Map selection");
    if (map_combo_open) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##map-search", "Search name, zone or area", search_,
                                 sizeof(search_));
        auto query = lower(search_);
        ImGui::BeginChild("map-list", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 12));
        unsigned matches = 0;
        for (auto it = catalog_.locations.begin(); it != catalog_.locations.end(); ++it) {
            auto text = label(*it);
            if (!query.empty() && lower(text).find(query) == std::string::npos)
                continue;
            ++matches;
            if (ImGui::Selectable(text.c_str(), it == selected)) {
                result = *it;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!matches)
            ImGui::TextDisabled("No matching maps");
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (job_.valid() || pending_)
        ImGui::TextDisabled("Reading map names...");
    if (!error_.empty())
        ImGui::TextWrapped("Map list unavailable: %s", error_.c_str());
    if (!catalog_.warning.empty())
        ImGui::TextWrapped("%s", catalog_.warning.c_str());
    if (studio::TutorialWidgets::SmallButton("map_selector", "Refresh map list"))
        refresh(requested_, requested_archives_);
    return result;
}
}
