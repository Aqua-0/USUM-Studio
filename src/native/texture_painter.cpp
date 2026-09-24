#include "native/canvas_navigation.h"
#include "native/undo_shortcuts.h"
#include "native/texture_painter.h"
#include "native/imgui_renderer.h"
#include "scene/texture_channels.h"
#include "scene/texture_paint.h"
#include "scene/texture_uv.h"
#include <algorithm>
#include <cmath>
namespace studio {
TexturePainter::~TexturePainter() {
    close();
}
void TexturePainter::close() {
    visible_ = stroke_ = changed_ = false;
    if (bgfx::isValid(preview_))
        bgfx::destroy(preview_);
    preview_ = BGFX_INVALID_HANDLE;
    pixels_ = {};
    before_ = {};
    revision_ = ~std::uint64_t(0);
}
void TexturePainter::open(const std::string &texture) {
    if (texture_ != texture || !visible_) {
        close();
        texture_ = texture;
        zoom_ = 1;
        pan_ = {};
        error_.clear();
    }
    visible_ = focus_ = true;
}
void TexturePainter::upload() {
    auto image = channel_ >= 2 ? texture_channel_preview(pixels_, unsigned(channel_ - 2)) : pixels_;
    if (bgfx::isValid(preview_))
        bgfx::destroy(preview_);
    preview_ =
        bgfx::createTexture2D(image.width, image.height, false, 1, bgfx::TextureFormat::RGBA8,
                              BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                              bgfx::copy(image.rgba.data(), std::uint32_t(image.rgba.size())));
    upload_ = false;
}
void TexturePainter::draw(MaterialDocument &doc, MaterialSelection &selection, bool editable) {
    if (!visible_)
        return;
    auto found = doc.model.scene->textures.find(texture_);
    if (found == doc.model.scene->textures.end()) {
        close();
        return;
    }
    if (revision_ != doc.texture_revision()) {
        pixels_ = found->second;
        revision_ = doc.texture_revision();
        stroke_ = changed_ = false;
        upload_ = true;
    }
    ImGui::SetNextWindowSize({820, 760}, ImGuiCond_FirstUseEver);
    if (focus_) {
        ImGui::SetNextWindowFocus();
        focus_ = false;
    }
    bool expanded = ImGui::Begin("Texture painter", &visible_, ImGuiWindowFlags_NoDocking);
    auto &io = ImGui::GetIO();
    if (stroke_ && (!io.MouseDown[0] || !editable || !expanded || !visible_ ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        bool cancel = !editable || ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (changed_ && !cancel) {
            try {
                doc.replace_texture(texture_, pixels_, TextureFormat::RGBA8);
                revision_ = doc.texture_revision();
                pixels_ = doc.model.scene->textures.at(texture_);
                error_.clear();
            } catch (const std::exception &e) {
                error_ = e.what();
                pixels_ = before_;
            }
        } else if (cancel)
            pixels_ = before_;
        stroke_ = changed_ = false;
        before_ = {};
        upload_ = true;
    }
    if (!expanded || !visible_) {
        ImGui::End();
        if (!visible_)
            close();
        return;
    }
    ImGui::Text("%s | %u x %u", texture_.c_str(), pixels_.width, pixels_.height);
    ImGui::TextDisabled("Strokes update the model on release. Save Project keeps your edits.");
    ImGui::BeginDisabled(!editable || stroke_);
    ImGui::RadioButton("Brush", &tool_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Eraser", &tool_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Picker", &tool_, 2);
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("Edit channel", &channel_, "RGBA\0RGB\0Red\0Green\0Blue\0Alpha\0"))
        upload_ = true;
    if (channel_ >= 2) {
        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("Value", &color_[channel_ - 2], 0, 1, "%.3f");
    } else
        ImGui::ColorEdit4("Color", color_.data());
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("Brush radius (pixels)", &radius_, .5f, 128.f, "%.1f");
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("Strength", &opacity_, 0, 1, "%.2f");
    if (tool_ == 1)
        ImGui::TextDisabled(channel_ == 0 ? "Eraser removes alpha and keeps RGB."
                                          : "Eraser clears only the selected channels to zero.");
    if (channel_ == 5 || (channel_ == 0 && tool_ == 1))
        ImGui::TextDisabled(
            "3D transparency also needs texture alpha in the material and Alpha blend.");
    ImGui::BeginDisabled(!doc.can_undo());
    if (studio::UndoShortcuts::button("material_editor", "Undo"))
        doc.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!doc.can_redo());
    if (studio::UndoShortcuts::button("material_editor", "Redo"))
        doc.redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Frame image (F)")) {
        zoom_ = 1;
        pan_ = {};
    }
    ImGui::EndDisabled();
    ImGui::Checkbox("UV overlay", &overlay_);
    if (overlay_) {
        ImGui::SameLine();
        ImGui::Checkbox("Selected meshes only", &selected_uvs_);
        if (ImGui::TreeNode("Meshes using this texture")) {
            ImGui::TextDisabled("Click to select; Ctrl+click adds or removes meshes.");
            ImGui::BeginChild("##paint-meshes", {0, 120}, ImGuiChildFlags_Borders);
            const auto &scene = *doc.model.scene;
            for (unsigned i = 0; i < scene.draws.size(); ++i) {
                const auto &mesh = scene.draws[i];
                const auto &material = scene.materials[mesh.material];
                if (std::find(material.texture_inputs.begin(), material.texture_inputs.end(),
                              texture_) == material.texture_inputs.end())
                    continue;
                ImGui::PushID(int(i));
                if (ImGui::Selectable((mesh.mesh + " / " + std::to_string(i)).c_str(),
                                      selection.mesh_selected(int(i)))) {
                    selection.select_mesh(int(i), int(mesh.material), io.KeyCtrl);
                    if (selection.draw >= 0)
                        selection.material = int(scene.draws[selection.draw].material);
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Controls")) {
        ImGui::TextWrapped("Wheel: zoom | Middle drag: pan | F: frame image\nAlt+click: pick | "
                           "Esc: cancel stroke");
        ImGui::TextWrapped("Shared textures change together. Painting uses RGBA8.");
    }
    if (!editable)
        ImGui::TextUnformatted("Finish the current operation before painting.");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    auto origin = ImGui::GetCursorScreenPos();
    ImVec2 area{std::max(32.f, ImGui::GetContentRegionAvail().x),
                std::max(96.f, ImGui::GetContentRegionAvail().y)};
    ImGui::InvisibleButton("##paint-canvas", area,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    float base = std::min(area.x / pixels_.width, area.y / pixels_.height);
    if (!stroke_)
        navigate_canvas(origin, area, hovered, zoom_, pan_);
    float scale = base * zoom_;
    ImVec2 size{pixels_.width * scale, pixels_.height * scale};
    ImVec2 top{origin.x + (area.x - size.x) * .5f + pan_.x,
               origin.y + (area.y - size.y) * .5f + pan_.y};
    ImVec2 mouse{(io.MousePos.x - top.x) / scale, (io.MousePos.y - top.y) / scale};
    bool inside = hovered && mouse.x >= 0 && mouse.y >= 0 && mouse.x < pixels_.width &&
                  mouse.y < pixels_.height;
    bool started = false;
    if (editable && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (tool_ == 2 || io.KeyAlt) {
            auto offset = (std::size_t(mouse.y) * pixels_.width + unsigned(mouse.x)) * 4;
            for (unsigned i = 0; i < 4; ++i)
                color_[i] = pixels_.rgba[offset + i] / 255.f;
        } else {
            before_ = pixels_;
            previous_ = mouse;
            stroke_ = true;
            changed_ = false;
            started = true;
        }
    }
    if (stroke_ && io.MouseDown[0] && editable) {
        if (hovered && (started || io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
            unsigned mask = channel_ == 0 ? 15 : channel_ == 1 ? 7 : 1u << (channel_ - 2);
            bool painted =
                paint_texture_segment(pixels_, previous_.x, previous_.y, mouse.x, mouse.y, radius_,
                                      color_, mask, opacity_, tool_ == 1);
            changed_ |= painted;
            upload_ |= painted;
        }
        previous_ = mouse;
    }
    if (upload_)
        upload();
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + area.x, origin.y + area.y}, true);
    draw->AddRectFilled(origin, {origin.x + area.x, origin.y + area.y}, IM_COL32(25, 30, 35, 255));
    auto left = std::max(origin.x, top.x), right = std::min(origin.x + area.x, top.x + size.x);
    auto upper = std::max(origin.y, top.y), bottom = std::min(origin.y + area.y, top.y + size.y);
    for (float y = upper; y < bottom; y += 12)
        for (float x = left; x < right; x += 12) {
            int shade = (int((x - left) / 12) + int((y - upper) / 12)) % 2 ? 80 : 120;
            draw->AddRectFilled({x, y}, {std::min(x + 12, right), std::min(y + 12, bottom)},
                                IM_COL32(shade, shade, shade, 255));
        }
    if (bgfx::isValid(preview_))
        draw->AddImage(ImTextureID(ImGuiRenderer::image_id(preview_, channel_ == 0, true)), top,
                       {top.x + size.x, top.y + size.y});
    if (overlay_) {
        const auto &scene = *doc.model.scene;
        draw->PushClipRect(top, {top.x + size.x, top.y + size.y}, true);
        for (unsigned i = 0; i < scene.draws.size(); ++i) {
            const auto &mesh = scene.draws[i];
            if (selected_uvs_ && !selection.mesh_selected(int(i)))
                continue;
            const auto &material = scene.materials[mesh.material];
            for (unsigned unit = 0; unit < 3; ++unit) {
                if (material.texture_inputs[unit] != texture_)
                    continue;
                const auto &input = material.inputs[unit];
                if (input.source > 2)
                    continue;
                auto point = [&](const SceneVertex &v) {
                    float u = input.source == 0 ? v.u : input.source == 1 ? v.u1 : v.u2;
                    float w = input.source == 0 ? v.v : input.source == 1 ? v.v1 : v.v2;
                    return std::array<float, 2>{
                        input.row_u[0] * u + input.row_u[1] * w + input.row_u[2],
                        input.row_v[0] * u + input.row_v[1] * w + input.row_v[2]};
                };
                for (std::size_t j = 0; j + 2 < mesh.indices.size(); j += 3)
                    for (unsigned k = 0; k < 3; ++k)
                        for (auto edge : texture_uv_segments(
                                 point(mesh.vertices[mesh.indices[j + k]]),
                                 point(mesh.vertices[mesh.indices[j + (k + 1) % 3]]), input.wrap_u,
                                 input.wrap_v)) {
                            ImVec2 a{top.x + edge[0][0] * size.x, top.y + edge[0][1] * size.y},
                                b{top.x + edge[1][0] * size.x, top.y + edge[1][1] * size.y};
                            draw->AddLine(a, b, IM_COL32(0, 0, 0, 210), 2);
                            draw->AddLine(a, b,
                                          selection.mesh_selected(int(i))
                                              ? IM_COL32(255, 185, 65, 230)
                                              : IM_COL32(80, 220, 240, 200));
                        }
            }
        }
        draw->PopClipRect();
    }
    if (hovered && tool_ != 2) {
        draw->AddCircle(io.MousePos, radius_ * scale, IM_COL32(0, 0, 0, 255), 32, 3);
        draw->AddCircle(io.MousePos, radius_ * scale, IM_COL32(255, 255, 255, 255), 32, 1);
    }
    draw->PopClipRect();
    ImGui::End();
}
}
