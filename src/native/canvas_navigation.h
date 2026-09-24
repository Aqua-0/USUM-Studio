#pragma once
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace studio {
inline void navigate_canvas(ImVec2 origin, ImVec2 area, bool hovered, float &zoom, ImVec2 &pan) {
    if (!hovered || ImGui::GetIO().WantTextInput)
        return;
    auto &io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        zoom = 1;
        pan = {};
    }
    if (io.MouseWheel != 0) {
        float old = zoom;
        zoom = std::clamp(zoom * std::pow(1.2f, io.MouseWheel), .1f, 64.f);
        pan.x = io.MousePos.x - origin.x - area.x * .5f -
                (io.MousePos.x - origin.x - area.x * .5f - pan.x) * zoom / old;
        pan.y = io.MousePos.y - origin.y - area.y * .5f -
                (io.MousePos.y - origin.y - area.y * .5f - pan.y) * zoom / old;
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        pan.x += io.MouseDelta.x;
        pan.y += io.MouseDelta.y;
    }
}
}
