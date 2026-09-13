#include "native/tutorial_widgets.h"
#include "native/material_inspector.h"
#include "native/imgui_renderer.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
namespace studio {
void material_inspector(const Environment *scene, const EnvironmentRenderer &renderer,
                        MaterialSelection &selection, const char *title) {
    bool reveal = selection.focus;
    if (selection.focus) {
        if (!scene || selection.draw < 0 || scene->draws.at(selection.draw).placement < 0)
            ImGui::SetNextWindowFocus();
        selection.focus = false;
    }
    ImGui::Begin(title);
    if (!scene) {
        ImGui::TextWrapped(
            "Load a model or map, then Ctrl+click a surface to inspect its material.");
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##material-filter", "Filter materials", selection.filter,
                             sizeof(selection.filter));
    auto lower = [](std::string s) {
        for (auto &c : s)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto filter = lower(selection.filter);
    std::vector<int> matches;
    for (std::size_t i = 0; i < scene->materials.size(); ++i)
        if (filter.empty() || lower(scene->materials[i].name).find(filter) != std::string::npos)
            matches.push_back(int(i));
    ImGui::BeginChild("Material list",
                      ImVec2(0, std::min(180.f, ImGui::GetContentRegionAvail().y * .35f)),
                      ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(int(matches.size()));
    if (reveal) {
        auto found = std::find(matches.begin(), matches.end(), selection.material);
        if (found != matches.end())
            clipper.IncludeItemByIndex(int(found - matches.begin()));
    }
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            int i = matches[row];
            ImGui::PushID(i);
            if (ImGui::Selectable(scene->materials[i].name.c_str(), selection.material == i)) {
                selection.material = i;
                selection.draw = -1;
            }
            if (reveal && selection.material == i)
                ImGui::SetScrollHereY(.5f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Instance %d | %s", i,
                                  scene->materials[i].resource_scope.c_str());
            ImGui::PopID();
        }
    ImGui::EndChild();
    if (selection.material < 0 || std::size_t(selection.material) >= scene->materials.size()) {
        ImGui::TextWrapped("Select a material above, or Ctrl+click a surface.");
        ImGui::End();
        return;
    }
    auto &base = scene->materials[selection.material];
    auto &live = renderer.materials();
    auto &m = std::size_t(selection.material) < live.size() ? live[selection.material] : base;
    ImGui::TextWrapped("%s", m.name.c_str());
    if (selection.draw >= 0 && std::size_t(selection.draw) < scene->draws.size())
        ImGui::TextWrapped("%s", scene->draws[selection.draw].name.c_str());
    ImGui::TextDisabled("Material instance %d", selection.material);
    if (studio::TutorialWidgets::Button("material_inspector", "Clear selection")) {
        selection.material = selection.draw = -1;
        ImGui::End();
        return;
    }
    if (selection.draw >= 0 && std::size_t(selection.draw) < scene->draws.size()) {
        auto &draw = scene->draws[selection.draw];
        if (studio::TutorialWidgets::CollapsingHeader("material_inspector", "Selected mesh",
                                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("%zu vertices | %zu triangles", draw.vertices.size(),
                        draw.indices.size() / 3);
            auto visibility =
                evaluate_visibility(*scene, renderer.playback.seconds, renderer.lighting.hour,
                                    renderer.visibility_enabled);
            const char *hidden = nullptr;
            if (!visibility[selection.draw])
                hidden = "Hidden by mesh visibility animation";
            if (draw.player >= 0 &&
                (!renderer.player.active || draw.player != int(renderer.player.appearance)))
                hidden = "This player appearance is not active";
            else if (draw.character && !renderer.characters_enabled)
                hidden = "Characters are disabled";
            else if (draw.conditional && !renderer.conditional_characters)
                hidden = "Story-dependent characters are disabled";
            if (draw.sky_part >= 0) {
                if (!renderer.sky_enabled)
                    hidden = "Skybox is disabled";
                else if (renderer.lighting.context >= scene->lighting_contexts.size() ||
                         !scene->lighting_contexts[renderer.lighting.context].sky_enabled)
                    hidden = "This zone has no skybox";
                else if ((draw.sky_part == 0 && renderer.sky_type != 0) ||
                         (draw.sky_part == 1 && renderer.sky_type == 0))
                    hidden = "Inactive sky variant";
            }
            if (draw.weather_mask && (!renderer.particles_enabled ||
                                      !(renderer.weather_effect < 32 &&
                                        (draw.weather_mask & (1u << renderer.weather_effect)))))
                hidden = "Inactive weather layer";
            if (hidden)
                ImGui::TextWrapped("%s", hidden);
            if (draw.conditional)
                ImGui::TextWrapped("Visibility depends on story progress.");
            if (draw.skeleton >= 0) {
                auto &rig = scene->skeletons.at(draw.skeleton);
                MaterialMotion clock;
                clock.frames = rig.motion.frames;
                clock.looping = rig.motion.looping;
                ImGui::Text("%zu joints | %zu motion tracks", rig.joints.size(),
                            rig.motion.tracks.size());
                ImGui::Text("Skeletal frame %.1f / %.0f",
                            animation_frame(clock, renderer.playback.seconds, rig.daily,
                                            renderer.lighting.hour),
                            rig.motion.frames);
                ImGui::TextDisabled("%s", !renderer.playback.enabled || !renderer.playback.skeletal
                                              ? "Showing bind pose"
                                          : rig.daily          ? "Follows time of day"
                                          : rig.motion.looping ? "Repeating motion"
                                                               : "One-shot motion");
            } else
                ImGui::TextDisabled("No skeletal motion bound");
        }
    }
    if (studio::TutorialWidgets::CollapsingHeader("material_inspector", "Material coverage",
                                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        bool problem = false;
        if (!m.combiner.unsupported.empty()) {
            ImGui::TextWrapped("%s", m.combiner.unsupported.c_str());
            problem = true;
        }
        if (m.unsupported_mapping) {
            ImGui::TextWrapped("Unsupported texture mapping");
            problem = true;
        }
        for (auto &texture : m.texture_inputs)
            if (!texture.empty() && !scene->textures.contains(texture)) {
                ImGui::TextWrapped("Missing texture: %s", texture.c_str());
                problem = true;
            }
        if (!problem)
            ImGui::TextWrapped("No texture or material-input issues detected.");
    }
    if (studio::TutorialWidgets::CollapsingHeader("material_inspector",
                                                  "Meshes using this material"))
        for (unsigned i = 0; i < scene->draws.size(); ++i)
            if (scene->draws[i].material == std::size_t(selection.material)) {
                ImGui::PushID(int(i));
                if (ImGui::Selectable(scene->draws[i].name.c_str(), selection.draw == int(i))) {
                    selection.draw = int(i);
                    selection.reveal_scene = true;
                }
                ImGui::PopID();
            }
    if (studio::TutorialWidgets::CollapsingHeader("material_inspector", "Animation tracks",
                                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        bool any = false;
        for (auto &animation : scene->material_animations) {
            bool heading = false;
            for (auto binding : animation.bindings)
                if (binding.material == std::size_t(selection.material)) {
                    any = true;
                    if (!heading) {
                        ImGui::TextWrapped("%s", animation.name.c_str());
                        ImGui::TextDisabled(
                            "Frame %.1f / %.0f%s",
                            animation_frame(animation.motion, renderer.playback.seconds,
                                            animation.daily, renderer.lighting.hour),
                            animation.motion.frames,
                            animation.daily            ? " (time of day)"
                            : animation.motion.looping ? " (loop)"
                                                       : "");
                        heading = true;
                    }
                    auto &track = animation.motion.tracks[binding.track];
                    const char *kind =
                        track.kind == MaterialTrack::Kind::TextureTransform ? "UV transform"
                        : track.kind == MaterialTrack::Kind::ConstantColor  ? "Constant color"
                                                                            : "Texture swap";
                    ImGui::BulletText("%s / slot %u", kind, track.slot);
                }
        }
        if (!any)
            ImGui::TextUnformatted("No bound material tracks");
        if (!renderer.playback.enabled)
            ImGui::TextDisabled("Animation disabled; showing base values");
    }
    if (studio::TutorialWidgets::CollapsingHeader("material_inspector", "Textures",
                                                  ImGuiTreeNodeFlags_DefaultOpen))
        for (unsigned unit = 0; unit < 3; ++unit)
            if (!m.texture_inputs[unit].empty()) {
                ImGui::PushID(int(unit));
                auto &input = m.inputs[unit];
                ImGui::Separator();
                ImGui::Text("Texture %u", unit);
                ImGui::SameLine();
                if (input.source == 5)
                    ImGui::TextDisabled("Projected mapping");
                else if (input.source == 4)
                    ImGui::TextDisabled("Camera-sphere mapping");
                else
                    ImGui::TextDisabled("UV %u", input.source);
                ImGui::TextWrapped("%s", m.texture_inputs[unit].c_str());
                auto it = scene->textures.find(m.texture_inputs[unit]);
                auto handle = renderer.texture(m.texture_inputs[unit]);
                if (it != scene->textures.end() && bgfx::isValid(handle)) {
                    auto &image = it->second;
                    float w = std::min(160.f, ImGui::GetContentRegionAvail().x),
                          h = w * image.height / image.width;
                    if (h > 160) {
                        w *= 160 / h;
                        h = 160;
                    }
                    ImGui::Image(ImTextureID(ImGuiRenderer::image_id(handle, false)), ImVec2(w, h));
                    ImGui::TextDisabled("%u x %u | %u stored levels", image.width, image.height,
                                        image.authored_levels);
                }
                ImGui::Text("Scale %.3f, %.3f", input.transform[0], input.transform[1]);
                ImGui::Text("Offset %.3f, %.3f", input.transform[3], input.transform[4]);
                ImGui::Text("Rotation %.3f rad", input.transform[2]);
                ImGui::PopID();
            }
    if (studio::TutorialWidgets::CollapsingHeader("material_inspector", "Material state")) {
        ImGui::TextWrapped("Vertex shader: %s", m.vertex_shader.c_str());
        ImGui::TextWrapped("Fragment shader: %s", m.fragment_shader.c_str());
        ImGui::Text("Light set %d", m.light_set);
        ImGui::Text("Alpha comparison %u / reference %u", m.alpha_function, m.alpha_reference);
        ImGui::Text("Cull mode %u", m.cull);
        if (!m.combiner.unsupported.empty())
            ImGui::TextWrapped("%s", m.combiner.unsupported.c_str());
        if (m.unsupported_mapping)
            ImGui::TextWrapped("Unsupported texture mapping");
    }
    ImGui::End();
}
}
