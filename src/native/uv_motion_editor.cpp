#include "native/canvas_navigation.h"
#include "native/undo_shortcuts.h"
#include "assets/material_motion.h"
#include "native/uv_motion_editor.h"
#include "native/imgui_renderer.h"
#include "native/texture_uv_preview.h"
#include <algorithm>
#include <cmath>
namespace studio {
namespace {
constexpr const char *preview_name = "Studio UV mapping preview";
MaterialTrack *uv_track(MaterialMotion &motion, const std::string &name, unsigned unit) {
    auto found = std::find_if(motion.tracks.begin(), motion.tracks.end(), [&](auto &track) {
        return track.kind == MaterialTrack::Kind::TextureTransform && track.material == name &&
               track.slot == unit;
    });
    return found == motion.tracks.end() ? nullptr : &*found;
}
}
void UvMotionEditor::open(const MaterialSelection &selection, unsigned unit) {
    open_ = true;
    pending_ = transforming_ = false;
    identity_.clear();
    meshes_ = selection.meshes;
    if (meshes_.empty() && selection.draw >= 0)
        meshes_.insert(selection.draw);
    material_ = selection.material;
    unit_ = int(unit);
}
void UvMotionEditor::clear_preview(EnvironmentRenderer &renderer) {
    if (auto scene = preview_scene_.lock()) {
        std::erase_if(scene->material_animations, [](auto &animation) {
            return animation.name == preview_name;
        });
        renderer.refresh_materials();
    }
    preview_scene_.reset();
}
void UvMotionEditor::draw(MaterialDocument &doc, ModelDocument &preview,
                          EnvironmentRenderer &renderer, bool &playing, bool &repeat) {
    clear_preview(renderer);
    if (!open_)
        return;
    if (!identity_.empty() && identity_ != doc.identity()) {
        open_ = pending_ = transforming_ = false;
        return;
    }
    ImGui::SetNextWindowSize({700, 760}, ImGuiCond_FirstUseEver);
    bool visible = ImGui::Begin("Animate mapping", &open_);
    if (!visible || !open_) {
        ImGui::End();
        return;
    }
    if (preview.motion < 0 || std::size_t(preview.motion) >= preview.motions.size()) {
        ImGui::TextWrapped("Choose an animation in the Animation window first.");
        ImGui::End();
        return;
    }
    if (identity_.empty()) {
        playing = false;
        preview.looping_effects = false;
        preview.select_motion(preview.motion, repeat);
        renderer.playback.materials = true;
    }
    auto &scene = *preview.scene;
    if (scene.materials.empty()) {
        ImGui::TextUnformatted("This model has no materials.");
        ImGui::End();
        return;
    }
    std::set<int> candidates;
    for (int mesh : meshes_)
        if (mesh >= 0 && std::size_t(mesh) < scene.draws.size())
            candidates.insert(int(scene.draws[mesh].material));
    if (candidates.empty()) {
        for (unsigned i = 0; i < scene.materials.size(); ++i)
            candidates.insert(int(i));
    }
    if (!candidates.contains(material_))
        material_ = *candidates.begin();
    bool reset = identity_.empty() || revision_ != doc.revision() || motion_ != preview.motion;
    identity_ = doc.identity();
    revision_ = doc.revision();
    motion_ = preview.motion;
    auto motion = preview.motions[motion_].material;
    bool daily = preview.area >= 0 && preview.motions[motion_].daily;
    ImGui::TextWrapped("%s", preview.motions[motion_].name.c_str());
    ImGui::BeginDisabled(pending_ || transforming_);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##mapping-material", scene.materials[material_].name.c_str())) {
        for (int material : candidates)
            if (ImGui::Selectable(scene.materials[material].name.c_str(), material == material_)) {
                material_ = material;
                reset = true;
            }
        ImGui::EndCombo();
    }
    if (ImGui::Combo("Texture unit", &unit_, "Texture 0\0Texture 1\0Texture 2\0"))
        reset = true;
    ImGui::EndDisabled();
    auto base = scene.materials[material_].inputs[unit_];
    const auto material_name = scene.materials[material_].name;
    if (base.source > 2) {
        ImGui::TextWrapped(
            "This texture uses generated coordinates. Choose a mesh UV texture unit.");
        ImGui::End();
        return;
    }
    unsigned affected = 0, selected = 0;
    for (unsigned i = 0; i < scene.draws.size(); ++i)
        if (scene.draws[i].material == std::size_t(material_)) {
            ++affected;
            if (meshes_.contains(int(i)))
                ++selected;
        }
    ImGui::Text("%u meshes share this material (%u selected)", affected, selected);
    if (ImGui::TreeNode("Affected meshes")) {
        for (unsigned i = 0; i < scene.draws.size(); ++i)
            if (scene.draws[i].material == std::size_t(material_))
                ImGui::BulletText("%s / %u%s", scene.draws[i].mesh.c_str(), i,
                                  meshes_.contains(int(i)) ? " (selected)" : "");
        ImGui::TreePop();
    }
    bool can_unique = selected > 0 && selected < affected &&
                      (preview.area < 0 || preview.project_asset) && !preview.clothing;
    ImGui::BeginDisabled(!can_unique || pending_ || transforming_);
    if (ImGui::Button("Make material unique")) {
        try {
            std::string name;
            for (unsigned suffix = 1;; ++suffix) {
                name = material_name + "_UV" + std::to_string(suffix);
                if (std::none_of(scene.materials.begin(), scene.materials.end(), [&](auto &m) {
                        return m.name == name;
                    }))
                    break;
            }
            doc.make_material_unique(std::size_t(material_), name, meshes_);
            const bool overlays = preview.looping_effects;
            preview = doc.model;
            preview.looping_effects = overlays;
            preview.select_motion(motion_, repeat);
            renderer.set_scene(preview.scene);
            material_ =
                int(std::find_if(preview.scene->materials.begin(), preview.scene->materials.end(),
                                 [&](auto &m) {
                                     return m.name == name;
                                 }) -
                    preview.scene->materials.begin());
            revision_ = ~std::uint64_t(0);
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
        ImGui::End();
        return;
    }
    ImGui::EndDisabled();
    if (pending_ || transforming_) {
        studio::UndoShortcuts::block("material_editor");
        playing = false;
        if (daily && motion.frames > 0)
            renderer.lighting.hour = frame_ * 24.f / motion.frames;
        else
            renderer.playback.seconds = frame_ / 30.;
    }
    auto clock = motion;
    clock.looping = repeat;
    int frame =
        int(animation_frame(clock, renderer.playback.seconds, daily, renderer.lighting.hour));
    ImGui::BeginDisabled(pending_ || transforming_);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##mapping-frame", &frame, 0, int(motion.frames), "Frame %d")) {
        playing = false;
        repeat = false;
        preview.select_motion(motion_, false);
        if (daily && motion.frames > 0)
            renderer.lighting.hour = frame * 24.f / motion.frames;
        else
            renderer.playback.seconds = frame / 30.;
    }
    ImGui::EndDisabled();
    if (reset || (!pending_ && !transforming_ && frame != frame_)) {
        values_ = base.transform;
        if (auto track = uv_track(motion, material_name, unsigned(unit_)))
            for (unsigned i = 0; i < 5; ++i)
                values_[i] = track->curves[i].sample(float(frame), values_[i]);
        frame_ = frame;
        pending_ = transforming_ = false;
    }
    auto insert = [&] {
        try {
            auto next = key_material_uv_transform(doc.model.motions.at(motion_).material,
                                                  material_name, unsigned(unit_), unsigned(frame_),
                                                  values_, base.transform);
            bool overlays = preview.looping_effects;
            doc.edit_material_motion(std::size_t(motion_), next);
            preview = doc.model;
            preview.looping_effects = overlays;
            preview.select_motion(motion_, repeat);
            renderer.set_scene(preview.scene);
            renderer.playback.materials = true;
            pending_ = false;
            keyed_ = std::pair{material_name, unit_};
            revision_ = doc.revision();
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    };
    ImGui::BeginDisabled(transforming_);
    ImGui::Checkbox("Auto key", &auto_key_);
    ImGui::SameLine();
    bool insert_requested = ImGui::Button("Insert key");
    ImGui::SameLine();
    if (ImGui::Button("Discard preview")) {
        pending_ = false;
        frame_ = -1;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(transforming_);
    ImGui::RadioButton("Move (G)", &operation_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Rotate (R)", &operation_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Scale (S)", &operation_, 2);
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        pending_
            ? "Unkeyed preview — Insert key or Discard preview."
            : "G: move | R: rotate | S: scale | X/Y: constrain | Enter: confirm | Esc: cancel");
    bool fields_changed = false, field_finished = false;
    ImGui::BeginDisabled(transforming_);
    ImGui::SetNextItemWidth(170);
    fields_changed |= ImGui::DragFloat2("Scale", values_.data(), .01f);
    field_finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(170);
    float degrees = values_[2] * 57.29577951f;
    if (ImGui::DragFloat("Rotation (degrees)", &degrees, .5f)) {
        values_[2] = degrees / 57.29577951f;
        fields_changed = true;
    }
    field_finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(170);
    fields_changed |= ImGui::DragFloat2("Offset", &values_[3], .005f);
    field_finished |= fields_changed && !ImGui::IsMouseDown(0);
    field_finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::EndDisabled();
    if (fields_changed) {
        pending_ = true;
        playing = false;
    }
    if (ImGui::Button("Frame image (F)")) {
        zoom_ = 1;
        pan_ = {};
    }
    const auto origin = ImGui::GetCursorScreenPos();
    float size = std::max(
        64.f, std::min(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - 25));
    ImGui::InvisibleButton("##animated-uv-canvas", {size, size},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    auto &io = ImGui::GetIO();
    if (!transforming_)
        navigate_canvas(origin, {size, size}, hovered, zoom_, pan_);
    if (hovered && !io.WantTextInput && !io.KeyCtrl && !transforming_) {
        const ImGuiKey keys[] = {ImGuiKey_G, ImGuiKey_R, ImGuiKey_S};
        for (int op = 0; op < 3; ++op)
            if (ImGui::IsKeyPressed(keys[op], false)) {
                mouse_drag_ = false;
                operation_ = op;
                before_ = values_;
                start_ = io.MousePos;
                transforming_ = true;
                playing = false;
                axis_ = -1;
            }
    }
    if (hovered && !transforming_ && ImGui::IsMouseClicked(0)) {
        mouse_drag_ = true;
        before_ = values_;
        start_ = io.MousePos;
        transforming_ = true;
        playing = false;
        axis_ = -1;
    }
    bool confirmed = false;
    if (transforming_) {
        if (ImGui::IsKeyPressed(ImGuiKey_X, false))
            axis_ = axis_ == 0 ? -1 : 0;
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            axis_ = axis_ == 1 ? -1 : 1;

        values_ = before_;
        float dx = (io.MousePos.x - start_.x) / (size * zoom_),
              dy = (io.MousePos.y - start_.y) / (size * zoom_);
        if (operation_ == 0) {
            float c = std::cos(before_[2]), s = std::sin(before_[2]);
            float u = axis_ == 1 ? 0 : dx / std::max(std::abs(before_[0]), .0001f);
            float v = axis_ == 0 ? 0 : dy / std::max(std::abs(before_[1]), .0001f);
            values_[3] -= c * u + s * v;
            values_[4] -= -s * u + c * v;
        } else if (operation_ == 1)
            values_[2] += (dx - dy) * 6.283185307f;
        else {
            float factor = std::exp(std::clamp((dx - dy) * 3, -6.f, 6.f));
            if (axis_ != 1)
                values_[0] *= factor;
            if (axis_ != 0)
                values_[1] *= factor;
        }
        if (io.KeyCtrl) {
            if (operation_ == 1)
                values_[2] =
                    before_[2] + std::round((values_[2] - before_[2]) / .261799388f) * .261799388f;
            else {
                unsigned first = operation_ == 0 ? 3 : 0;
                float step = operation_ == 0 ? .01f : .1f;
                for (unsigned i = first; i < first + 2; ++i)
                    values_[i] = before_[i] + std::round((values_[i] - before_[i]) / step) * step;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(1) || io.AppFocusLost) {
            values_ = before_;
            transforming_ = false;
        } else if (mouse_drag_
                       ? ImGui::IsMouseReleased(0)
                       : (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsMouseClicked(0))) {
            transforming_ = false;
            pending_ = true;
            confirmed = true;
        }
    }
    if ((auto_key_ && pending_ && (confirmed || field_finished)) || insert_requested) {
        insert();
        ImGui::End();
        return;
    }
    auto input = base;
    input.transform = values_;
    update_texture_transform(input);
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size, origin.y + size}, true);
    float image_size = size * zoom_;
    ImVec2 top{origin.x + (size - image_size) * .5f + pan_.x,
               origin.y + (size - image_size) * .5f + pan_.y};
    draw_texture_checkerboard(origin, {size, size});
    auto texture = renderer.texture(scene.materials[material_].texture_inputs[unit_]);
    if (bgfx::isValid(texture))
        draw->AddImage(ImTextureID(ImGuiRenderer::image_id(texture, true)), top,
                       {top.x + image_size, top.y + image_size});
    draw_texture_uvs(scene, std::size_t(material_), input, true, top, image_size);
    draw->PopClipRect();
    if (pending_ || transforming_) {
        MaterialAnimation overlay;
        overlay.name = preview_name;
        overlay.motion.frames = motion.frames;
        MaterialTrack track;
        track.material = material_name;
        track.slot = unsigned(unit_);
        for (unsigned i = 0; i < 5; ++i)
            track.curves[i].keys.push_back({0, values_[i], 0});
        overlay.motion.tracks.push_back(track);
        for (unsigned i = 0; i < scene.materials.size(); ++i)
            if (scene.materials[i].name == material_name)
                overlay.bindings.push_back({0, i});
        scene.material_animations.push_back(std::move(overlay));
        preview_scene_ = preview.scene;
        renderer.playback.materials = true;
    }
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::End();
}
}
