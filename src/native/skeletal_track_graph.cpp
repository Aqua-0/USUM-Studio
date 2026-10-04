#include "native/skeletal_track_graph.h"
#include "native/tutorial_widgets.h"
#include "assets/motion_key_edit.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <map>
namespace studio {
namespace {
constexpr const char *channel_names[] = {"Scale X",       "Scale Y",       "Scale Z",
                                         "Rotation X",    "Rotation Y",    "Rotation Z",
                                         "Translation X", "Translation Y", "Translation Z"};
struct Curve {
    int bone, channel;
    const AnimationCurve *curve;
    float fallback, low, high;
    std::array<float, 129> samples;
    ImU32 color;
};
float fallback(const Joint &j, const JointTrack *t, unsigned c) {
    return c < 3   ? j.scale[c]
           : c < 6 ? (t && t->axis_angle ? 0 : j.rotation[c - 3])
                   : j.translation[c - 6];
}
std::string lower(std::string text) {
    for (auto &c : text)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
ImU32 curve_color(unsigned bone, unsigned channel, unsigned alpha) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(std::fmod(bone * .61803399f + (channel % 3) * .17f, 1.f), .6f, .95f,
                                r, g, b);
    return ImGui::ColorConvertFloat4ToU32({r, g, b, alpha / 255.f});
}
}
SkeletalGraphResult SkeletalTrackGraph::draw(const SkeletalMotion &motion,
                                             const std::vector<Joint> &joints,
                                             const std::string &identity, float playhead, int &bone,
                                             int &channel, bool &playing, bool &show_bones,
                                             bool *show_weights) {
    SkeletalGraphResult result;
    if (TutorialWidgets::Button("skeletal_tracks", "All bone tracks...")) {
        open_ = true;
        show_bones = true;
    }
    if (identity_ != identity || masks_.size() != joints.size()) {
        identity_ = identity;
        masks_.assign(joints.size(), 511);
        first_ = 0;
        last_ = int(motion.frames);
        error_.clear();
    }
    if (!open_)
        return result;
    ImGui::SetNextWindowSize({1100, 760}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Bone track graph", &open_)) {
        ImGui::End();
        return result;
    }
    if (ImGui::Button(playing ? "Pause" : "Play"))
        playing = !playing;
    ImGui::SameLine();
    if (ImGui::Button("Restart"))
        result.frame = 0;
    ImGui::SameLine();
    ImGui::Checkbox("Skeleton overlay", &show_bones);
    ImGui::SameLine();
    ImGui::Checkbox("Normalize each curve", &normalized_);
    ImGui::SameLine();
    ImGui::Checkbox("Include constant / bind channels", &constants_);
    if (show_weights) {
        ImGui::SameLine();
        ImGui::Checkbox("Bone weights", show_weights);
    }
    ImGui::TextWrapped(
        "Click a curve or key to select its bone and SRT channel. Drag empty space to scrub the "
        "deforming model. Shift-drag selects a frame range across shown tracks.");
    if (normalized_)
        ImGui::TextDisabled("Each curve has its own vertical scale; hover for actual values. These "
                            "are local bone channels, not world-space paths.");
    else
        ImGui::TextDisabled("Shared value scale within each S/R/T graph. Rotation channels use "
                            "each track's stored Euler or half-angle vector representation.");
    std::map<std::string, const JointTrack *> tracks;
    for (auto &t : motion.tracks)
        tracks[t.name] = &t;
    auto needle = lower(search_);
    if (ImGui::BeginTable("Tracks and curves", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Bones", ImGuiTableColumnFlags_WidthFixed, 265);
        ImGui::TableSetupColumn("Curves", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Button("Show all bones"))
            std::fill(masks_.begin(), masks_.end(), 511);
        ImGui::SameLine();
        if (ImGui::Button("Hide all"))
            std::fill(masks_.begin(), masks_.end(), 0);
        if (ImGui::Button("Only selected bone")) {
            std::fill(masks_.begin(), masks_.end(), 0);
            if (bone >= 0 && std::size_t(bone) < masks_.size())
                masks_[bone] = 511;
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Children") && bone >= 0) {
            for (unsigned i = 0; i < joints.size(); ++i) {
                int parent = int(i);
                for (unsigned depth = 0;
                     depth < joints.size() && parent >= 0 && std::size_t(parent) < joints.size();
                     ++depth) {
                    if (parent == bone) {
                        masks_[i] = 511;
                        break;
                    }
                    parent = joints[parent].parent;
                }
            }
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##track-filter", "Filter visible bone names", search_,
                                 sizeof(search_));
        needle = lower(search_);
        ImGui::BeginChild("Bone track list", {0, ImGui::GetContentRegionAvail().y},
                          ImGuiChildFlags_Borders);
        for (unsigned i = 0; i < joints.size(); ++i) {
            auto &joint = joints[i];
            if (!needle.empty() && lower(joint.name).find(needle) == std::string::npos)
                continue;
            ImGui::PushID(int(i));
            bool shown = masks_[i] != 0;
            if (ImGui::Checkbox("##shown", &shown))
                masks_[i] = shown ? 511 : 0;
            ImGui::SameLine();
            bool expanded = ImGui::TreeNodeEx("##channels", ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::SameLine();
            if (ImGui::Selectable(joint.name.c_str(), bone == int(i))) {
                bone = int(i);
                show_bones = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(joint.name.c_str());
                if (joint.parent >= 0 && std::size_t(joint.parent) < joints.size())
                    ImGui::Text("Parent: %s", joints[joint.parent].name.c_str());
                auto t = tracks.contains(joint.name) ? tracks.at(joint.name) : nullptr;
                for (unsigned c = 0; c < 9; ++c)
                    ImGui::Text("%s: %.5g", channel_names[c],
                                t ? t->curves[c].sample(playhead, fallback(joint, t, c))
                                  : fallback(joint, t, c));
                ImGui::EndTooltip();
            }
            if (expanded)
                for (unsigned c = 0; c < 9; ++c) {
                    ImGui::PushID(int(c));
                    ImGui::Indent(20);
                    bool visible = (masks_[i] & (1u << c)) != 0;
                    if (ImGui::Checkbox("##channel-visible", &visible)) {
                        if (visible)
                            masks_[i] |= 1u << c;
                        else
                            masks_[i] &= ~(1u << c);
                    }
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, curve_color(i, c, 255));
                    if (ImGui::Selectable(channel_names[c], bone == int(i) && channel == int(c))) {
                        bone = int(i);
                        channel = int(c);
                        show_bones = true;
                    }
                    ImGui::PopStyleColor();
                    ImGui::Unindent(20);
                    ImGui::PopID();
                }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        for (unsigned g = 0; g < 3; ++g) {
            if (g)
                ImGui::SameLine();
            ImGui::Checkbox(g == 0 ? "Scale" : g == 1 ? "Rotation" : "Translation", &groups_[g]);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Axes:");
        for (unsigned c = 0; c < 3; ++c) {
            ImGui::SameLine();
            ImGui::Checkbox(c == 0 ? "X" : c == 1 ? "Y" : "Z", &axes_[c]);
        }
        std::vector<Curve> curves;
        static const AnimationCurve empty;
        for (unsigned i = 0; i < joints.size(); ++i) {
            if (!needle.empty() && lower(joints[i].name).find(needle) == std::string::npos)
                continue;
            auto t = tracks.contains(joints[i].name) ? tracks.at(joints[i].name) : nullptr;
            for (unsigned c = 0; c < 9; ++c) {
                if (!groups_[c / 3] || !axes_[c % 3] || !(masks_[i] & (1u << c)))
                    continue;
                auto &curve = t ? t->curves[c] : empty;
                if (!constants_ && curve.keys.size() < 2)
                    continue;
                Curve entry{int(i), int(c), &curve, fallback(joints[i], t, c),
                            0,      0,      {},     curve_color(i, c, 180)};
                for (unsigned k = 0; k < entry.samples.size(); ++k) {
                    float value = curve.sample(motion.frames * k / 128.f, entry.fallback);
                    entry.samples[k] = std::isfinite(value) ? value : 0;
                    if (!k)
                        entry.low = entry.high = entry.samples[k];
                    entry.low = std::min(entry.low, entry.samples[k]);
                    entry.high = std::max(entry.high, entry.samples[k]);
                }
                for (auto &key : curve.keys)
                    if (std::isfinite(key.value)) {
                        entry.low = std::min(entry.low, key.value);
                        entry.high = std::max(entry.high, key.value);
                    }
                float margin = std::max(.001f, (entry.high - entry.low) * .08f);
                entry.low -= margin;
                entry.high += margin;
                curves.push_back(entry);
            }
        }
        ImGui::Text("%zu curves | %u frames | active: %s / %s", curves.size(),
                    unsigned(motion.frames), joints.at(std::size_t(bone)).name.c_str(),
                    channel_names[channel]);
        float graph_height = std::max(250.f, ImGui::GetContentRegionAvail().y - 185.f);
        auto origin = ImGui::GetCursorScreenPos();
        ImVec2 size{std::max(100.f, ImGui::GetContentRegionAvail().x), graph_height};
        ImGui::InvisibleButton("##all-tracks", size);
        bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive(),
             clicked = ImGui::IsItemClicked();
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(20, 29, 35, 255));
        float duration = std::max(1.f, motion.frames), left = origin.x + 48,
              width = std::max(1.f, size.x - 58);
        int group_count = int(groups_[0]) + int(groups_[1]) + int(groups_[2]);
        float lane_height = (size.y - 20) / std::max(1, group_count);
        std::array<float, 3> tops{}, lows{}, highs{};
        int lane = 0;
        for (unsigned g = 0; g < 3; ++g)
            if (groups_[g]) {
                tops[g] = origin.y + lane++ * lane_height + 15;
                bool initial = true;
                for (auto &curve : curves)
                    if (curve.channel / 3 == int(g)) {
                        if (initial) {
                            lows[g] = curve.low;
                            highs[g] = curve.high;
                            initial = false;
                        } else {
                            lows[g] = std::min(lows[g], curve.low);
                            highs[g] = std::max(highs[g], curve.high);
                        }
                    }
                if (initial) {
                    lows[g] = -.1f;
                    highs[g] = .1f;
                }
                draw->AddText({origin.x + 4, tops[g] - 13}, IM_COL32(190, 200, 210, 255),
                              g == 0   ? "Scale"
                              : g == 1 ? "Rotate"
                                       : "Move");
                for (unsigned k = 0; k <= 4; ++k) {
                    float y = tops[g] + (lane_height - 30) * k / 4;
                    draw->AddLine({left, y}, {left + width, y}, IM_COL32(42, 53, 64, 255));
                    char label[32];
                    std::snprintf(label, sizeof(label), "%.3g",
                                  normalized_ ? 1.f - k / 4.f
                                              : highs[g] - (highs[g] - lows[g]) * k / 4.f);
                    draw->AddText({origin.x + 2, y}, IM_COL32(155, 170, 180, 255), label);
                }
            }
        auto x = [&](float frame) {
            return left + frame / duration * width;
        };
        auto y = [&](const Curve &curve, float value) {
            unsigned g = unsigned(curve.channel) / 3;
            float low = normalized_ ? curve.low : lows[g],
                  high = normalized_ ? curve.high : highs[g];
            return tops[g] + (high - value) / (high - low) * (lane_height - 30);
        };
        for (unsigned k = 0; k <= 4; ++k) {
            float px = x(duration * k / 4);
            draw->AddLine({px, origin.y}, {px, origin.y + size.y - 17}, IM_COL32(50, 62, 73, 255));
            char label[24];
            std::snprintf(label, sizeof(label), "%.0f", duration * k / 4);
            draw->AddText({px - 4, origin.y + size.y - 15}, IM_COL32(180, 190, 200, 255), label);
        }
        auto mouse = ImGui::GetIO().MousePos;
        float mouse_frame = std::round(std::clamp((mouse.x - left) / width, 0.f, 1.f) * duration);
        int picked = -1, picked_key = -1, key_curve = -1, key_index = -1;
        float nearest = 7, nearest_key = 7;
        for (unsigned pass = 0; pass < 2; ++pass)
            for (unsigned i = 0; i < curves.size(); ++i) {
                auto &curve = curves[i];
                bool selected = curve.bone == bone && curve.channel == channel;
                if (selected != (pass == 1))
                    continue;
                auto color = selected ? IM_COL32(255, 212, 105, 255) : curve.color;
                for (unsigned k = 1; k < curve.samples.size(); ++k)
                    draw->AddLine({x(duration * (k - 1) / 128), y(curve, curve.samples[k - 1])},
                                  {x(duration * k / 128), y(curve, curve.samples[k])}, color,
                                  selected ? 2.5f : 1.f);
                if (hovered) {
                    float value = curve.curve->sample(mouse_frame, curve.fallback);
                    float distance = std::abs(mouse.y - y(curve, value));
                    if (distance < nearest) {
                        nearest = distance;
                        picked = int(i);
                        picked_key = -1;
                    }
                }
                if (selected || curves.size() < 25)
                    for (unsigned k = 0; k < curve.curve->keys.size(); ++k) {
                        auto &key = curve.curve->keys[k];
                        ImVec2 p{x(key.frame), y(curve, key.value)};
                        draw->AddCircleFilled(p, selected ? 3.f : 2.f, color);
                        if (hovered) {
                            float d = std::hypot(mouse.x - p.x, mouse.y - p.y);
                            if (d <= nearest_key) {
                                nearest_key = d;
                                key_curve = int(i);
                                key_index = int(k);
                            }
                        }
                    }
            }
        if (key_curve >= 0) {
            picked = key_curve;
            picked_key = key_index;
        }
        float cursor = x(std::clamp(playhead, 0.f, duration));
        draw->AddLine({cursor, origin.y}, {cursor, origin.y + size.y - 18},
                      IM_COL32(255, 95, 85, 255), 2);
        draw->AddRectFilled({x(float(first_)), origin.y}, {x(float(last_)), origin.y + size.y - 18},
                            IM_COL32(90, 150, 220, 25));
        if (hovered && picked >= 0) {
            auto &curve = curves[picked];
            ImGui::SetTooltip("%s / %s\nFrame %.0f: %.6g", joints[curve.bone].name.c_str(),
                              channel_names[curve.channel], mouse_frame,
                              curve.curve->sample(mouse_frame, curve.fallback));
        }
        if (clicked) {
            range_drag_ = ImGui::GetIO().KeyShift;
            range_anchor_ = int(mouse_frame);
        }
        if (clicked && range_drag_)
            first_ = last_ = range_anchor_;
        else if (clicked && picked >= 0) {
            auto &curve = curves[picked];
            bone = curve.bone;
            channel = curve.channel;
            show_bones = true;
            result.key = picked_key;
            result.frame =
                picked_key >= 0 ? int(curve.curve->keys[picked_key].frame) : int(mouse_frame);
        }
        if (active && ImGui::IsMouseDown(0)) {
            if (range_drag_) {
                first_ = std::min(range_anchor_, int(mouse_frame));
                last_ = std::max(range_anchor_, int(mouse_frame));
            } else if (!clicked || picked_key < 0)
                result.frame = int(mouse_frame);
        }
        draw->PopClipRect();
        ImGui::SetNextItemWidth(85);
        ImGui::InputInt("First", &first_, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(85);
        ImGui::InputInt("Last", &last_, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(85);
        ImGui::InputInt("Shift", &shift_, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(85);
        ImGui::InputFloat("Time scale", &scale_, 0, 0, "%.3f");
        ImGui::TextWrapped("Range edits affect the shown authored curves across all visible bones. "
                           "Hidden curves and bind-only channels are preserved.");
        auto edit = [&](bool remove) {
            try {
                require(first_ >= 0 && last_ >= first_ && last_ <= motion.frames,
                        "Choose an ordered frame range inside the motion");
                auto next = motion;
                for (auto &curve : curves) {
                    auto track = std::find_if(next.tracks.begin(), next.tracks.end(), [&](auto &t) {
                        return t.name == joints[curve.bone].name;
                    });
                    if (track == next.tracks.end())
                        continue;
                    auto &keys = track->curves[curve.channel];
                    if (remove)
                        std::erase_if(keys.keys, [&](auto &k) {
                            return k.frame >= first_ && k.frame <= last_;
                        });
                    else
                        retime_motion_keys(keys, float(first_), float(last_), float(shift_), scale_,
                                           motion.frames);
                }
                result.edited = std::move(next);
                error_.clear();
            } catch (const std::exception &error) {
                error_ = error.what();
            }
        };
        ImGui::BeginDisabled(curves.empty());
        if (TutorialWidgets::Button("skeletal_tracks", "Retime shown keys"))
            edit(false);
        ImGui::SameLine();
        if (TutorialWidgets::Button("skeletal_tracks", "Delete shown keys"))
            edit(true);
        if (TutorialWidgets::Button("skeletal_tracks", "Copy shown keys"))
            try {
                require(first_ >= 0 && last_ >= first_ && last_ <= motion.frames,
                        "Choose an ordered frame range inside the motion");
                std::map<std::string, JointTrack> copied;
                for (const auto &curve : curves) {
                    auto keys = copy_motion_keys(*curve.curve, float(first_), float(last_));
                    if (keys.empty())
                        continue;
                    auto name = joints[curve.bone].name;
                    auto &track = copied[name];
                    track.name = name;
                    track.axis_angle = tracks.at(name)->axis_angle;
                    track.curves[curve.channel].keys = std::move(keys);
                }
                clipboard_.clear();
                for (auto &[name, track] : copied)
                    clipboard_.push_back(std::move(track));
                error_.clear();
            } catch (const std::exception &error) {
                error_ = error.what();
            }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(85);
        ImGui::InputInt("Paste at", &paste_frame_, 0);
        ImGui::SameLine();
        ImGui::BeginDisabled(clipboard_.empty());
        if (TutorialWidgets::Button("skeletal_tracks", "Paste copied tracks"))
            try {
                auto next = motion;
                for (const auto &copied : clipboard_) {
                    auto joint = std::find_if(joints.begin(), joints.end(), [&](auto &j) {
                        return j.name == copied.name;
                    });
                    require(joint != joints.end(), "A copied bone is missing in this model");
                    auto track = std::find_if(next.tracks.begin(), next.tracks.end(), [&](auto &t) {
                        return t.name == copied.name;
                    });
                    if (track == next.tracks.end()) {
                        next.tracks.push_back({});
                        track = next.tracks.end() - 1;
                        track->name = copied.name;
                        track->axis_angle = copied.axis_angle;
                    }
                    bool rotations = false;
                    for (unsigned c = 3; c < 6; ++c)
                        rotations |= !copied.curves[c].keys.empty();
                    require(!rotations || track->axis_angle == copied.axis_angle,
                            "Copied rotation representation does not match this motion");
                    for (unsigned c = 0; c < 9; ++c)
                        if (!copied.curves[c].keys.empty()) {
                            if (track->curves[c].keys.empty() && paste_frame_ > 0)
                                track->curves[c].keys.push_back(
                                    {0, fallback(*joint, &*track, c), 0});
                            paste_motion_keys(track->curves[c], copied.curves[c].keys,
                                              float(paste_frame_), motion.frames);
                        }
                }
                result.edited = std::move(next);
                error_.clear();
            } catch (const std::exception &error) {
                error_ = error.what();
            }
        ImGui::EndDisabled();
        ImGui::TextDisabled("%zu copied bone tracks; paste uses their names and channels, "
                            "independent of the visibility filters.",
                            clipboard_.size());
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndTable();
    }
    ImGui::End();
    return result;
}
}
