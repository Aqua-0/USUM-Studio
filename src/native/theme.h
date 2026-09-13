#pragma once
#include <imgui.h>
#include "native/tutorial_widgets.h"
namespace studio {
inline void apply_editor_theme() {
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowRounding = 4;
    style.ChildRounding = 3;
    style.FrameRounding = 2;
    style.PopupRounding = 4;
    style.TabRounding = 5;
    style.FrameBorderSize = 1;
    style.WindowBorderSize = 1;
    style.PopupBorderSize = 1;
    auto &colors = style.Colors;
    colors[ImGuiCol_Text] = {.91f, .93f, .95f, 1};
    colors[ImGuiCol_TextDisabled] = {.53f, .58f, .63f, 1};
    colors[ImGuiCol_WindowBg] = {.075f, .085f, .10f, 1};
    colors[ImGuiCol_PopupBg] = {.095f, .11f, .13f, 1};
    colors[ImGuiCol_Border] = {.27f, .31f, .36f, 1};
    colors[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    colors[ImGuiCol_FrameBg] = {.045f, .055f, .07f, 1};
    colors[ImGuiCol_FrameBgHovered] = {.12f, .15f, .18f, 1};
    colors[ImGuiCol_FrameBgActive] = {.15f, .20f, .25f, 1};
    colors[ImGuiCol_Button] = {.22f, .25f, .29f, 1};
    colors[ImGuiCol_ButtonHovered] = {.30f, .35f, .40f, 1};
    colors[ImGuiCol_ButtonActive] = {.16f, .22f, .28f, 1};
    colors[ImGuiCol_Header] = {.16f, .21f, .25f, 1};
    colors[ImGuiCol_HeaderHovered] = {.23f, .31f, .37f, 1};
    colors[ImGuiCol_HeaderActive] = {.18f, .32f, .43f, 1};
    colors[ImGuiCol_CheckMark] = {.36f, .83f, .73f, 1};
    colors[ImGuiCol_SliderGrab] = {.36f, .68f, .88f, 1};
    colors[ImGuiCol_SliderGrabActive] = {.55f, .82f, 1, 1};
    colors[ImGuiCol_TitleBg] = {.10f, .12f, .15f, 1};
    colors[ImGuiCol_TitleBgActive] = {.15f, .19f, .23f, 1};
    colors[ImGuiCol_TitleBgCollapsed] = {.10f, .12f, .15f, 1};
    colors[ImGuiCol_MenuBarBg] = {.10f, .12f, .15f, 1};
    colors[ImGuiCol_Tab] = {.12f, .14f, .17f, 1};
    colors[ImGuiCol_TabHovered] = {.23f, .32f, .39f, 1};
    colors[ImGuiCol_TabSelected] = {.19f, .27f, .34f, 1};
    colors[ImGuiCol_TabSelectedOverline] = {.36f, .68f, .88f, 1};
    colors[ImGuiCol_TabDimmed] = {.10f, .12f, .15f, 1};
    colors[ImGuiCol_TabDimmedSelected] = {.17f, .21f, .26f, 1};
    colors[ImGuiCol_TabDimmedSelectedOverline] = {.32f, .47f, .58f, 1};
    colors[ImGuiCol_Separator] = {.25f, .29f, .34f, 1};
    colors[ImGuiCol_SeparatorHovered] = {.36f, .68f, .88f, 1};
    colors[ImGuiCol_SeparatorActive] = {.55f, .82f, 1, 1};
    colors[ImGuiCol_TextSelectedBg] = {.20f, .45f, .65f, .65f};
    colors[ImGuiCol_NavCursor] = {.55f, .82f, 1, 1};
}
inline bool primary_button(const char *label, ImVec2 size = {0, 0}) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetStyle().FrameRounding * 3.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .36f, .52f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.21f, .46f, .65f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.12f, .28f, .42f, 1));
    bool clicked = TutorialWidgets::Button("primary", label, size);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return clicked;
}
}
