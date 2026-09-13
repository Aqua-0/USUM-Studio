#pragma once
#include <imgui.h>
namespace studio::TutorialWidgets {
using Observer = void (*)(const char *, const char *, bool);
inline Observer observer = nullptr;
inline void item(const char *module, const char *label, bool used = false) {
    if (observer)
        observer(module, label, used);
}
inline bool Button(const char *module, const char *label, const ImVec2 &size = ImVec2(0, 0)) {
    bool used = ImGui::Button(label, size);
    item(module, label, used);
    return used;
}
inline bool SmallButton(const char *module, const char *label) {
    bool used = ImGui::SmallButton(label);
    item(module, label, used);
    return used;
}
inline bool Checkbox(const char *module, const char *label, bool *value) {
    bool used = ImGui::Checkbox(label, value);
    item(module, label, used);
    return used;
}
inline bool RadioButton(const char *module, const char *label, bool value) {
    bool used = ImGui::RadioButton(label, value);
    item(module, label, used);
    return used;
}
inline bool RadioButton(const char *module, const char *label, int *value, int button) {
    bool used = ImGui::RadioButton(label, value, button);
    item(module, label, used);
    return used;
}
inline bool CollapsingHeader(const char *module, const char *label, ImGuiTreeNodeFlags flags = 0) {
    bool open = ImGui::CollapsingHeader(label, flags);
    item(module, label, ImGui::IsItemActivated());
    return open;
}
inline bool CollapsingHeader(const char *module, const char *label, bool *visible,
                             ImGuiTreeNodeFlags flags = 0) {
    bool open = ImGui::CollapsingHeader(label, visible, flags);
    item(module, label, ImGui::IsItemActivated());
    return open;
}
inline bool BeginTabItem(const char *module, const char *label, bool *open = nullptr,
                         ImGuiTabItemFlags flags = 0) {
    bool active = ImGui::BeginTabItem(label, open, flags);
    item(module, label, ImGui::IsItemActivated());
    return active;
}
inline bool MenuItem(const char *module, const char *label, const char *shortcut = nullptr,
                     bool selected = false, bool enabled = true) {
    bool used = ImGui::MenuItem(label, shortcut, selected, enabled);
    item(module, label, used);
    return used;
}
inline bool MenuItem(const char *module, const char *label, const char *shortcut, bool *selected,
                     bool enabled = true) {
    bool used = ImGui::MenuItem(label, shortcut, selected, enabled);
    item(module, label, used);
    return used;
}
inline bool TreeNode(const char *module, const char *label) {
    bool open = ImGui::TreeNode(label);
    item(module, label, ImGui::IsItemActivated());
    return open;
}
}
