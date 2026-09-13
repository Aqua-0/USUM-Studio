#include "native/tutorial_widgets.h"
#include "native/refresh_feeding_preview.h"
#include "native/imgui_renderer.h"
#include <imgui.h>
#include <bx/math.h>
#include <algorithm>
#include <cmath>
namespace studio {
void RefreshFeedingPreview::reset() {
    area_ = {};
    active = camera_view = false;
    camera_kind_ = 0;
    held_ = false;
    paused_ = false;
    session_.reset();
    model_.reset();
    source_.reset();
    error_.clear();
    food_x_ = 280;
    food_y_ = 65;
}
void RefreshFeedingPreview::camera_controls(const RefreshFeedingData &data, MaterialDocument *edit,
                                            unsigned slot) {
    ImGui::TextWrapped("Position moves the camera; look-at selects where it points. Save edits "
                       "stores these values; Write game files exports them.");
    if (data.camera_default)
        ImGui::TextWrapped(
            "No source camera row. This variant uses the default camera; no row will be added.");
    else {
        auto row = refresh_feeding_record(data.camera_original, data.camera_row, 126);
        ImGui::TextWrapped("Camera source: species %u, form %u, sex %u. Variants using this row "
                           "share camera edits.",
                           unsigned(u16(row, 0)), unsigned(row[2]), unsigned(row[3]));
    }
    if (!edit)
        ImGui::TextWrapped("Send to Studio to edit this Refresh camera.");
    auto camera = data.cameras[slot];
    bool changed = false, commit = false;
    ImGui::BeginDisabled(!edit || data.camera_default);
    ImGui::TextUnformatted("Position (X / Y / Z)");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::DragInt3("##feeding-camera-eye", camera.position.data(), 1, -32768, 32767,
                               "%d", ImGuiSliderFlags_AlwaysClamp);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextUnformatted("Look-at (X / Y / Z)");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::DragInt3("##feeding-camera-focus", camera.focus.data(), 1, -32768, 32767,
                               "%d", ImGuiSliderFlags_AlwaysClamp);
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    if (changed && edit)
        try {
            edit->preview_refresh_camera(slot, camera);
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    if (commit && edit)
        edit->commit();
    if (studio::TutorialWidgets::Button("refresh_feeding_preview",
                                        slot == 9 ? "Reset source feeding camera"
                                                  : "Reset source camera") &&
        edit)
        edit->reset_refresh_camera(slot);
    ImGui::EndDisabled();
}
void RefreshFeedingPreview::controls(const ModelDocument &document, MaterialDocument *edit) {
    auto data = edit ? edit->model.refresh_feeding : document.refresh_feeding;
    if (!data) {
        ImGui::TextWrapped("Feeding data unavailable: %s", document.feeding_error.c_str());
        return;
    }
    if (camera_view) {
        ImGui::TextUnformatted("Normal / petting cameras");
        ImGui::TextWrapped(
            "The normal view uses whole-body. Petting can move closer or blend whole-body and "
            "close-up. This previews framing, not petting reactions or camera transitions.");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##refresh-camera-kind", &camera_kind_,
                     "Whole-body / normal\0Close-up / petting\0Chest-up / blended preview\0");
        if (camera_kind_ == 2) {
            ImGui::TextWrapped("The game averages whole-body and close-up for this view. Edit "
                               "those two stored cameras to change it.");
            for (unsigned point = 0; point < 2; ++point) {
                auto &a = point ? data->cameras[1].focus : data->cameras[1].position;
                auto &b = point ? data->cameras[2].focus : data->cameras[2].position;
                ImGui::TextWrapped("%s: %.1f, %.1f, %.1f", point ? "Look-at" : "Position",
                                   (a[0] + b[0]) * .5f, (a[1] + b[1]) * .5f, (a[2] + b[2]) * .5f);
            }
        } else
            camera_controls(*data, edit, camera_kind_ == 1 ? 2 : 1);
        if (edit) {
            ImGui::BeginDisabled(!edit->can_undo());
            if (studio::TutorialWidgets::Button("refresh_feeding_preview", "Undo camera / edits"))
                edit->undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!edit->can_redo());
            if (studio::TutorialWidgets::Button("refresh_feeding_preview", "Redo"))
                edit->redo();
            ImGui::EndDisabled();
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        return;
    }
    ImGui::TextUnformatted("Interactive feeding preview");
    ImGui::TextWrapped(
        "Drag the bean into the lower feeding area and hold it through the eating motions.");
    ImGui::SeparatorText("Feeding parameters");
    if (data->available) {
        ImGui::TextWrapped(
            "Source: species %u, form %u, sex %u. Variants using this row share these edits.",
            data->species, data->form, data->sex);
        if (!edit)
            ImGui::TextWrapped("Send to Studio to edit and save these values.");
        auto p = data->parameters;
        bool changed = false, commit = false;
        ImGui::BeginDisabled(!edit);
        auto input = [&](const char *label, int &value, int low, int high) {
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-1);
            ImGui::PushID(label);
            if (ImGui::InputInt("##value", &value)) {
                value = std::clamp(value, low, high);
                changed = true;
            }
            if (edit && ImGui::IsItemDeactivatedAfterEdit())
                commit = true;
            ImGui::PopID();
        };
        input("Eating loops per bean", p.animation_count, 1, 255);
        input("Eating head angle (degrees)", p.head_angle, -32768, 32767);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Adjusts the game's procedural head tilt toward food. Does not move the feeding "
                "area. Saved for the game; not yet applied in this preview.");
        input("Stored food distance", p.distance, -32768, 32767);
        input("Stored food scale (hundredths)", p.scale_percent, 0, 65535);
        if (changed && edit)
            try {
                edit->preview_feeding(p);
                error_.clear();
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        if (commit && edit)
            edit->commit();
        if (studio::TutorialWidgets::Button("refresh_feeding_preview",
                                            "Reset feeding parameters") &&
            edit)
            edit->reset_feeding();
        ImGui::EndDisabled();
        if (edit) {
            ImGui::BeginDisabled(!edit->can_undo());
            if (studio::TutorialWidgets::Button("refresh_feeding_preview", "Undo feeding / edits"))
                edit->undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!edit->can_redo());
            if (studio::TutorialWidgets::Button("refresh_feeding_preview", "Redo"))
                edit->redo();
            ImGui::EndDisabled();
        }
    } else
        ImGui::TextWrapped("No source row for this species. Preview uses runtime defaults; no "
                           "parameter row will be added.");
    ImGui::TextWrapped(
        "Save edits stores the parameters. Write game files exports them with your other edits.");
    ImGui::SeparatorText("Preview controls");
    studio::TutorialWidgets::Checkbox("refresh_feeding_preview", "Show feeding area", &show_zone_);
    studio::TutorialWidgets::Checkbox("refresh_feeding_preview", "Use distance / scale overrides",
                                      &overrides_);
    ImGui::TextWrapped(
        "The inspected runtime uses distance 130 and scale 1.0 instead of the stored values.");
    ImGui::TextUnformatted("Starting fullness");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##feeding-fullness", &initial_fullness_, 0, 255)) {
        session_.reset(initial_fullness_);
        held_ = false;
    }
    if (studio::TutorialWidgets::Button("refresh_feeding_preview", "Reset test")) {
        session_.reset(initial_fullness_);
        held_ = false;
        food_x_ = 280;
        food_y_ = 65;
    }
    if (studio::TutorialWidgets::TreeNode("refresh_feeding_preview", "Feeding area bounds")) {
        ImGui::TextWrapped("Preview only. The game's feeding rectangle is hardcoded; these bounds "
                           "are not exported.");
        ImGui::TextUnformatted("Top-left (touch pixels)");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat2("##feeding-area-position", area_.position.data(), 1, 0, 319, "%.0f",
                              ImGuiSliderFlags_AlwaysClamp)) {
            area_.position[1] = std::min(area_.position[1], 239.f);
            area_.size[0] = std::min(area_.size[0], 320 - area_.position[0]);
            area_.size[1] = std::min(area_.size[1], 240 - area_.position[1]);
        }
        ImGui::TextUnformatted("Width / height (touch pixels)");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat2("##feeding-area-size", area_.size.data(), 1, 1, 320, "%.0f",
                              ImGuiSliderFlags_AlwaysClamp)) {
            area_.size[0] = std::min(area_.size[0], 320 - area_.position[0]);
            area_.size[1] = std::min(area_.size[1], 240 - area_.position[1]);
        }
        if (studio::TutorialWidgets::Button("refresh_feeding_preview", "Reset game feeding area"))
            area_ = {};
        ImGui::TreePop();
    }
    if (studio::TutorialWidgets::TreeNode("refresh_feeding_preview", "Feeding camera (saved)")) {
        camera_controls(*data, edit, 9);
        ImGui::TreePop();
    }
    if (studio::TutorialWidgets::TreeNode("refresh_feeding_preview", "Preview coverage")) {
        ImGui::TextWrapped(
            "Uses source feeding camera, 320 x 240 touch coordinates, fullness refusal, and eating "
            "start / loop / finish motions. Bean graphic, lighting and interrupted feeding are "
            "simplified. Procedural head tracking, facial overrides, dual-screen transitions and "
            "near-full behavior are not yet replicated. The head angle is saved for the game but "
            "does not alter this preview.");
        ImGui::TreePop();
    }
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
}
void RefreshFeedingPreview::viewport(const ModelDocument &document,
                                     const EnvironmentRenderer &original, std::uint64_t revision) {
    try {
        if (!renderer_) {
            renderer_ = std::make_unique<EnvironmentRenderer>(shaders_);
            renderer_->highlight_object = false;
            renderer_->fog_enabled = false;
            renderer_->sky_enabled = false;
        }
        if (source_ != document.scene) {
            source_ = document.scene;
            model_ = std::make_unique<ModelDocument>(document);
            model_->scene = std::make_shared<Environment>(*document.scene);
            model_->select_motion(-1);
            renderer_->set_scene(model_->scene);
            session_.reset(initial_fullness_);
            held_ = false;
        }
        if (revision_ != revision) {
            revision_ = revision;
            model_->scene->materials = document.scene->materials;
            renderer_->refresh_materials();
            if (model_->scene->texture_overrides != document.scene->texture_overrides) {
                model_->scene->textures = document.scene->textures;
                model_->scene->texture_overrides = document.scene->texture_overrides;
                renderer_->refresh_textures();
            }
        }
        auto data = document.refresh_feeding;
        if (!data) {
            ImGui::TextUnformatted("Feeding parameters unavailable.");
            return;
        }
        auto p = data->parameters;
        if (!camera_view)
            validate_refresh_feeding(p);
        auto motion = [&](unsigned slot) {
            auto it = std::find_if(model_->motions.begin(), model_->motions.end(), [&](auto &m) {
                return m.group == 1 && m.slot == slot && m.error.empty();
            });
            return it == model_->motions.end() ? -1 : int(it - model_->motions.begin());
        };
        for (unsigned slot : {0u, 22u, 23u, 24u})
            if (!camera_view || slot == 0)
                require(motion(slot) >= 0, "This model is missing a decoded idle or eating motion");
        if (camera_view) {
            if (studio::TutorialWidgets::Button("refresh_feeding_preview",
                                                paused_ ? "Resume preview" : "Pause preview"))
                paused_ = !paused_;
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("refresh_feeding_preview", "Restart idle"))
                session_.seconds = 0;
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("refresh_feeding_preview", "Exit cameras"))
                active = false;
            ImGui::TextUnformatted(camera_kind_ == 0 ? "Whole-body / normal camera"
                                   : camera_kind_ == 1
                                       ? "Close-up / petting camera"
                                       : "Chest-up: average of whole-body and close-up");
            ImGui::TextDisabled("Saved camera framing | 320 x 240 | idle motion");
        } else {
            if (studio::TutorialWidgets::Button("refresh_feeding_preview",
                                                paused_ ? "Resume feeding" : "Pause feeding"))
                paused_ = !paused_;
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("refresh_feeding_preview", "New bean")) {
                session_.reset(session_.fullness);
                held_ = false;
                food_x_ = 280;
                food_y_ = 65;
            }
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("refresh_feeding_preview", "Exit feeding")) {
                active = false;
                held_ = false;
            }
            ImGui::Text("%s | Bites %u / %d | Fullness %d / 255", session_.status(), session_.bites,
                        p.animation_count, session_.fullness);
            ImGui::TextDisabled(
                "Feeding camera | 320 x 240 touch preview | simplified bean / head tracking");
        }
        auto &io = ImGui::GetIO();
        auto available = ImGui::GetContentRegionAvail();
        float scale = std::max(.1f, std::min(available.x / 320, available.y / 240));
        ImVec2 size{320 * scale, 240 * scale};
        auto origin = ImGui::GetCursorScreenPos();
        origin.x += std::max(0.f, (available.x - size.x) * .5f);
        ImGui::SetCursorScreenPos(origin);
        float x = (io.MousePos.x - origin.x) / scale, y = (io.MousePos.y - origin.y) / scale;
        if (!camera_view && !paused_ && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) &&
            std::hypot(x - food_x_, y - food_y_) < 25 && session_.stage == FeedingStage::Ready)
            held_ = true;
        if (!camera_view && held_) {
            food_x_ = std::clamp(x, 0.f, 320.f);
            food_y_ = std::clamp(y, 0.f, 240.f);
            if (!io.MouseDown[0])
                held_ = false;
        }
        auto slot = camera_view ? 0u : session_.motion_slot();
        auto &current = model_->motions[std::size_t(motion(slot))];
        double duration = std::max({current.skeletal.frames, current.material.frames,
                                    current.visibility.clock.frames, 1.f}) /
                          30.;
        if (!paused_ && renderer_->ready() && camera_view)
            session_.seconds += std::min(double(io.DeltaTime), .1);
        if (!camera_view && !paused_ && renderer_->ready())
            session_.advance(std::min(double(io.DeltaTime), .1), held_,
                             area_.contains(food_x_, food_y_), unsigned(p.animation_count),
                             duration);
        int index = motion(camera_view ? 0u : session_.motion_slot());
        if (model_->motion != index) {
            model_->select_motion(index, camera_view || session_.motion_slot() == 0);
            renderer_->refresh_materials();
        }
        renderer_->playback.seconds = session_.seconds;
        renderer_->lighting = original.lighting;
        renderer_->background_color = original.background_color;
        renderer_->outlines = original.outlines;
        for (std::size_t i = 0; i < model_->scene->draws.size(); ++i)
            renderer_->set_draw_visible(i, original.draw_visible(i));
        renderer_->upload_step();
        float view[16], projection[16];
        std::array<float, 3> eye{}, focus{};
        unsigned camera_slot = camera_view ? (camera_kind_ == 1 ? 2 : 1) : 9;
        for (unsigned k = 0; k < 3; ++k) {
            eye[k] = float(data->cameras[camera_slot].position[k]);
            focus[k] = float(data->cameras[camera_slot].focus[k]);
            if (camera_view && camera_kind_ == 2) {
                eye[k] = (eye[k] + data->cameras[2].position[k]) * .5f;
                focus[k] = (focus[k] + data->cameras[2].focus[k]) * .5f;
            }
        }
        float length = 0, horizontal = 0;
        for (unsigned k = 0; k < 3; ++k) {
            require(std::isfinite(eye[k]) && std::isfinite(focus[k]),
                    "Camera coordinates must be finite");
            float delta = float(eye[k]) - float(focus[k]);
            length += delta * delta;
            if (k != 1)
                horizontal += delta * delta;
        }
        require(
            length > .0001f,
            "Camera position and look-at must be different; move either point or reset the camera");
        auto up = horizontal / length < .0001f ? bx::Vec3{0, 0, 1} : bx::Vec3{0, 1, 0};
        bx::mtxLookAt(view, {float(eye[0]), float(eye[1]), float(eye[2])},
                      {float(focus[0]), float(focus[1]), float(focus[2])}, up,
                      bx::Handedness::Right);
        bx::mtxProj(projection, float(.35 * 180 / 3.141592653589793), 320.f / 240, 10, 2000,
                    bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
        auto texture =
            renderer_->render(unsigned(std::max(1.f, size.x * io.DisplayFramebufferScale.x)),
                              unsigned(std::max(1.f, size.y * io.DisplayFramebufferScale.y)), view,
                              projection, true, false, 0, false);
        bool flip = bgfx::getCaps()->originBottomLeft;
        ImGui::Image(ImTextureID(ImGuiRenderer::image_id(texture, false)), size,
                     {0, flip ? 1.f : 0.f}, {1, flip ? 0.f : 1.f});
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        if (!camera_view && show_zone_) {
            ImVec2 a{origin.x + area_.position[0] * scale, origin.y + area_.position[1] * scale},
                b{origin.x + (area_.position[0] + area_.size[0]) * scale,
                  origin.y + (area_.position[1] + area_.size[1]) * scale};
            auto color = area_.contains(food_x_, food_y_) && held_ ? IM_COL32(100, 235, 155, 255)
                                                                   : IM_COL32(240, 205, 95, 255);
            draw->AddRectFilled(a, b, IM_COL32(120, 210, 160, 25));
            draw->AddRect(a, b, color, 0, 0, 2);
            draw->AddText({a.x + 6, a.y + 6}, color, "Feeding area");
        }
        if (!camera_view && session_.stage != FeedingStage::Complete) {
            float radius = 10 * scale;
            if (overrides_)
                radius *= std::clamp(float(p.scale_percent) / 100 * 130 /
                                         std::max(1.f, float(p.distance)),
                                     0.f, 10.f);
            ImVec2 food{origin.x + food_x_ * scale, origin.y + food_y_ * scale};
            if (session_.stage == FeedingStage::Dropped)
                food.y = origin.y + size.y - radius;
            radius *= std::max(.35f, 1.f - float(session_.bites) / float(p.animation_count) * .65f);
            draw->AddCircleFilled(food, radius, IM_COL32(218, 123, 87, 255), 24);
            draw->AddCircle(food, radius, IM_COL32(255, 221, 153, 255), 24, 2);
            draw->AddLine({food.x - radius * .2f, food.y - radius * .65f},
                          {food.x + radius * .2f, food.y + radius * .65f},
                          IM_COL32(128, 58, 51, 255), 2 * scale);
        }
        draw->PopClipRect();
    } catch (const std::exception &e) {
        ImGui::TextWrapped("Refresh preview unavailable: %s", e.what());
    }
}
}
