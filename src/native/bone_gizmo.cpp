#include "native/viewport_navigation.h"
#include "native/tutorial_widgets.h"
#include "native/bone_gizmo.h"
#include "assets/skeletal_motion.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace studio {
namespace {
using Point = std::array<float, 3>;
Point subtract(Point a, Point b) {
    for (unsigned i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
float dot(Point a, Point b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point transform(const Matrix &m, Point p, bool position) {
    Point out{};
    for (unsigned r = 0; r < 3; ++r) {
        for (unsigned c = 0; c < 3; ++c)
            out[r] += m[r * 4 + c] * p[c];
        if (position)
            out[r] += m[r * 4 + 3];
    }
    return out;
}
float distance(ImVec2 p, ImVec2 a, ImVec2 b) {
    float x = b.x - a.x, y = b.y - a.y,
          t = std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / std::max(x * x + y * y, .001f), 0.f,
                         1.f);
    return std::hypot(p.x - a.x - t * x, p.y - a.y - t * y);
}
}
void BoneGizmo::toolbar() {
    ImGui::BeginDisabled(dragging());
    ImGui::TextUnformatted("Pose mode:");
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("bone_gizmo", "Move (G)", &mode_, 0);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("bone_gizmo", "Rotate (R)", &mode_, 1);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("bone_gizmo", "Scale (S)", &mode_, 2);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("bone_gizmo", "Select", &mode_, 3);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(dragging());
    ImGui::Checkbox("Auto key", &auto_key_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!pending_);
    insert_ = ImGui::Button("Insert key");
    ImGui::SameLine();
    discard_ = ImGui::Button("Discard preview");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::TextDisabled(pending_ ? "Unkeyed preview — Insert key or Discard preview."
                                 : "G/R/S: transform | X/Y/Z: axis | Ctrl: snap | Esc: cancel");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
}
void BoneGizmo::cancel(MaterialDocument &doc, ModelDocument &preview, bool repeat) {
    if (!dragging() && !pending_)
        return;
    pending_ = insert_ = discard_ = false;
    preview.motions = doc.model.motions;
    preview.select_motion(preview.motion, repeat);
    axis_ = -1;
    keyboard_ = false;
    changed_ = false;
}
bool BoneGizmo::draw(MaterialDocument &doc, ModelDocument &preview, EnvironmentRenderer &renderer,
                     const ViewportCamera &camera, const float *view, const float *projection,
                     ImVec2 origin, ImVec2 size, bool hovered, bool &playing, bool &repeat,
                     int &bone) {
    if (preview.area >= 0 || preview.motion < 0 || preview.scene->skeletons.empty() ||
        size.x <= 0 || size.y <= 0)
        return false;
    auto &io = ImGui::GetIO();
    auto mouse = io.MousePos;
    auto &rig = preview.scene->skeletons[0];
    if (rig.joints.empty())
        return false;
    auto restore = [&]() {
        preview.motions = doc.model.motions;
        preview.select_motion(preview.motion, repeat);
        axis_ = -1;
        changed_ = false;
        keyboard_ = false;
    };
    if (dragging() && (identity_ != doc.identity() || revision_ != doc.revision() ||
                       motion_ != preview.motion || bone_ != bone)) {
        restore();
        blocked_ = ImGui::IsMouseDown(0);
    }
    if (pending_) {
        if (identity_ != doc.identity() || revision_ != doc.revision() ||
            motion_ != preview.motion) {
            pending_ = false;
            restore();
        } else {
            playing = false;
            renderer.playback.seconds = frame_ / 30.;
            if (discard_) {
                pending_ = false;
                restore();
            } else if (insert_) {
                try {
                    doc.edit_skeletal_motion(std::size_t(motion_), pending_motion_);
                    revision_ = doc.revision();
                    pending_ = false;
                    restore();
                    error_.clear();
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            } else {
                preview.motions[motion_].skeletal = pending_motion_;
                preview.select_motion(motion_, false);
            }
        }
    }
    insert_ = discard_ = false;
    bool input =
        !io.WantTextInput && !io.AppFocusLost &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (dragging() && (!input || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                       (keyboard_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right)))) {
        restore();
        blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    if (blocked_) {
        blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    bool start_keyboard = false;
    if (input && hovered && !dragging() && !io.KeyCtrl && !viewport_navigating()) {
        const ImGuiKey shortcuts[] = {ImGuiKey_G, ImGuiKey_R, ImGuiKey_S};
        for (int i = 0; i < 3; ++i)
            if (ImGui::IsKeyPressed(shortcuts[i], false)) {
                mode_ = i;
                start_keyboard = true;
            }
    }
    if (keyboard_ && dragging()) {
        const ImGuiKey axes[] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
        for (int i = 0; i < 3; ++i)
            if (ImGui::IsKeyPressed(axes[i], false))
                axis_ = axis_ == i ? 3 : i;
    }
    if (preview.looping_overlay >= 0) {
        if (hovered)
            error_ = "Disable Looping effects to edit this motion with bone handles.";
        return false;
    }
    auto project = [&](Point p, ImVec2 &screen) {
        float a[4]{}, b[4]{};
        for (unsigned r = 0; r < 4; ++r)
            a[r] = view[r] * p[0] + view[4 + r] * p[1] + view[8 + r] * p[2] + view[12 + r];
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned c = 0; c < 4; ++c)
                b[r] += projection[c * 4 + r] * a[c];
        if (b[3] <= .001f)
            return false;
        screen = {origin.x + (b[0] / b[3] + 1) * size.x * .5f,
                  origin.y + (1 - b[1] / b[3]) * size.y * .5f};
        return std::isfinite(screen.x) && std::isfinite(screen.y);
    };
    auto poses = evaluate_skeleton(rig, renderer.playback.seconds, 12, true);
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    int picked = -1;
    float pick_distance = 8;
    for (unsigned i = 0; i < poses.size(); ++i) {
        auto world = pose_multiply(poses[i], rig.joints[i].bind);
        ImVec2 p;
        if (!project({world[3], world[7], world[11]}, p))
            continue;
        draw->AddCircleFilled(p, bone == int(i) ? 5.f : 3.f,
                              bone == int(i) ? IM_COL32(255, 205, 90, 255)
                                             : IM_COL32(100, 230, 230, 210));
        float d = std::hypot(mouse.x - p.x, mouse.y - p.y);
        if (d < pick_distance) {
            pick_distance = d;
            picked = int(i);
        }
    }
    bone = std::clamp(bone, 0, int(rig.joints.size()) - 1);
    auto &joint = rig.joints[bone];
    auto world = pose_multiply(poses[bone], joint.bind);
    Point pivot{world[3], world[7], world[11]};
    Matrix basis = pose_identity();
    if (joint.parent >= 0)
        basis = pose_multiply(poses[joint.parent], rig.joints[joint.parent].bind);
    if (mode_ == 1 && joint.parent >= 0 && (joint.flags & 2) == 0) {
        auto scale = rig.joints[joint.parent].scale;
        int track = rig.tracks.at(joint.parent);
        if (track >= 0)
            for (unsigned c = 0; c < 3; ++c)
                scale[c] = rig.motion.tracks[track].curves[c].sample(
                    float(renderer.playback.seconds * 30), scale[c]);
        for (unsigned c = 0; c < 3; ++c)
            if (std::abs(scale[c]) > 1e-8f)
                for (unsigned r = 0; r < 3; ++r)
                    basis[r * 4 + c] /= scale[c];
    }
    for (unsigned r = 0; r < 3; ++r)
        basis[r * 4 + 3] = pivot[r];
    float length = std::max(dot(subtract(pivot, camera.eye()), camera.forward()) * .13f, .1f);
    if (dragging()) {
        basis = frame_matrix_;
        pivot = pivot_;
        length = length_;
    }
    Matrix normalized = basis;
    Point scales{};
    for (unsigned c = 0; c < 3; ++c) {
        for (unsigned r = 0; r < 3; ++r)
            scales[c] += basis[r * 4 + c] * basis[r * 4 + c];
        scales[c] = std::sqrt(scales[c]);
        if (scales[c] < 1e-7f) {
            draw->PopClipRect();
            error_ = "The parent pose has a zero scale; edit its scale before using handles.";
            if (dragging())
                restore();
            return false;
        }
        for (unsigned r = 0; r < 3; ++r)
            normalized[r * 4 + c] /= scales[c];
    }
    auto eye = camera.eye(), ray = camera.forward(), right = camera.right(), up = camera.up();
    float nx = (2 * (mouse.x - origin.x) / size.x - 1) * .41421356f * size.x / size.y,
          ny = (1 - 2 * (mouse.y - origin.y) / size.y) * .41421356f;
    for (unsigned i = 0; i < 3; ++i)
        ray[i] += right[i] * nx + up[i] * ny;
    float ray_length = std::sqrt(dot(ray, ray));
    for (auto &v : ray)
        v /= ray_length;
    Matrix inverse;
    try {
        inverse = pose_inverse(normalized);
    } catch (const std::exception &e) {
        draw->PopClipRect();
        error_ = e.what();
        if (dragging())
            restore();
        return false;
    }
    auto local_eye = transform(inverse, eye, true), local_ray = transform(inverse, ray, false);
    auto parameter = [&](unsigned axis) {
        Point direction{normalized[axis], normalized[4 + axis], normalized[8 + axis]};
        auto w = subtract(eye, pivot);
        float parallel = dot(ray, direction), denominator = 1 - parallel * parallel;
        if (denominator < .001f)
            return std::numeric_limits<float>::quiet_NaN();
        return (dot(w, direction) - parallel * dot(ray, w)) / denominator;
    };
    auto angle = [&](unsigned axis) {
        if (std::abs(local_ray[axis]) < .001f)
            return std::numeric_limits<float>::quiet_NaN();
        float t = -local_eye[axis] / local_ray[axis];
        if (t <= 0)
            return std::numeric_limits<float>::quiet_NaN();
        unsigned u = (axis + 1) % 3, v = (axis + 2) % 3;
        return std::atan2(local_eye[v] + local_ray[v] * t, local_eye[u] + local_ray[u] * t);
    };
    ImVec2 center;
    int hot = -1;
    float nearest = 8;
    const ImU32 colors[] = {IM_COL32(245, 95, 85, 255), IM_COL32(100, 235, 130, 255),
                            IM_COL32(95, 155, 255, 255)};
    if (project(pivot, center) && mode_ != 3)
        for (unsigned axis = 0; axis < 3; ++axis) {
            auto color = axis_ == int(axis) ? IM_COL32(255, 225, 110, 255) : colors[axis];
            if (mode_ == 0 || mode_ == 2) {
                Point p{};
                p[axis] = length;
                ImVec2 end;
                if (!project(transform(normalized, p, true), end))
                    continue;
                float d = distance(mouse, center, end);
                if (d < nearest && std::hypot(end.x - center.x, end.y - center.y) > 12 &&
                    std::isfinite(parameter(axis))) {
                    nearest = d;
                    hot = int(axis);
                }
                draw->AddLine(center, end, color, 3);
                float dx = end.x - center.x, dy = end.y - center.y,
                      n = std::max(std::hypot(dx, dy), .001f);
                dx /= n;
                dy /= n;
                if (mode_ == 2)
                    draw->AddRectFilled({end.x - 5, end.y - 5}, {end.x + 5, end.y + 5}, color);
                else
                    draw->AddTriangleFilled(
                        end, {end.x - dx * 12 - dy * 5, end.y - dy * 12 + dx * 5},
                        {end.x - dx * 12 + dy * 5, end.y - dy * 12 - dx * 5}, color);
                draw->AddText({end.x + 5, end.y + 3}, color,
                              axis == 0   ? "X"
                              : axis == 1 ? "Y"
                                          : "Z");
            } else {
                ImVec2 previous;
                bool valid = false;
                for (unsigned i = 0; i <= 64; ++i) {
                    float t = float(i) * 6.283185307f / 64;
                    Point p{};
                    p[(axis + 1) % 3] = std::cos(t) * length;
                    p[(axis + 2) % 3] = std::sin(t) * length;
                    ImVec2 at;
                    bool ok = project(transform(normalized, p, true), at);
                    if (ok && valid) {
                        float d = distance(mouse, previous, at);
                        if (d < nearest && std::isfinite(angle(axis))) {
                            nearest = d;
                            hot = int(axis);
                        }
                        draw->AddLine(previous, at, color, 2);
                    }
                    previous = at;
                    valid = ok;
                }
            }
        }
    draw->PopClipRect();
    bool captured = dragging() || (input && hovered && (hot >= 0 || ImGui::IsMouseReleased(0)));
    if (input && hovered && !dragging() && (start_keyboard || ImGui::IsMouseClicked(0))) {
        if (hot >= 0 || start_keyboard) {
            keyboard_ = start_keyboard;
            start_mouse_ = mouse;
            axis_ = start_keyboard ? 3 : hot;
            bone_ = bone;
            motion_ = preview.motion;
            identity_ = doc.identity();
            revision_ = doc.revision();
            frame_matrix_ = basis;
            pivot_ = pivot;
            length_ = length;
            start_ = start_keyboard ? 0 : parameter(hot);
            last_angle_ = start_keyboard ? 0 : angle(hot);
            angle_ = 0;
            original_ = pending_ ? pending_motion_ : doc.model.motions[motion_].skeletal;
            next_ = original_;
            MaterialMotion clock;
            clock.frames = original_.frames;
            clock.looping = repeat;
            frame_ =
                unsigned(std::round(animation_frame(clock, renderer.playback.seconds, false, 12)));
            playing = false;
            repeat = false;
            renderer.playback.seconds = frame_ / 30.;
            preview.select_motion(motion_, false);
            changed_ = false;
            error_.clear();
        } else if (picked >= 0 && !io.KeyCtrl) {
            bone = picked;
            playing = false;
            captured = true;
        }
    }
    if (dragging())
        try {
            float amount = 0;
            Point movement{}, rotation_axis{};
            if (keyboard_) {
                const float dx = mouse.x - start_mouse_.x, dy = mouse.y - start_mouse_.y;
                if (mode_ == 0) {
                    Point world_delta{};
                    const float units =
                        std::max(dot(subtract(pivot, eye), camera.forward()), .01f) * .82842712f /
                        size.y;
                    for (unsigned i = 0; i < 3; ++i)
                        world_delta[i] = (right[i] * dx - up[i] * dy) * units;
                    movement = transform(inverse, world_delta, false);
                    for (unsigned i = 0; i < 3; ++i) {
                        movement[i] /= scales[i];
                        if (axis_ < 3 && int(i) != axis_)
                            movement[i] = 0;
                        if (io.KeyCtrl)
                            movement[i] = std::round(movement[i]);
                    }
                } else if (mode_ == 1) {
                    amount = (dx - dy) * .01f;
                    if (io.KeyCtrl)
                        amount = std::round(amount / .261799388f) * .261799388f;
                    if (axis_ < 3)
                        rotation_axis[axis_] = 1;
                    else
                        rotation_axis = transform(inverse, camera.forward(), false);
                } else {
                    amount = std::exp(std::clamp((dx - dy) / 150.f, -6.f, 6.f));
                    if (io.KeyCtrl)
                        amount = std::max(.1f, std::round(amount * 10) / 10);
                }
            } else if (mode_ == 0) {
                amount = (parameter(unsigned(axis_)) - start_) / scales[axis_];
                if (io.KeyCtrl)
                    amount = std::round(amount);
            } else if (mode_ == 2) {
                amount = std::max(.01f, 1 + (parameter(unsigned(axis_)) - start_) / length_);
                if (io.KeyCtrl)
                    amount = std::max(.1f, std::round(amount * 10) / 10);
            } else {
                float current = angle(unsigned(axis_));
                angle_ += std::remainder(current - last_angle_, 6.283185307f);
                last_angle_ = current;
                amount = angle_;
                if (io.KeyCtrl)
                    amount = std::round(amount / .261799388f) * .261799388f;
            }
            require(std::isfinite(amount),
                    "View is parallel to this handle; orbit the camera and try again");
            next_ = original_;
            auto found = std::find_if(next_.tracks.begin(), next_.tracks.end(), [&](auto &t) {
                return t.name == joint.name;
            });
            JointTrack track;
            if (found != next_.tracks.end())
                track = *found;
            else
                track.name = joint.name;
            auto edited = track;
            if (mode_ == 2) {
                Point factors{1, 1, 1};
                for (int i = 0; i < 3; ++i)
                    if (axis_ == 3 || axis_ == i)
                        factors[i] = amount;
                edited = scale_joint_motion(track, joint, frame_, factors);
            } else if (keyboard_ && mode_ == 0) {
                for (unsigned i = 0; i < 3; ++i)
                    edited = offset_joint_motion(edited, joint, frame_, i, movement[i], false);
            } else if (keyboard_)
                edited = rotate_joint_motion(track, joint, frame_, rotation_axis, amount);
            else
                edited =
                    offset_joint_motion(track, joint, frame_, unsigned(axis_), amount, mode_ == 1);
            if (found == next_.tracks.end()) {
                if (edited != track)
                    next_.tracks.push_back(edited);
            } else
                *found = edited;
            changed_ = next_ != original_;
            preview.motions[motion_].skeletal = next_;
            preview.select_motion(motion_, false);
            renderer.playback.skeletal = true;
            if (keyboard_ ? (!start_keyboard &&
                             (ImGui::IsMouseClicked(0) || ImGui::IsKeyPressed(ImGuiKey_Enter)))
                          : (ImGui::IsMouseReleased(0) || !ImGui::IsMouseDown(0))) {
                auto next = next_;
                bool changed = changed_;
                restore();
                if (changed && !auto_key_) {
                    pending_motion_ = next;
                    pending_ = true;
                    preview.motions[motion_].skeletal = next;
                    preview.select_motion(motion_, false);
                } else if (changed) {
                    pending_ = false;
                    doc.edit_skeletal_motion(std::size_t(motion_), next);
                    const bool overlays = preview.looping_effects;
                    preview = doc.model;
                    preview.looping_effects = overlays;
                    preview.select_motion(motion_, false);
                    renderer.set_scene(preview.scene);
                }
                error_.clear();
            }
        } catch (const std::exception &e) {
            restore();
            blocked_ = ImGui::IsMouseDown(0);
            error_ = e.what();
        }
    return captured;
}
}
