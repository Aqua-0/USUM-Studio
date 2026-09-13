#include "native/tutorial_widgets.h"
#include "native/refresh_inspector.h"
#include <imgui.h>
#include "native/imgui_renderer.h"
#include "scene/refresh_surface.h"
#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <set>
namespace studio {
void RefreshInspector::sync(const ModelDocument &document, EnvironmentRenderer &renderer) {
    if (!renderer.refresh.enabled)
        return;
    try {
        renderer.refresh.set_regions(document.refresh_regions, document.texture_prefix);
        error_.clear();
    } catch (const std::exception &e) {
        renderer.refresh.enabled = false;
        error_ = e.what();
    }
}
void RefreshInspector::draw(const ModelDocument &document, EnvironmentRenderer &renderer,
                            const MaterialSelection &selection, MaterialDocument *edit) {
    if (studio::TutorialWidgets::RadioButton("refresh_inspector", "Regions", !feeding.active)) {
        feeding.active = false;
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::RadioButton("refresh_inspector", "Feeding",
                                             feeding.active && !feeding.camera_view)) {
        if (edit)
            edit->commit();
        painting_ = stroke_ = false;
        feeding.active = true;
        feeding.camera_view = false;
        renderer.refresh.enabled = false;
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::RadioButton("refresh_inspector", "Cameras",
                                             feeding.active && feeding.camera_view)) {
        if (edit)
            edit->commit();
        painting_ = stroke_ = false;
        feeding.active = true;
        feeding.camera_view = true;
        renderer.refresh.enabled = false;
    }
    if (feeding.active) {
        feeding.controls(document, edit);
        return;
    }
    ImGui::TextUnformatted("Refresh interaction regions");
    if (!edit)
        ImGui::TextWrapped("Send to Studio to paint and save Refresh masks.");
    if (!document.refresh_error.empty()) {
        ImGui::TextWrapped("Regions unavailable: %s", document.refresh_error.c_str());
        return;
    }
    if (!document.refresh_regions || document.refresh_regions->masks.empty()) {
        ImGui::TextWrapped("This model variant has no Refresh masks.");
        return;
    }
    auto regions = document.refresh_regions;
    auto &pack = *regions;
    std::array<std::size_t, 256> counts{};
    for (auto &mask : pack.masks)
        for (unsigned id = 0; id < 256; ++id)
            counts[id] += mask.counts[id];
    if (painting_ && renderer.refresh.selected >= 0) {
        if (known_refresh_region(std::uint8_t(renderer.refresh.selected)))
            category_ = renderer.refresh.selected;
        renderer.refresh.selected = -1;
    }
    if (studio::TutorialWidgets::Checkbox("refresh_inspector", "Show interaction regions",
                                          &renderer.refresh.enabled)) {
        renderer.invalidate_selection_readback();
        sync(document, renderer);
    }
    if (edit) {
        if (studio::TutorialWidgets::Checkbox("refresh_inspector", "Paint regions", &painting_)) {
            edit->commit();
            stroke_ = false;
            if (painting_) {
                renderer.refresh.enabled = true;
                renderer.refresh.selected = -1;
                sync(document, renderer);
            }
            renderer.invalidate_selection_readback();
        }
        if (!renderer.refresh.enabled)
            painting_ = false;
        if (painting_) {
            auto color = refresh_region_color(std::uint8_t(category_));
            ImGui::ColorButton("##active-paint-color", {color[0], color[1], color[2], 1},
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                               {32, 32});
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted("Active paint");
            ImGui::TextWrapped("%s (%d)",
                               std::string(refresh_region_label(std::uint8_t(category_))).c_str(),
                               category_);
            ImGui::EndGroup();
            studio::TutorialWidgets::Checkbox("refresh_inspector", "Show all reaction categories",
                                              &show_all_categories_);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(
                    "##paint-category",
                    std::string(refresh_region_label(std::uint8_t(category_))).c_str())) {
                for (unsigned id = 0; id < 256; ++id)
                    if (known_refresh_region(std::uint8_t(id)) &&
                        (show_all_categories_ || counts[id] || int(id) == category_)) {
                        ImGui::PushID(int(id));
                        auto color = refresh_region_color(std::uint8_t(id));
                        ImGui::ColorButton("##paint-swatch", {color[0], color[1], color[2], 1},
                                           ImGuiColorEditFlags_NoTooltip |
                                               ImGuiColorEditFlags_NoDragDrop,
                                           {16, 16});
                        ImGui::SameLine();
                        auto label = std::string(refresh_region_label(std::uint8_t(id))) + " (" +
                                     std::to_string(id) + ")";
                        if (ImGui::Selectable(label.c_str(), category_ == int(id)))
                            category_ = int(id);
                        ImGui::PopID();
                    }
                ImGui::EndCombo();
            }
            ImGui::TextUnformatted("Brush radius (texels)");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderInt("##refresh-radius", &radius_, 1, 32);
            if (studio::TutorialWidgets::Button("refresh_inspector", "Erase / background"))
                category_ = 0;
            ImGui::TextWrapped("Left drag: paint | Shift+drag: orbit");
        }
        ImGui::BeginDisabled(stroke_);
        ImGui::BeginDisabled(!edit->can_undo());
        if (studio::TutorialWidgets::Button("refresh_inspector", "Undo"))
            edit->undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!edit->can_redo());
        if (studio::TutorialWidgets::Button("refresh_inspector", "Redo"))
            edit->redo();
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (studio::TutorialWidgets::CollapsingHeader("refresh_inspector", "Painting help")) {
            ImGui::TextWrapped(
                "Ctrl+click samples a reaction category from the model. Painting pauses animation. "
                "Brush radius is measured in mask texels; its size on the model depends on the UV "
                "layout. Mirrored UVs and shared masks update together. UVs repeat across texture "
                "edges, and the brush continues across those edges too.");
            ImGui::TextWrapped(
                "Use Save edits for the document; Write game files exports the edited masks. "
                "Normal/shiny and texture-sharing forms use the same source.");
        }
    }
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    if (!painting_)
        ImGui::TextWrapped("Ctrl+click the model to select a category. Pause or scrub a motion to "
                           "inspect its animated surface.");
    if (!painting_ &&
        studio::TutorialWidgets::Button("refresh_inspector", "Clear category highlight")) {
        renderer.refresh.selected = -1;
        renderer.invalidate_selection_readback();
    }
    if (renderer.refresh.selected >= 0)
        ImGui::TextWrapped(
            "Selected: %s",
            std::string(refresh_region_label(std::uint8_t(renderer.refresh.selected))).c_str());
    if (selection.material >= 0 &&
        std::size_t(selection.material) < document.scene->materials.size()) {
        auto &material = document.scene->materials[std::size_t(selection.material)];
        auto binding = bind_refresh_material(pack, material, document.texture_prefix);
        ImGui::Separator();
        ImGui::TextWrapped("Material: %s", material.name.c_str());
        if (binding.mask >= 0) {
            auto &mask = pack.masks[std::size_t(binding.mask)];
            ImGui::TextWrapped("Mask: %s", mask.texture.c_str());
            ImGui::Text("%u x %u", unsigned(mask.width), unsigned(mask.height));
            if (!mask.read_only_reason.empty())
                ImGui::TextWrapped("%s", mask.read_only_reason.c_str());
            if (edit && mask.read_only_reason.empty()) {
                ImGui::BeginDisabled(stroke_);
                if (studio::TutorialWidgets::Button("refresh_inspector", "Reset this mask")) {
                    edit->reset_refresh(std::size_t(binding.mask));
                    renderer.invalidate_selection_readback();
                }
                ImGui::EndDisabled();
            }
        } else
            ImGui::TextWrapped("%s", binding.reason.c_str());
    }
    ImGui::Text("%zu masks", pack.masks.size());
    if (!painting_)
        studio::TutorialWidgets::Checkbox("refresh_inspector", "Show all reaction categories",
                                          &show_all_categories_);
    ImGui::BeginChild("Refresh categories", ImVec2(0, 210), ImGuiChildFlags_Borders);
    for (unsigned id = 0; id < 256; ++id)
        if (counts[id] || (show_all_categories_ && known_refresh_region(std::uint8_t(id)))) {
            ImGui::PushID(int(id));
            auto color = refresh_region_color(std::uint8_t(id));
            ImGui::ColorButton("##category-color", {color[0], color[1], color[2], 1},
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                               {16, 16});
            ImGui::SameLine();
            auto label = std::string(refresh_region_label(std::uint8_t(id))) + " (" +
                         std::to_string(id) + ")";
            if (ImGui::Selectable(label.c_str(), painting_
                                                     ? category_ == int(id)
                                                     : renderer.refresh.selected == int(id))) {
                if (painting_) {
                    if (known_refresh_region(std::uint8_t(id)))
                        category_ = int(id);
                } else {
                    renderer.refresh.enabled = true;
                    sync(document, renderer);
                    renderer.refresh.selected = int(id);
                    renderer.invalidate_selection_readback();
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%zu stored texels; includes unused UV space", label.c_str(),
                                  counts[id]);
            ImGui::PopID();
        }
    ImGui::EndChild();
    ImGui::TextWrapped("Dark surfaces have background IDs or unavailable bindings. Category colors "
                       "are preview aids; game reactions also depend on interaction state.");
    if (studio::TutorialWidgets::TreeNode("refresh_inspector", "Material coverage")) {
        for (auto &material : document.scene->materials) {
            auto binding = bind_refresh_material(pack, material, document.texture_prefix);
            if (binding.mask >= 0)
                ImGui::TextWrapped("%s: %s", material.name.c_str(),
                                   pack.masks[std::size_t(binding.mask)].texture.c_str());
            else
                ImGui::TextWrapped("%s: %s", material.name.c_str(), binding.reason.c_str());
        }
        ImGui::TreePop();
    }
    ImGui::SeparatorText("Mask texture");
    if (preview_material_ != selection.material) {
        preview_material_ = selection.material;
        if (selection.material >= 0 &&
            std::size_t(selection.material) < document.scene->materials.size()) {
            auto binding = bind_refresh_material(
                pack, document.scene->materials[std::size_t(selection.material)],
                document.texture_prefix);
            if (binding.mask >= 0)
                preview_mask_ = binding.mask;
        }
    }
    preview_mask_ = std::clamp(preview_mask_, 0, int(pack.masks.size()) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##preview-mask",
                          pack.masks[std::size_t(preview_mask_)].texture.c_str())) {
        for (std::size_t i = 0; i < pack.masks.size(); ++i)
            if (ImGui::Selectable(pack.masks[i].texture.c_str(), preview_mask_ == int(i)))
                preview_mask_ = int(i);
        ImGui::EndCombo();
    }
    studio::TutorialWidgets::Checkbox("refresh_inspector", "Show UVs", &show_uvs_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("UVs for visible meshes using this mask, repeated into the texture tile");
    if (painting_) {
        auto color = refresh_region_color(std::uint8_t(category_));
        ImGui::ColorButton("##preview-paint-color", {color[0], color[1], color[2], 1},
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                           {16, 16});
        ImGui::SameLine();
        ImGui::TextWrapped("Paint: %s (%d)",
                           std::string(refresh_region_label(std::uint8_t(category_))).c_str(),
                           category_);
    }
    auto &mask = pack.masks[std::size_t(preview_mask_)];
    ImGui::Text("%u x %u | reaction colors", unsigned(mask.width), unsigned(mask.height));
    bool visible_uvs = false;
    try {
        renderer.refresh.set_regions(document.refresh_regions, document.texture_prefix);
        auto texture = renderer.refresh.preview_texture(std::size_t(preview_mask_));
        float scale = std::min({std::max(1.f, ImGui::GetContentRegionAvail().x) / mask.width,
                                384.f / mask.width, 384.f / mask.height});
        ImVec2 size{mask.width * scale, mask.height * scale};
        ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false)), size);
        if (show_uvs_) {
            if (uv_scene_ != document.scene) {
                uv_scene_ = document.scene;
                uv_edges_.clear();
                uv_edges_.resize(document.scene->draws.size());
                for (std::size_t i = 0; i < document.scene->draws.size(); ++i) {
                    auto &draw = document.scene->draws[i];
                    std::set<std::pair<std::uint16_t, std::uint16_t>> edges;
                    for (std::size_t j = 0; j + 2 < draw.indices.size(); j += 3)
                        for (unsigned k = 0; k < 3; ++k) {
                            auto a = draw.indices[j + k], b = draw.indices[j + (k + 1) % 3];
                            if (a != b)
                                edges.emplace(std::min(a, b), std::max(a, b));
                        }
                    for (auto [a, b] : edges) {
                        auto &first = draw.vertices.at(a);
                        auto &second = draw.vertices.at(b);
                        for (auto &segment :
                             refresh_uv_segments({first.u, first.v}, {second.u, second.v}))
                            uv_edges_[i].push_back({ImVec2{segment[0][0], segment[0][1]},
                                                    ImVec2{segment[1][0], segment[1][1]}});
                    }
                }
            }
            auto origin = ImGui::GetItemRectMin();
            auto visible = evaluate_visibility(*document.scene, renderer.playback.seconds,
                                               renderer.lighting.hour, renderer.visibility_enabled);
            auto *lines = ImGui::GetWindowDrawList();
            lines->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
            for (std::size_t i = 0; i < uv_edges_.size(); ++i)
                if (visible[i] && renderer.draw_visible(i) &&
                    bind_refresh_material(
                        pack, document.scene->materials[document.scene->draws[i].material],
                        document.texture_prefix)
                            .mask == preview_mask_)
                    for (auto &edge : uv_edges_[i]) {
                        visible_uvs = true;
                        ImVec2 a{origin.x + edge[0].x * size.x, origin.y + edge[0].y * size.y},
                            b{origin.x + edge[1].x * size.x, origin.y + edge[1].y * size.y};
                        lines->AddLine(a, b, IM_COL32(0, 0, 0, 170), 2);
                        lines->AddLine(a, b, IM_COL32(255, 255, 255, 200), 1);
                    }
            lines->PopClipRect();
        }
        if (ImGui::IsItemHovered()) {
            auto origin = ImGui::GetItemRectMin();
            auto mouse = ImGui::GetIO().MousePos;
            auto x = unsigned(std::clamp((mouse.x - origin.x) / scale, 0.f, float(mask.width - 1))),
                 y = unsigned(
                     std::clamp((mouse.y - origin.y) / scale, 0.f, float(mask.height - 1)));
            auto id = mask.ids[std::size_t(y) * mask.width + x];
            ImGui::SetTooltip("%s (%u) | texel %u, %u",
                              std::string(refresh_region_label(id)).c_str(), unsigned(id), x, y);
        }
    } catch (const std::exception &e) {
        ImGui::TextWrapped("Texture preview unavailable: %s", e.what());
    }
    if (show_uvs_ && !visible_uvs)
        ImGui::TextWrapped(
            "No visible mesh UVs use this mask. Check mesh visibility or choose another mask.");
}
void RefreshInspector::paint(MaterialDocument *edit, EnvironmentRenderer &renderer,
                             const float *view, const float *projection, ImVec2 origin, ImVec2 size,
                             bool hovered, MaterialSelection &selection) {
    auto &io = ImGui::GetIO();
    if (stroke_ && (!io.MouseDown[0] || !hovered || io.KeyShift || io.KeyCtrl || !painting_)) {
        if (edit)
            edit->commit();
        stroke_ = false;
    }
    if (!edit || !painting_ || !renderer.refresh.active() || !hovered || io.WantTextInput ||
        io.KeyCtrl || io.KeyShift || !renderer.ready())
        return;
    auto *list = ImGui::GetWindowDrawList();
    list->AddCircle(io.MousePos, 6, IM_COL32(0, 0, 0, 255), 16, 3);
    list->AddCircle(io.MousePos, 6, IM_COL32(255, 255, 255, 255), 16, 1);
    auto color = refresh_region_color(std::uint8_t(category_));
    list->AddCircleFilled(io.MousePos, 3,
                          ImGui::ColorConvertFloat4ToU32({color[0], color[1], color[2], 1}));
    if (!io.MouseDown[0])
        return;
    if (!stroke_ && !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return;
    if (stroke_ && previous_.x == io.MousePos.x && previous_.y == io.MousePos.y)
        return;
    try {
        auto &model = edit->model;
        if (!model.refresh_regions)
            return;
        auto visible = evaluate_visibility(*model.scene, renderer.playback.seconds,
                                           renderer.lighting.hour, renderer.visibility_enabled);
        for (std::size_t i = 0; i < visible.size(); ++i)
            visible[i] = visible[i] && renderer.draw_visible(i);
        auto poses = evaluate_scene_poses(model.scene->skeletons, renderer.player,
                                          renderer.playback.seconds, renderer.lighting.hour,
                                          renderer.playback.enabled && renderer.playback.skeletal);
        RefreshSurface surface(*model.scene, *model.refresh_regions, model.texture_prefix, poses,
                               visible);
        std::map<std::size_t, RefreshRegionMask> painted;
        float inverse_projection[16], inverse_view[16];
        bx::mtxInverse(inverse_projection, projection);
        bx::mtxInverse(inverse_view, view);
        auto unproject = [&](float x, float y, float z) {
            float p[4] = {x, y, z, 1}, eye[4]{}, world[4]{};
            for (unsigned r = 0; r < 4; ++r)
                for (unsigned c = 0; c < 4; ++c)
                    eye[r] += inverse_projection[c * 4 + r] * p[c];
            for (unsigned r = 0; r < 4; ++r)
                for (unsigned c = 0; c < 4; ++c)
                    world[r] += inverse_view[c * 4 + r] * eye[c];
            require(std::abs(world[3]) > 1e-8f, "Cannot project brush ray");
            return std::array<float, 3>{world[0] / world[3], world[1] / world[3],
                                        world[2] / world[3]};
        };
        if (!stroke_) {
            previous_ = io.MousePos;
            edit->commit();
            stroke_ = true;
        }
        auto dx = io.MousePos.x - previous_.x, dy = io.MousePos.y - previous_.y;
        unsigned steps = std::max(1u, unsigned(std::ceil(std::sqrt(dx * dx + dy * dy) / 2)));
        for (unsigned step = 1; step <= steps; ++step) {
            float t = float(step) / steps;
            float x = ((previous_.x + dx * t - origin.x) / size.x) * 2 - 1,
                  y = 1 - ((previous_.y + dy * t - origin.y) / size.y) * 2;
            auto near = unproject(x, y, bgfx::getCaps()->homogeneousDepth ? -1.f : 0.f),
                 far = unproject(x, y, 1);
            std::array<float, 3> direction{};
            for (unsigned k = 0; k < 3; ++k)
                direction[k] = far[k] - near[k];
            auto hit = surface.hit(near, direction);
            if (!hit || hit->mask < 0 || hit->distance > 1)
                continue;
            selection.draw = int(hit->draw);
            selection.material = int(model.scene->draws.at(hit->draw).material);
            selection.focus = false;
            auto index = std::size_t(hit->mask);
            auto &source = model.refresh_regions->masks.at(index);
            if (!source.read_only_reason.empty()) {
                error_ = source.read_only_reason;
                continue;
            }
            auto it = painted.find(index);
            if (it == painted.end())
                it = painted.emplace(index, source).first;
            paint_refresh_disc(it->second, hit->u, hit->v, unsigned(radius_),
                               std::uint8_t(category_));
        }
        if (!painted.empty()) {
            std::map<std::size_t, Bytes> updates;
            for (auto &[index, mask] : painted)
                updates.emplace(index, std::move(mask.ids));
            edit->preview_refresh_batch(std::move(updates));
            renderer.invalidate_selection_readback();
            error_.clear();
        }
        previous_ = io.MousePos;
    } catch (const std::exception &e) {
        edit->commit();
        stroke_ = false;
        error_ = e.what();
    }
}

}
