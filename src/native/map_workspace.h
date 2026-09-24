#pragma once
#include <imgui.h>
#include <cmath>
#include <string>
namespace studio {
enum class MapInspectorPage { Selection, Environment, Display, Settings };
enum class MapToolIcon { Select, Play, Frame, Start, Sun, Display, Focus };
inline bool map_tool_button(const char *label, MapToolIcon icon, bool active = false) {
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    const auto origin = ImGui::GetCursorScreenPos();
    const auto text = std::string("    ") + label;
    const bool pressed = ImGui::Button(text.c_str());
    if (active)
        ImGui::PopStyleColor();
    auto *draw = ImGui::GetWindowDrawList();
    const auto color = ImGui::GetColorU32(ImGuiCol_Text);
    const float x = origin.x + 8, y = origin.y + ImGui::GetFrameHeight() * .5f - 6;
    auto line = [&](float ax, float ay, float bx, float by) {
        draw->AddLine({x + ax, y + ay}, {x + bx, y + by}, color, 1.5f);
    };
    if (icon == MapToolIcon::Play)
        draw->AddTriangle({x + 2, y}, {x + 12, y + 6}, {x + 2, y + 12}, color, 1.5f);
    else if (icon == MapToolIcon::Select) {
        line(1, 0, 1, 12);
        line(1, 0, 11, 8);
        line(1, 12, 5, 8);
        line(5, 8, 11, 8);
    } else if (icon == MapToolIcon::Sun) {
        draw->AddCircle({x + 6, y + 6}, 3, color, 12, 1.5f);
        for (unsigned i = 0; i < 8; ++i) {
            const auto a = float(i) * .785398f;
            line(6 + 5 * std::cos(a), 6 + 5 * std::sin(a), 6 + 7 * std::cos(a),
                 6 + 7 * std::sin(a));
        }
    } else if (icon == MapToolIcon::Start) {
        line(0, 6, 6, 0);
        line(6, 0, 12, 6);
        line(2, 5, 2, 12);
        line(2, 12, 10, 12);
        line(10, 12, 10, 5);
    } else if (icon == MapToolIcon::Display) {
        draw->AddRect({x, y}, {x + 12, y + 9}, color, 1, 0, 1.5f);
        line(6, 9, 6, 12);
        line(3, 12, 9, 12);
    } else {
        line(0, 4, 0, 0);
        line(0, 0, 4, 0);
        line(8, 0, 12, 0);
        line(12, 0, 12, 4);
        line(12, 8, 12, 12);
        line(12, 12, 8, 12);
        line(4, 12, 0, 12);
        line(0, 12, 0, 8);
    }
    return pressed;
}
}
