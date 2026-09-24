#pragma once
#include "core/undo_routing.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <string_view>
namespace studio::UndoShortcuts {
inline UndoRouting routing;
inline std::string pending_history, pending_window;
inline int pending_action=0;
inline bool consumed=false;
inline std::set<std::string> blocked;
inline void block(const char *history) {blocked.insert(history);}
inline std::string canonical(const char *group) {
    std::string name(group);
    if(name=="refresh_inspector" || name=="refresh_feeding_preview" || name=="texture_painter" || name=="lighting_table_editor") return "material_editor";
    return name;
}
inline const char *window_name() {return ImGui::GetCurrentWindow()->RootWindow->Name;}
inline void aliases(const std::string &group) {
    const auto offer=[&](std::initializer_list<const char *> windows){for(auto window:windows) routing.offer(window,group);};
    if(group=="material_editor") offer({"Studio materials","Studio inspector","Studio viewport","Studio UVs","Studio animation","Texture painter","Material lighting tables","Animate mapping"});
    if(group=="map_authoring_workspace" || group=="existing_map_editor") offer({"Composition","Asset Library","Authoring tools","Authoring viewport","Authoring status"});
    if(group=="collision_editor") offer({"Collision viewport","Collision editing","Collision layers","Collision properties"});
    if(group=="camera_editor") offer({"Camera viewport","Camera editing","Camera regions"});
    if(group=="audio_editor") offer({"Audio library","Audio studio","Audio inspector"});
    if(group=="placement_editor") offer({"Environment","Map inspector"});
    if(group=="image_editor") offer({"Image library","Image preview","Image properties"});
}
inline int action(const char *label) {
    std::string_view text(label);
    if(text=="Undo" || text.starts_with("Undo##") || text.starts_with("Undo ")) return 1;
    if(text=="Redo" || text.starts_with("Redo##") || text.starts_with("Redo ")) return 2;
    return 0;
}
inline void observe(const char *group,bool used) {
    if(used) routing.activate(window_name(),canonical(group));
}
inline bool button(const char *group,const char *label,ImVec2 size={0,0}) {
    bool clicked=ImGui::Button(label,size);
    int operation=action(label);
    if(!operation) return clicked;
    const auto history=canonical(group);
    routing.offer(window_name(),history);
    aliases(history);
    if(clicked) {routing.activate(window_name(),canonical(group));consumed=true;return true;}
    auto &io=ImGui::GetIO();
    bool input_blocked=io.WantTextInput || ImGui::IsAnyItemActive() || io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] ||
        (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled);
    auto *focused=ImGui::GetCurrentContext()->NavWindow;
    bool same_focus=focused && pending_window==focused->RootWindow->Name;
    if(same_focus && !input_blocked && !consumed && pending_action==operation && pending_history==history && !blocked.contains(history)) {consumed=true;return true;}
    return false;
}
inline void begin_frame() {routing.clear();blocked.clear();consumed=false;}
inline void end_frame() {
    pending_action=0;pending_history.clear();pending_window.clear();
    auto &io=ImGui::GetIO();auto *context=ImGui::GetCurrentContext();
    if(consumed || !context->NavWindow || io.WantTextInput || ImGui::IsAnyItemActive() ||
        io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] || !io.KeyCtrl || io.KeyAlt || io.AppFocusLost) return;
    if(ImGui::IsKeyPressed(ImGuiKey_Z,false)) pending_action=io.KeyShift?2:1;
    else if(ImGui::IsKeyPressed(ImGuiKey_Y,false)) pending_action=2;
    if(pending_action) {pending_window=context->NavWindow->RootWindow->Name;pending_history=routing.target(pending_window);}
    if(blocked.contains(pending_history)) {pending_action=0;pending_history.clear();}
}
}
