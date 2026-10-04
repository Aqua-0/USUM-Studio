#include "native/motion_graph.h"
#include "assets/motion_key_edit.h"
#include "native/tutorial_widgets.h"
#include "native/skeletal_motion_editor.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cctype>
namespace studio {
void SkeletalMotionEditor::draw(MaterialDocument &doc, ModelDocument &preview,
                                EnvironmentRenderer &renderer, bool &playing, bool &repeat,
                                int &bone, bool &show_bones, bool *show_weights) {
    bool pose_pending;
    {
        std::lock_guard lock(pose_dialog_->mutex);
        pose_pending = pose_dialog_->pending;
        if (pose_dialog_->ready) {
            pose_dialog_->ready = false;
            try {
                require(pose_dialog_->error.empty(), pose_dialog_->error);
                if (!pose_dialog_->path.empty()) {
                    auto path = std::filesystem::u8path(pose_dialog_->path);
                    if (!pose_export_.empty()) {
                        if (path.extension().empty())
                            path += ".usum-pose";
                        write_file_atomic(
                            path, View(reinterpret_cast<const std::uint8_t *>(pose_export_.data()),
                                       pose_export_.size()));
                    } else
                        poses_.push_back(parse_motion_pose(text(read_file(path))));
                    error_.clear();
                }
            } catch (const std::exception &e) {
                error_ = e.what();
            }
            pose_export_.clear();
        }
    }
    if (preview.area >= 0 ||
        (preview.scene->skeletons.empty() || preview.scene->skeletons[0].joints.empty())) {
        ImGui::TextWrapped("Open a Pokemon in Studio to edit its bone motions.");
        return;
    }
    if (preview.motion < 0) {
        ImGui::TextWrapped("Choose a motion in the viewport.");
        return;
    }
    auto index = std::size_t(preview.motion);
    auto motion = preview.motions.at(index).skeletal;
    auto joints = preview.scene->skeletons[0].joints;
    auto identity = doc.identity() + "/" + std::to_string(index);
    if (identity_ != identity) {
        identity_ = identity;
        key_frame_ = 0;
        editing_bone_ = editing_channel_ = -1;
        value_ = slope_ = 0;
        error_.clear();
    }
    ImGui::TextDisabled("Select a bone to pose or edit its keys.");
    if (preview.looping_overlay >= 0) {
        if (ImGui::Button("Disable overlay for posing")) {
            preview.looping_effects = false;
            preview.select_motion(preview.motion, repeat);
        }
    }
    studio::TutorialWidgets::Checkbox("skeletal_motion_editor", "Show bones through model",
                                      &show_bones);
    MaterialMotion clock;
    clock.frames = motion.frames;
    clock.looping = repeat;
    int frame = int(animation_frame(clock, renderer.playback.seconds, false, 12));
    auto scrub = [&](int at) {
        playing = false;
        repeat = false;
        preview.select_motion(int(index), false);
        renderer.playback.seconds = at / 30.;
        renderer.playback.skeletal = true;
    };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##bone-frame", &frame, 0, int(motion.frames), "Frame %d"))
        scrub(frame);
    ImGui::TextDisabled("%u frames | 30 fps", unsigned(motion.frames));
    auto lowercase = [](std::string text) {
        for (auto &c : text)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    bone = std::clamp(bone, 0, int(joints.size()) - 1);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##bone-filter", "Filter bones", search_, sizeof(search_));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##motion-bone", joints[bone].name.c_str())) {
        for (unsigned i = 0; i < joints.size(); ++i)
            if (std::string(search_).empty() ||
                lowercase(joints[i].name).find(lowercase(search_)) != std::string::npos)
                if (ImGui::Selectable(joints[i].name.c_str(), bone == int(i))) {
                    bone = int(i);
                    value_ = slope_ = 0;
                }
        ImGui::EndCombo();
    }
    if (show_weights)
        ImGui::Checkbox("Show selected bone weights", show_weights);
    auto selected = tracks_.draw(motion, joints, identity, float(frame), bone, channel_, playing,
                                 show_bones, show_weights);
    if (selected.frame >= 0) {
        frame = selected.frame;
        scrub(frame);
    }
    if (selected.edited)
        try {
            doc.edit_skeletal_motion(index, *selected.edited);
            preview = doc.model;
            preview.select_motion(int(index), repeat);
            renderer.set_scene(preview.scene);
            renderer.playback.skeletal = true;
            motion = preview.motions.at(index).skeletal;
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    auto &joint = joints[bone];
    auto found = std::find_if(motion.tracks.begin(), motion.tracks.end(), [&](auto &t) {
        return t.name == joint.name;
    });
    JointTrack track;
    if (found != motion.tracks.end())
        track = *found;
    else
        track.name = joint.name;
    const char *channels[] = {"Scale X",       "Scale Y",       "Scale Z",
                              "Rotation X",    "Rotation Y",    "Rotation Z",
                              "Translation X", "Translation Y", "Translation Z"};
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##bone-channel", &channel_, channels, 9);
    if (channel_ >= 3 && channel_ < 6)
        ImGui::TextWrapped(track.axis_angle
                               ? "Rotation vector: axis multiplied by half the angle in radians."
                               : "Euler rotation in radians, relative to the parent bone.");
    float fallback = channel_ < 3   ? joint.scale[channel_]
                     : channel_ < 6 ? (track.axis_angle ? 0 : joint.rotation[channel_ - 3])
                                    : joint.translation[channel_ - 6];
    auto &curve = track.curves[channel_];
    if (editing_bone_ != bone || editing_channel_ != channel_ || selected.frame >= 0) {
        editing_bone_ = bone;
        editing_channel_ = channel_;
        key_frame_ = frame;
        value_ = curve.sample(float(frame), fallback);
        slope_ = 0;
        for (const auto &key : curve.keys)
            if (key.frame == frame) {
                value_ = key.value;
                slope_ = key.slope;
                break;
            }
    }
    if (selected.key >= 0 && std::size_t(selected.key) < curve.keys.size()) {
        const auto &key = curve.keys[selected.key];
        key_frame_ = int(key.frame);
        value_ = key.value;
        slope_ = key.slope;
    }
    std::vector<MotionGraphKey> graph_keys;
    for (auto &key : curve.keys)
        graph_keys.push_back({key.frame, key.value});
    auto graph = motion_graph(
        "Bone motion timeline", motion.frames, float(frame), graph_keys,
        [&](float at) {
            return curve.sample(at, fallback);
        },
        graph_expanded_);
    if (graph.frame >= 0)
        scrub(graph.frame);
    if (graph.key >= 0) {
        const auto &key = curve.keys[graph.key];
        key_frame_ = int(key.frame);
        value_ = key.value;
        slope_ = key.slope;
    }
    ImGui::BeginChild("Bone keys", {0, 100}, ImGuiChildFlags_Borders);
    if (curve.keys.empty())
        ImGui::TextWrapped("No keys: uses the base pose or preceding layer.");
    for (auto &k : curve.keys) {
        char label[100];
        std::snprintf(label, sizeof(label), "%g: %.5g (slope %.5g)", k.frame, k.value, k.slope);
        if (ImGui::Selectable(label, key_frame_ == int(k.frame))) {
            key_frame_ = int(k.frame);
            value_ = k.value;
            slope_ = k.slope;
            scrub(key_frame_);
        }
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-80);
    ImGui::InputInt("Key frame", &key_frame_);
    if (studio::TutorialWidgets::Button("skeletal_motion_editor", "Sample current frame")) {
        key_frame_ = frame;
        value_ = curve.sample(float(frame), fallback);
        slope_ = 0;
    }
    ImGui::SetNextItemWidth(-65);
    ImGui::InputFloat("Value", &value_, 0, 0, "%.5f");
    ImGui::SetNextItemWidth(-65);
    ImGui::InputFloat("Slope", &slope_, 0, 0, "%.5f");
    ImGui::TextWrapped(
        "Slope: change per frame. One key is constant; zero slope eases into a key.");
    auto apply = [&](bool remove) {
        try {
            auto next = motion;
            std::erase_if(next.tracks, [&](auto &t) {
                return t.name == joint.name;
            });
            if (!remove) {
                if (found == motion.tracks.end())
                    next.tracks.push_back(track);
                else
                    next.tracks.insert(
                        next.tracks.begin() + std::distance(motion.tracks.begin(), found), track);
            }
            doc.edit_skeletal_motion(index, next);
            preview = doc.model;
            preview.select_motion(int(index), repeat);
            renderer.set_scene(preview.scene);
            renderer.playback.skeletal = true;
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    };
    bool valid = key_frame_ >= 0 && key_frame_ <= motion.frames && std::isfinite(value_) &&
                 std::isfinite(slope_);
    ImGui::BeginDisabled(!valid);
    if (studio::TutorialWidgets::Button("skeletal_motion_editor", "Insert key")) {
        auto &keys = track.curves[channel_].keys;
        std::erase_if(keys, [&](auto &k) {
            return k.frame == key_frame_;
        });
        if (keys.empty() && key_frame_)
            keys.push_back({0, fallback, 0});
        keys.push_back({float(key_frame_), value_, slope_});
        std::sort(keys.begin(), keys.end(), [](auto &a, auto &b) {
            return a.frame < b.frame;
        });
        apply(false);
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("skeletal_motion_editor", "Delete key")) {
        auto &keys = track.curves[channel_].keys;
        std::erase_if(keys, [&](auto &k) {
            return k.frame == key_frame_;
        });
        if (keys.size() == 1)
            keys[0].frame = 0;
        apply(false);
    }
    ImGui::EndDisabled();
    if (studio::TutorialWidgets::Button("skeletal_motion_editor", "Clear channel")) {
        track.curves[channel_].keys.clear();
        apply(false);
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("skeletal_motion_editor", "Remove bone track"))
        apply(true);
    if (TutorialWidgets::CollapsingHeader("skeletal_motion_editor", "Multiple keys and timing")) {
        ImGui::InputInt("First frame", &first_);
        ImGui::InputInt("Last frame", &last_);
        ImGui::TextWrapped("Inclusive range in this bone channel. Paste replaces keys at matching "
                           "frames. Retiming rejects overlaps and fractional frames.");
        auto edit_keys = [&](auto operation) {
            try {
                operation();
                apply(false);
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        };
        if (TutorialWidgets::Button("skeletal_motion_editor", "Copy key range"))
            try {
                clipboard_ = copy_motion_keys(curve, float(first_), float(last_));
                error_.clear();
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu copied keys", clipboard_.size());
        ImGui::InputInt("Paste at frame", &paste_frame_);
        ImGui::BeginDisabled(clipboard_.empty());
        if (TutorialWidgets::Button("skeletal_motion_editor", "Paste key range"))
            edit_keys([&] {
                paste_motion_keys(curve, clipboard_, float(paste_frame_), motion.frames);
            });
        ImGui::EndDisabled();
        if (TutorialWidgets::Button("skeletal_motion_editor", "Delete key range"))
            edit_keys([&] {
                require(first_ >= 0 && last_ >= first_, "Choose a valid key range");
                std::erase_if(curve.keys, [&](auto k) {
                    return k.frame >= first_ && k.frame <= last_;
                });
            });
        ImGui::InputInt("Shift frames", &offset_);
        ImGui::InputFloat("Time scale", &time_scale_);
        if (TutorialWidgets::Button("skeletal_motion_editor", "Retime key range"))
            edit_keys([&] {
                retime_motion_keys(curve, float(first_), float(last_), float(offset_), time_scale_,
                                   motion.frames);
            });
        if (TutorialWidgets::Button("skeletal_motion_editor", "Flat tangents"))
            edit_keys([&] {
                set_motion_tangents(curve, float(first_), float(last_), MotionTangent::Flat);
            });
        ImGui::SameLine();
        if (TutorialWidgets::Button("skeletal_motion_editor", "Smooth tangents"))
            edit_keys([&] {
                set_motion_tangents(curve, float(first_), float(last_), MotionTangent::Smooth);
            });
        if (TutorialWidgets::Button("skeletal_motion_editor", "Match next segment slope"))
            edit_keys([&] {
                set_motion_tangents(curve, float(first_), float(last_), MotionTangent::Linear);
            });
        ImGui::TextWrapped("The format stores one tangent per key. Matching the next segment slope "
                           "does not make both neighboring segments linear.");
    }
    if (TutorialWidgets::CollapsingHeader("skeletal_motion_editor", "Pose shelf")) {
        ImGui::TextWrapped(
            "Capture all bones from the base motion at the current frame. Save poses to reuse them "
            "in later sessions. Applying inserts keys at the current frame.");
        ImGui::BeginDisabled(pose_pending);
        if (TutorialWidgets::Button("skeletal_motion_editor", "Load pose...")) {
            pose_export_.clear();
            choose_pose(nullptr, pose_dialog_, nullptr, false);
        }
        ImGui::EndDisabled();
        ImGui::InputText("Pose name", pose_name_, sizeof(pose_name_));
        if (TutorialWidgets::Button("skeletal_motion_editor", "Capture pose"))
            try {
                require(pose_name_[0], "Enter a pose name");
                poses_.emplace_back(pose_name_, capture_motion_pose(motion, joints, float(frame)));
                error_.clear();
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        for (std::size_t i = 0; i < poses_.size(); ++i) {
            ImGui::PushID(int(i));
            ImGui::TextUnformatted(poses_[i].first.c_str());
            ImGui::SameLine();
            if (TutorialWidgets::Button("skeletal_motion_editor", "Apply pose"))
                try {
                    auto next = motion;
                    paste_motion_pose(next, poses_[i].second, joints, float(frame));
                    doc.edit_skeletal_motion(index, next);
                    preview = doc.model;
                    preview.select_motion(int(index), repeat);
                    renderer.set_scene(preview.scene);
                    renderer.playback.skeletal = true;
                    error_.clear();
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            ImGui::BeginDisabled(pose_pending);
            if (TutorialWidgets::Button("skeletal_motion_editor", "Save pose...")) {
                pose_export_ = serialize_motion_pose(poses_[i].first, poses_[i].second);
                choose_pose(nullptr, pose_dialog_, "pose.usum-pose", true);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            bool remove = TutorialWidgets::Button("skeletal_motion_editor", "Remove pose");
            ImGui::PopID();
            if (remove) {
                poses_.erase(poses_.begin() + std::ptrdiff_t(i));
                break;
            }
        }
    }
    if (preview.looping_overlay >= 0)
        ImGui::TextWrapped("A looping overlay is active. It can override this bone; disable "
                           "Looping effects in the viewport to inspect the base motion.");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
}
}
