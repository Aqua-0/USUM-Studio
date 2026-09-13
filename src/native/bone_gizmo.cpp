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
    ImGui::TextUnformatted("Bone handles:");
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("bone_gizmo", "Move (W)", &mode_, 0);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("bone_gizmo", "Rotate (E)", &mode_, 1);
    ImGui::SameLine();
    studio::TutorialWidgets::RadioButton("bone_gizmo", "Off", &mode_, 2);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drag to key the current frame. Ctrl snaps; Escape cancels. Click a bone "
                          "dot to select it.");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
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
    };
    if (dragging() && (identity_ != doc.identity() || revision_ != doc.revision() ||
                       motion_ != preview.motion || bone_ != bone)) {
        restore();
        blocked_ = ImGui::IsMouseDown(0);
    }
    bool input =
        !io.WantTextInput && !io.AppFocusLost &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (dragging() && (!input || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        restore();
        blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    if (blocked_) {
        blocked_ = ImGui::IsMouseDown(0);
        return true;
    }
    if (input && hovered && !dragging() && !io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_W))
            mode_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_E))
            mode_ = 1;
    }
    if (mode_ == 2)
        return false;
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
    if (project(pivot, center))
        for (unsigned axis = 0; axis < 3; ++axis) {
            auto color = axis_ == int(axis) ? IM_COL32(255, 225, 110, 255) : colors[axis];
            if (mode_ == 0) {
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
                draw->AddTriangleFilled(end, {end.x - dx * 12 - dy * 5, end.y - dy * 12 + dx * 5},
                                        {end.x - dx * 12 + dy * 5, end.y - dy * 12 - dx * 5},
                                        color);
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
    bool captured = dragging() || (input && hovered && hot >= 0);
    if (input && hovered && !dragging() && ImGui::IsMouseClicked(0)) {
        if (hot >= 0) {
            axis_ = hot;
            bone_ = bone;
            motion_ = preview.motion;
            identity_ = doc.identity();
            revision_ = doc.revision();
            frame_matrix_ = basis;
            pivot_ = pivot;
            length_ = length;
            start_ = parameter(hot);
            last_angle_ = angle(hot);
            angle_ = 0;
            original_ = doc.model.motions[motion_].skeletal;
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
            if (mode_ == 0) {
                amount = (parameter(unsigned(axis_)) - start_) / scales[axis_];
                if (io.KeyCtrl)
                    amount = std::round(amount);
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
            auto edited =
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
            if (ImGui::IsMouseReleased(0) || !ImGui::IsMouseDown(0)) {
                auto next = next_;
                bool changed = changed_;
                restore();
                if (changed) {
                    doc.edit_skeletal_motion(std::size_t(motion_), next);
                    preview = doc.model;
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
