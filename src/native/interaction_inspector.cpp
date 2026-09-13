#include "native/tutorial_widgets.h"
#include "native/interaction_inspector.h"
#include <imgui.h>
namespace studio {
void InteractionInspector::controls(const Environment *scene, const std::filesystem::path &dump,
                                    int area, int selection) {
    const SpatialRegion *region =
        scene && selection >= 0 && std::size_t(selection) < scene->spatial.regions.size()
            ? &scene->spatial.regions[selection]
            : nullptr;
    bool available = region && region->overworld && region->kind != SpatialKind::Entrance &&
                     region->overworld->script;
    ImGui::Begin("Map editing");
    ImGui::BeginDisabled(!available);
    if (studio::TutorialWidgets::Button("interaction_inspector", "Inspect interaction",
                                        ImVec2(-1, 0))) {
        open_ = true;
        error_.clear();
        inspection_ = {};
        auto &reference = *region->overworld;
        identity_ = "Zone " + std::to_string(region->zone) + " / event " +
                    std::to_string(reference.event) + " / script " +
                    std::to_string(reference.script);
        try {
            inspection_ = inspect_interaction(dump, scene->archive_sources, unsigned(area),
                                              reference.local_zone, region->zone, reference.script,
                                              reference.event);
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Select an NPC or trigger with a script.");
    ImGui::End();
    if (!open_)
        return;
    ImGui::SetNextWindowSize(ImVec2(740, 620), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Interaction inspector", &open_)) {
        ImGui::TextUnformatted(identity_.c_str());
        ImGui::TextDisabled("Read-only preview / no scripts are executed or changed");
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        else {
            ImGui::TextWrapped("%s", inspection_.notice.c_str());
            if (ImGui::BeginTabBar("Interaction views")) {
                if (studio::TutorialWidgets::BeginTabItem("interaction_inspector", "Actions")) {
                    ImGui::TextWrapped("Dialogue previews use resolved English message tables. "
                                       "Dynamic words and formatting are omitted.");
                    studio::TutorialWidgets::Checkbox("interaction_inspector",
                                                      "Show technical calls", &technical_);
                    ImGui::BeginChild("Action sequence", ImVec2(0, 0));
                    if (inspection_.actions.empty())
                        ImGui::TextWrapped("No reliable action summary is available. Inspect the "
                                           "AMX tab for the decoded instructions.");
                    unsigned visible_index = 0;
                    for (unsigned index = 0; index < inspection_.actions.size(); ++index) {
                        auto &action = inspection_.actions[index];
                        if (!technical_ && !action.recognized &&
                            action.title != "Unresolved control flow" &&
                            action.title != "Unresolved helper" && action.title != "Runtime loop" &&
                            action.title.rfind("Unknown native", 0) != 0 &&
                            action.title.rfind("Call shared script", 0) != 0 &&
                            action.title != "Change message context")
                            continue;
                        ImGui::PushID(int(index));
                        ImGui::Separator();
                        ImGui::TextDisabled("%u.  AMX %06X", ++visible_index, action.address);
                        if (action.recognized) {
                            ImGui::TextUnformatted(action.title.c_str());
                            ImGui::TextWrapped("%s", action.detail.c_str());
                        } else if (ImGui::TreeNode("detail", "%s", action.title.c_str())) {
                            ImGui::TextWrapped("%s", action.detail.c_str());
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (studio::TutorialWidgets::BeginTabItem("interaction_inspector", "AMX")) {
                    ImGui::TextWrapped("%s", inspection_.source.c_str());
                    if (studio::TutorialWidgets::Button("interaction_inspector",
                                                        "Copy disassembly"))
                        ImGui::SetClipboardText(inspection_.listing.c_str());
                    ImGui::BeginChild("Disassembly", ImVec2(0, 0), ImGuiChildFlags_None,
                                      ImGuiWindowFlags_HorizontalScrollbar);
                    ImGui::TextUnformatted(inspection_.listing.c_str());
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
    }
    ImGui::End();
}
}
