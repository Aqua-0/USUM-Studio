#include "native/related_data_inspector.h"
#include "native/tutorial_widgets.h"
#include "scene/object_relationships.h"
#include <imgui.h>
#include "native/project_binding.h"
#include "field/area.h"
#include <algorithm>
#include <sstream>
namespace studio {
bool related_data_inspector(const Environment &scene, MaterialSelection &selection,
                            EnvironmentRenderer &renderer) {
    if (!TutorialWidgets::CollapsingHeader("related_data", "Related data"))
        return false;
    int selected_draw = selection.draw, selected_region = renderer.spatial.selected;
    if (selected_region < 0)
        selected_region = object_region(scene, selected_draw);
    if (selected_draw < 0)
        selected_draw = object_draw(scene, selected_region);
    ImGui::TextWrapped(
        "Relationships in the loaded area. Shared model edits affect every user of that resource.");
    auto navigate = [&](int draw, int region) {
        selection = {};
        selection.draw = draw;
        if (draw >= 0)
            selection.material = int(scene.draws[draw].material);
        selection.reveal_scene = true;
        renderer.spatial.selected = region;
        if (region >= 0)
            renderer.spatial.enabled[unsigned(scene.spatial.regions[region].kind)] = true;
    };
    if (selected_draw >= 0 && selection.draw < 0 && ImGui::Button("Select linked model")) {
        navigate(selected_draw, selected_region);
        return true;
    }
    if (selected_region >= 0 && std::size_t(selected_region) < scene.spatial.regions.size()) {
        const auto &region = scene.spatial.regions[selected_region];
        if (region.overworld) {
            const auto &ref = *region.overworld;
            ImGui::Text("Event %u / local zone %u", ref.event, ref.local_zone);
            ImGui::Text("Script selector: %u", ref.script);
            if (ref.condition)
                ImGui::Text("Condition: %u / value %u", ref.condition, ref.value);
            if (ref.version)
                ImGui::Text("Version condition: %u", ref.version);
            if (ref.condition || ref.version)
                ImGui::TextDisabled(
                    "Conditions are stored references; preview does not evaluate a save.");
        }
    }
    auto links = object_relationships(scene, selected_draw, selected_region);
    if (links.empty())
        ImGui::TextDisabled("No additional relationships resolved in this area.");
    ImGui::BeginChild("related-objects", {0, links.empty() ? 1.f : 180.f}, ImGuiChildFlags_None);
    bool changed = false;
    for (std::size_t i = 0; i < links.size(); ++i) {
        const auto &link = links[i];
        ImGui::PushID(int(i));
        if (ImGui::Selectable(link.label.c_str())) {
            navigate(link.draw, link.region);
            changed = true;
        }
        std::string reasons;
        auto reason = [&](const char *label) {
            if (!reasons.empty())
                reasons += " / ";
            reasons += label;
        };
        if (link.model)
            reason("Shared model");
        if (link.script)
            reason("Same local script selector");
        if (link.condition)
            reason("Same condition");
        if (link.destination)
            reason("Entrance destination");
        if (link.incoming)
            reason("Entrance leads here");
        ImGui::TextDisabled("%s", reasons.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    return changed;
}
void RelatedDataInspector::draw(const Environment &scene, const std::filesystem::path &source,
                                unsigned area, EnvironmentRenderer &renderer) {
    if (source_ != source || archives_ != scene.archive_sources) {
        if ((doors_loading_.valid() &&
             doors_loading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) ||
            (uses_loading_.valid() &&
             uses_loading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)) {
            ImGui::TextDisabled("Finishing the previous dependency scan...");
            return;
        }
        doors_loading_ = {};
        uses_loading_ = {};
        doors_.clear();
        uses_.clear();
        behaviors_.clear();
        identity_.clear();
        error_.clear();
        source_ = source;
        archives_ = scene.archive_sources;
        doors_scanned_ = uses_scanned_ = false;
    }
    try {
        if (doors_loading_.valid() &&
            doors_loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            doors_ = doors_loading_.get();
            doors_scanned_ = true;
        }
        if (uses_loading_.valid() &&
            uses_loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            uses_ = uses_loading_.get();
            uses_scanned_ = true;
        }
        int selected = renderer.spatial.selected;
        if (selected < 0 || std::size_t(selected) >= scene.spatial.regions.size())
            return;
        const auto &region = scene.spatial.regions[selected];
        if (!region.overworld)
            return;
        const auto &ref = *region.overworld;
        if (!TutorialWidgets::CollapsingHeader("related_data", "Across areas and door behavior"))
            return;
        if (region.kind == SpatialKind::Entrance) {
            if (behaviors_.empty())
                try {
                    behaviors_ = load_entrance_behaviors(source, scene.archive_sources);
                } catch (const std::exception &error) {
                    error_ = error.what();
                }
            if (ref.style < behaviors_.size()) {
                const auto &behavior = behaviors_[ref.style];
                ImGui::TextWrapped("Transition preset: %s", behavior.name.c_str());
                const char *activation[] = {"Push / walk against", "Interact", "Enter region"};
                ImGui::Text("Activation: %s", activation[behavior.activation]);
                if (behavior.sound == 0xffffffffu) ImGui::TextDisabled("Sound: unspecified");
                else ImGui::Text("Sound reference: %u", behavior.sound);
                ImGui::Text("Arrival: %s", behavior.center_arrival ? "center of entrance"
                                                                   : "preserve relative entry position");
            } else
                ImGui::Text("Unresolved transition preset %u", ref.style);
            ImGui::TextWrapped(
                "The entrance controls the transition. A door mesh, its animation and any scripted "
                "choreography are separate dependencies; no mesh link is inferred from proximity.");
            ImGui::BeginDisabled(doors_loading_.valid());
            bool scan_doors = TutorialWidgets::Button("related_data", "Scan entrance connections");
            ImGui::EndDisabled();
            if (scan_doors) {
                auto archives = scene.archive_sources;
                std::vector<std::pair<unsigned, std::string>> patches;
                if (auto *store = project_store())
                    for (const auto &[key, edit] : store->edits)
                        if (edit.kind == "warps") {
                            unsigned patch_area;
                            std::istringstream in(edit.parameters);
                            require(bool(in >> patch_area), "Invalid saved entrance area");
                            patches.emplace_back(patch_area, text(read_file(store->document(key))));
                        }
                doors_loading_ = std::async(std::launch::async, [source, archives, patches] {
                    auto result = load_warp_destinations(source, archives);
                    Archive field(archives.resolve(source, GameProfile::field_archive(source)));
                    for (const auto &[patch_area, patch] : patches) {
                        WarpDocument document(
                            patch_area, field.decoded(patch_area * TargetProfile::area_stride +
                                                      TargetProfile::placement_slot));
                        document.restore(patch);
                        std::erase_if(result, [&](const auto &entry) {
                            return entry.area == patch_area;
                        });
                        for (unsigned i = 0; i < document.records().size(); ++i) {
                            const auto &r = document.records()[i];
                            const auto v = document.values(i);
                            result.push_back({patch_area, r.zone, r.event,
                                              "Zone " + std::to_string(r.zone) + " / entrance " +
                                                  std::to_string(r.event),
                                              std::pair{v.destination_zone, v.destination_event}});
                        }
                    }
                    return result;
                });
                error_.clear();
            }
            if (doors_loading_.valid())
                ImGui::TextDisabled("Scanning entrance connections...");
            if (doors_scanned_) {
                auto entries = doors_;
                std::erase_if(entries, [&](const auto &entry) {
                    return entry.area == area;
                });
                for (const auto &r : scene.spatial.regions)
                    if (r.kind == SpatialKind::Entrance && r.overworld && r.zone >= 0 &&
                        r.overworld->destination_zone >= 0)
                        entries.push_back({area, unsigned(r.zone), r.overworld->event, r.name,
                                           std::pair{unsigned(r.overworld->destination_zone),
                                                     r.overworld->destination_event}});
                unsigned matches = 0;
                for (const auto &entry : entries)
                    if (int(entry.zone) == ref.destination_zone &&
                        entry.event == ref.destination_event)
                        ++matches;
                if (matches != 1)
                    ImGui::Text("Destination matches: %u (expected one)", matches);
                for (std::size_t i = 0; i < entries.size(); ++i) {
                    const auto &entry = entries[i];
                    bool destination = int(entry.zone) == ref.destination_zone &&
                                       entry.event == ref.destination_event;
                    bool incoming = region.zone >= 0 &&
                                    entry.destination ==
                                        std::optional(std::pair{unsigned(region.zone), ref.event});
                    if (!destination && !incoming)
                        continue;
                    ImGui::PushID(int(i));
                    ImGui::TextWrapped("%s: %s (area %u)",
                                       destination ? "Destination" : "Leads here",
                                       entry.label.c_str(), entry.area);
                    if (destination)
                        ImGui::TextDisabled(incoming ? "Return link points here"
                                                     : "Return link goes elsewhere");
                    auto count =
                        std::count_if(entries.begin(), entries.end(), [&](const auto &other) {
                            return other.zone == entry.zone && other.event == entry.event;
                        });
                    ImGui::BeginDisabled(count != 1);
                    if (ImGui::Button("Open entrance")) {
                        renderer.spatial.destination_zone = int(entry.zone);
                        renderer.spatial.destination_event = entry.event;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::TextWrapped(
                    "Scan includes saved entrance edits; this area's live entrance shapes supply "
                    "its current links. Rescan after editing other areas.");
            }
        } else if (region.kind == SpatialKind::Actor || region.kind == SpatialKind::Interaction ||
                   region.kind == SpatialKind::StoryTrigger ||
                   region.kind == SpatialKind::Contact) {
            auto identity = std::to_string(area) + "/" + std::to_string(ref.local_zone) + "/" +
                            std::to_string(ref.script);
            if (identity_ != identity && !uses_loading_.valid()) {
                uses_.clear();
                uses_scanned_ = false;
                identity_ = identity;
                error_.clear();
            }
            ImGui::BeginDisabled(uses_loading_.valid());
            bool scan_uses = TutorialWidgets::Button("related_data", "Find shared script users");
            ImGui::EndDisabled();
            if (scan_uses) {
                auto resolved = resolve_interaction_source(source, scene.archive_sources, area,
                                                           ref.local_zone, region.zone, ref.script);
                require(resolved.shared, "This script is local to the selected zone; it has no "
                                         "shared-program users in other areas");
                auto archives = scene.archive_sources;
                auto member = resolved.member;
                uses_loading_ = std::async(std::launch::async, [source, archives, member] {
                    return shared_script_uses(source, archives, member);
                });
                identity_ = identity;
                error_.clear();
            }
            if (uses_loading_.valid())
                ImGui::TextDisabled("Scanning shared script dependencies...");
            if (identity_ == identity) {
                if (uses_scanned_)
                    ImGui::Text("Resolved placement users: %zu", uses_.size());
                ImGui::TextWrapped(
                    "Direct placement users of the same shared program. This does not prove they "
                    "execute the same handler. Other areas use the loaded project source; stage "
                    "and reload their placement edits before scanning.");
                ImGui::BeginChild("shared-program-users", {0, uses_.empty() ? 1.f : 180.f});
                for (std::size_t i = 0; i < uses_.size(); ++i) {
                    const auto &use = uses_[i];
                    ImGui::PushID(int(i));
                    ImGui::BeginDisabled(use.zone < 0);
                    auto label = use.kind + " / area " + std::to_string(use.area) + " / zone " +
                                 std::to_string(use.zone) + " / event " + std::to_string(use.event);
                    if (ImGui::Selectable(label.c_str())) {
                        unsigned category =
                            use.kind == "Contact Pokemon" ? TargetProfile::contact_placement_pack
                            : use.kind == "NPC"           ? TargetProfile::character_placement_pack
                            : use.kind == "Trainer"       ? TargetProfile::trainer_placement_pack
                            : use.kind == "Scenery" ? TargetProfile::interaction_placement_pack
                                                    : TargetProfile::position_event_pack;
                        request_ = RelatedMapTarget{use.area, use.local_zone, use.row,
                                                    category, use.event,      use.zone};
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
    } catch (const std::exception &error) {
        error_ = error.what();
        ImGui::TextWrapped("%s", error_.c_str());
    }
}

}
