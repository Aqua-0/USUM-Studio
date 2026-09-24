#pragma once
#include "native/camera.h"
#include <SDL3/SDL.h>
#include <imgui.h>
namespace studio {
inline constexpr const char *viewport_navigation_help =
    "Middle drag: orbit | Shift + middle drag: pan | Wheel: zoom\n"
    "Hold right mouse + WASD/QE: fly | Shift: faster | Right mouse + wheel: flight speed";
inline bool viewport_navigating() {
    return ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
           ImGui::IsMouseDown(ImGuiMouseButton_Right);
}
inline bool viewport_navigation(ViewportCamera &camera, SDL_Window *window, bool hovered) {
    auto &io = ImGui::GetIO();
    if (!hovered || io.WantTextInput || ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
        !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        return false;
    const bool flying = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (flying) {
        camera.rotate(io.MouseDelta.x, io.MouseDelta.y, true);
        auto key = [](ImGuiKey k) {
            return ImGui::IsKeyDown(k) ? 1.f : 0.f;
        };
        camera.fly(key(ImGuiKey_D) - key(ImGuiKey_A), key(ImGuiKey_E) - key(ImGuiKey_Q),
                   key(ImGuiKey_W) - key(ImGuiKey_S), io.DeltaTime, io.KeyShift ? 4.f : 1.f);
    } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        if (io.KeyShift)
            camera.pan(io.MouseDelta.x, io.MouseDelta.y);
        else
            camera.rotate(io.MouseDelta.x, io.MouseDelta.y, false);
    }
    camera.wheel(io.MouseWheel, flying);
    return viewport_navigating() || io.MouseWheel != 0;
}
}
