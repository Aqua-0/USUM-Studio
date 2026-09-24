#pragma once
#include <imgui.h>
namespace studio {
class InspectorSelectorStyle {
    bool active_ = true;

  public:
    explicit InspectorSelectorStyle(const char *label) {
        ImGui::TextDisabled("%s", label);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(.13f, .29f, .36f, 1));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(.18f, .39f, .47f, 1));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(.20f, .44f, .53f, 1));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.23f, .48f, .60f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.32f, .62f, .75f, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.40f, .62f, .72f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        ImGui::SetNextItemWidth(-1);
    }
    void end() {
        if (!active_)
            return;
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(6);
        active_ = false;
    }
    ~InspectorSelectorStyle() {
        end();
    }
};
}
