#include "native/tutorial_widgets.h"
#include "native/map_cursor.h"
#include "scene/refresh_surface.h"
#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace studio {
void MapCursor::draw(const Environment *scene, ViewportCamera &camera) {
    if (scene_ != scene) {
        scene_ = scene;
        enabled_ = placing_ = blocked_ = false;
        axis_ = -1;
        start_.reset();
        position_ = camera.target;
        message_.clear();
    }
    ImGui::Begin("Spatial");
    if (studio::TutorialWidgets::Checkbox("map_cursor", "3D cursor and ruler", &enabled_)) {
        placing_ = false;
        if (axis_ >= 0)
            position_ = drag_start_;
        axis_ = -1;
    }
    ImGui::End();
    if (!enabled_)
        return;
    ImGui::SetNextWindowSize({420, 330}, ImGuiCond_FirstUseEver);
    ImGui::Begin("3D cursor and ruler");
    ImGui::BeginDisabled(axis_ >= 0);
    auto next = position_;
    if (ImGui::InputFloat3("World XYZ", next.data(), "%.3f")) {
        if (std::all_of(next.begin(), next.end(), [](float v) {
                return std::isfinite(v) && std::abs(v) < 1e8f;
            }))
            position_ = next;
    }
    if (studio::TutorialWidgets::Button("map_cursor",
                                        placing_ ? "Cancel surface placement" : "Place on surface"))
        placing_ = !placing_;
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("map_cursor", "At view center"))
        position_ = camera.target;
    if (studio::TutorialWidgets::Button("map_cursor", "Frame cursor"))
        camera.target = position_;
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("map_cursor", "Copy XYZ")) {
        char text[160];
        std::snprintf(text, sizeof(text), "%.3f, %.3f, %.3f", position_[0], position_[1],
                      position_[2]);
        ImGui::SetClipboardText(text);
    }
    ImGui::TextWrapped("Place on a surface, then drag XYZ arrows to move through empty space. "
                       "Shift-drag orbits; Escape cancels a drag or placement.");
    ImGui::Separator();
    if (studio::TutorialWidgets::Button("map_cursor",
                                        start_ ? "Set new start here" : "Mark measurement start"))
        start_ = position_;
    if (start_) {
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("map_cursor", "Clear measurement"))
            start_.reset();
    }
    if (start_) {
        double x = double(position_[0]) - (*start_)[0], y = double(position_[1]) - (*start_)[1],
               z = double(position_[2]) - (*start_)[2];
        double horizontal = std::hypot(x, z), distance = std::hypot(horizontal, y);
        ImGui::Text("Start: %.3f, %.3f, %.3f", (*start_)[0], (*start_)[1], (*start_)[2]);
        ImGui::Text("Distance: %.3f world units", distance);
        ImGui::Text("Horizontal: %.3f   Height difference: %+.3f", horizontal, y);
        ImGui::Text("Delta XYZ: %+.3f, %+.3f, %+.3f", x, y, z);
        if (studio::TutorialWidgets::Button("map_cursor", "Copy measurement")) {
            char text[320];
            std::snprintf(text, sizeof(text),
                          "A: %.3f, %.3f, %.3f\nB: %.3f, %.3f, %.3f\nDistance: %.3f world "
                          "units\nDelta XYZ: %+.3f, %+.3f, %+.3f",
                          (*start_)[0], (*start_)[1], (*start_)[2], position_[0], position_[1],
                          position_[2], distance, x, y, z);
            ImGui::SetClipboardText(text);
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("map_cursor", "Swap endpoints"))
            std::swap(position_, *start_);
    }
    if (!message_.empty())
        ImGui::TextWrapped("%s", message_.c_str());
    ImGui::EndDisabled();
    ImGui::End();
}
bool MapCursor::viewport(const Environment &scene, EnvironmentRenderer &renderer, const float *view,
                         const float *projection, ImVec2 origin, ImVec2 size, bool hovered,
                         bool cutaway) {
    if (!enabled_ || renderer.player.active)
        return false;
    auto &io = ImGui::GetIO();
    if (blocked_) {
        blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        placing_ = false;
        if (axis_ >= 0) {
            position_ = drag_start_;
            axis_ = -1;
            blocked_ = true;
        }
    }
    auto project = [&](SpatialPoint p, ImVec2 &out) {
        float a[4]{}, b[4]{};
        for (unsigned i = 0; i < 4; ++i)
            a[i] = view[i] * p[0] + view[4 + i] * p[1] + view[8 + i] * p[2] + view[12 + i];
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 0; j < 4; ++j)
                b[i] += projection[j * 4 + i] * a[j];
        if (b[3] <= .001f)
            return false;
        out = {origin.x + (b[0] / b[3] * .5f + .5f) * size.x,
               origin.y + (.5f - b[1] / b[3] * .5f) * size.y};
        return true;
    };
    bool modifier = io.KeyShift || io.KeyCtrl || io.KeyAlt;
    if (placing_ && hovered && !modifier && ImGui::IsMouseClicked(0)) {
        try {
            require(!cutaway, "Turn off Cutaway before placing the cursor on a surface.");
            require(renderer.geometry_enabled, "Show map geometry before placing the cursor.");
            float iv[16], ip[16];
            bx::mtxInverse(iv, view);
            bx::mtxInverse(ip, projection);
            float x = (io.MousePos.x - origin.x) / size.x * 2 - 1,
                  y = 1 - (io.MousePos.y - origin.y) / size.y * 2;
            auto unproject = [&](float z) {
                float p[4]{x, y, z, 1}, a[4]{}, b[4]{};
                for (unsigned i = 0; i < 4; ++i)
                    for (unsigned j = 0; j < 4; ++j)
                        a[i] += ip[j * 4 + i] * p[j];
                for (unsigned i = 0; i < 4; ++i)
                    for (unsigned j = 0; j < 4; ++j)
                        b[i] += iv[j * 4 + i] * a[j];
                require(std::abs(b[3]) > 1e-8f, "Invalid cursor projection");
                return SpatialPoint{b[0] / b[3], b[1] / b[3], b[2] / b[3]};
            };
            auto near = unproject(bgfx::getCaps()->homogeneousDepth ? -1.f : 0.f),
                 far = unproject(1);
            SpatialPoint direction;
            for (unsigned k = 0; k < 3; ++k)
                direction[k] = far[k] - near[k];
            auto visible = evaluate_visibility(scene, renderer.playback.seconds,
                                               renderer.lighting.hour, renderer.visibility_enabled);
            for (unsigned i = 0; i < visible.size(); ++i) {
                auto &d = scene.draws[i];
                visible[i] =
                    visible[i] && renderer.draw_visible(i) && d.sky_part < 0 && !d.weather_mask &&
                    (d.player < 0 ||
                     (renderer.player.active && d.player == int(renderer.player.appearance))) &&
                    (!d.character || (renderer.characters_enabled &&
                                      (!d.conditional || renderer.conditional_characters)));
            }
            auto poses = evaluate_scene_poses(
                scene.skeletons, renderer.player, renderer.playback.seconds, renderer.lighting.hour,
                renderer.playback.enabled && renderer.playback.skeletal);
            auto hit = RefreshSurface(scene, {}, "", poses, visible).hit(near, direction);
            require(hit && hit->distance <= 1,
                    "No model surface here. Use the arrows or enter XYZ for empty space.");
            for (unsigned k = 0; k < 3; ++k)
                position_[k] = near[k] + direction[k] * hit->distance;
            placing_ = false;
            blocked_ = true;
            message_.clear();
        } catch (const std::exception &e) {
            message_ = e.what();
        }
    }
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    ImVec2 center;
    int hot = -1;
    float nearest = 9;
    ImVec2 directions[3]{};
    float depth =
        -(view[2] * position_[0] + view[6] * position_[1] + view[10] * position_[2] + view[14]);
    float length = axis_ >= 0 ? length_ : std::clamp(depth * .1f, 1.f, 2000.f);
    if (project(position_, center)) {
        const ImU32 colors[]{IM_COL32(255, 95, 85, 255), IM_COL32(110, 240, 130, 255),
                             IM_COL32(100, 160, 255, 255)};
        for (unsigned axis = 0; axis < 3; ++axis) {
            auto p = position_;
            p[axis] += length;
            ImVec2 end;
            if (!project(p, end))
                continue;
            float x = end.x - center.x, y = end.y - center.y, n = std::hypot(x, y);
            if (n < 12)
                continue;
            directions[axis] = {x, y};
            float t = std::clamp(((io.MousePos.x - center.x) * x + (io.MousePos.y - center.y) * y) /
                                     (n * n),
                                 0.f, 1.f);
            float distance =
                std::hypot(io.MousePos.x - center.x - t * x, io.MousePos.y - center.y - t * y);
            if (distance < nearest) {
                nearest = distance;
                hot = int(axis);
            }
            auto color = colors[axis];
            draw->AddLine(center, end, color, 2);
            x /= n;
            y /= n;
            draw->AddTriangleFilled(end, {end.x - 12 * x + 5 * y, end.y - 12 * y - 5 * x},
                                    {end.x - 12 * x - 5 * y, end.y - 12 * y + 5 * x}, color);
            draw->AddText({end.x + 6, end.y - 6}, color, axis == 0 ? "X" : axis == 1 ? "Y" : "Z");
        }
        draw->AddCircle(center, 7, IM_COL32(0, 0, 0, 255), 16, 3);
        draw->AddCircle(center, 5, IM_COL32(255, 255, 255, 255), 16, 2);
        draw->AddLine({center.x - 10, center.y}, {center.x + 10, center.y},
                      IM_COL32(255, 255, 255, 255));
        draw->AddLine({center.x, center.y - 10}, {center.x, center.y + 10},
                      IM_COL32(255, 255, 255, 255));
        if (start_) {
            ImVec2 a;
            if (project(*start_, a)) {
                auto color = IM_COL32(255, 230, 100, 255);
                draw->AddLine(a, center, IM_COL32(0, 0, 0, 255), 5);
                draw->AddLine(a, center, color, 2);
                draw->AddCircleFilled(a, 5, color);
                double x = double(position_[0]) - (*start_)[0],
                       y = double(position_[1]) - (*start_)[1],
                       z = double(position_[2]) - (*start_)[2];
                char label[80];
                std::snprintf(label, sizeof(label), "%.3f units", std::hypot(std::hypot(x, z), y));
                draw->AddText({(a.x + center.x) * .5f + 6, (a.y + center.y) * .5f - 16}, color,
                              label);
            }
        }
    }
    draw->PopClipRect();
    bool handle = hovered && !modifier && hot >= 0 && !placing_;
    if (axis_ < 0 && handle && ImGui::IsMouseClicked(0)) {
        axis_ = hot;
        drag_start_ = position_;
        mouse_start_ = io.MousePos;
        direction_ = directions[hot];
        length_ = length;
    }
    bool captured = blocked_ || axis_ >= 0 || handle || (placing_ && hovered && !modifier);
    if (axis_ >= 0) {
        float square = direction_.x * direction_.x + direction_.y * direction_.y;
        position_ = drag_start_;
        position_[axis_] += ((io.MousePos.x - mouse_start_.x) * direction_.x +
                             (io.MousePos.y - mouse_start_.y) * direction_.y) *
                            length_ / std::max(square, .01f);
        if (!ImGui::IsMouseDown(0))
            axis_ = -1;
    }
    return captured;
}
}
