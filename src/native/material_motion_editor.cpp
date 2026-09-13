#include "native/tutorial_widgets.h"
#include "native/material_motion_editor.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cctype>
namespace studio {
namespace {
const char *uv_channels[] = {"Scale U", "Scale V", "Rotation (radians)", "Translation U",
                             "Translation V"};
const char *color_channels[] = {"Red", "Green", "Blue", "Alpha"};
int channel_count(const MaterialTrack &track) {
    return track.kind == MaterialTrack::Kind::TexturePattern     ? 1
           : track.kind == MaterialTrack::Kind::TextureTransform ? 5
                                                                 : 4;
}
std::size_t key_count(const MaterialTrack &track, int channel) {
    return track.kind == MaterialTrack::Kind::TexturePattern ? track.textures.size()
                                                             : track.curves[channel].keys.size();
}
const char *channel_name(const MaterialTrack &track, int channel) {
    return track.kind == MaterialTrack::Kind::TexturePattern     ? "Texture switch"
           : track.kind == MaterialTrack::Kind::TextureTransform ? uv_channels[channel]
                                                                 : color_channels[channel];
}
int first_keyed_channel(const MaterialTrack &track) {
    for (int channel = 0; channel < channel_count(track); ++channel)
        if (key_count(track, channel))
            return channel;
    return 0;
}
const char *kinds[] = {"UV transform", "Constant color", "Texture switch"};
std::string label(const MaterialTrack &t) {
    return t.material + " / " + kinds[int(t.kind)] + " / " + std::to_string(t.slot);
}
}
void MaterialMotionEditor::draw(MaterialDocument &doc, ModelDocument &preview,
                                EnvironmentRenderer &renderer, bool &playing, bool &repeat) {
    bool map = preview.area >= 0;
    if (map) {
        preview.motions = doc.model.motions;
        if (preview.motions.empty()) {
            ImGui::TextWrapped("This map resource has no existing editable local or daily material "
                               "motion. Choose another terrain or static model.");
            return;
        }
        if (preview.motion < 0 || std::size_t(preview.motion) >= preview.motions.size())
            preview.motion = 0;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##map-motion", preview.motions[preview.motion].name.c_str())) {
            for (unsigned i = 0; i < preview.motions.size(); ++i)
                if (ImGui::Selectable(preview.motions[i].name.c_str(), preview.motion == int(i))) {
                    preview.motion = int(i);
                    doc.model.motion = int(i);
                }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("Shared motion: changes affect matching materials and placements.");
    }
    if (preview.motion < 0) {
        ImGui::TextWrapped("Choose a motion in the viewport to edit its material animation.");
        return;
    }
    auto index = std::size_t(preview.motion);
    bool daily = map && preview.motions[index].daily;
    auto motion = preview.motions.at(index).material;
    auto identity = doc.identity() + "/" + std::to_string(preview.motions[index].group) + "/" +
                    std::to_string(preview.motions[index].slot);
    if (identity_ != identity) {
        identity_ = identity;
        renderer.playback.materials = true;
        track_.clear();
        filter_[0] = 0;
        channel_ = 0;
        key_frame_ = 0;
        value_ = slope_ = 0;
        error_.clear();
    }
    if (!map)
        ImGui::TextWrapped("%s", preview.motions[index].name.c_str());
    auto scrub = [&](int frame) {
        playing = false;
        repeat = false;
        preview.select_motion(preview.motion, false);
        if (daily)
            renderer.lighting.hour = float(frame) * 24 / motion.frames;
        else
            renderer.playback.seconds = frame / 30.;
        renderer.playback.materials = true;
        renderer.refresh_materials();
    };
    auto clock = motion;
    clock.looping = repeat;
    int frame =
        int(animation_frame(clock, renderer.playback.seconds, daily, renderer.lighting.hour));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##motion-frame", &frame, 0, int(motion.frames), "Frame %d"))
        scrub(frame);
    if (daily) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##motion-hour", &renderer.lighting.hour, 0, 24,
                               "Time of day: %.2f h")) {
            playing = false;
            renderer.playback.materials = true;
        }
        ImGui::TextDisabled("%u frames span 24 hours", unsigned(motion.frames));
    } else
        ImGui::TextDisabled("%u frames | 30 fps", unsigned(motion.frames));
    if (preview.looping_overlay >= 0)
        ImGui::TextWrapped(
            "A looping overlay is also active. Select it in the viewport to edit its tracks.");
    auto apply = [&](MaterialMotion next) {
        try {
            auto selected = track_;
            doc.edit_material_motion(index, next);
            preview = doc.model;
            preview.select_motion(int(index), repeat);
            if (map)
                renderer.refresh_materials();
            else
                renderer.set_scene(preview.scene);
            renderer.playback.materials = true;
            track_ = selected;
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    };
    auto local = [&](const MaterialTrack &t) {
        return std::any_of(preview.scene->materials.begin(), preview.scene->materials.end(),
                           [&](auto &m) {
                               return m.name == t.material;
                           });
    };
    auto select_channel = [&](const MaterialTrack &track, int channel) {
        track_ = label(track);
        channel_ = channel;
        key_frame_ = 0;
        value_ = slope_ = 0;
        texture_.clear();
        if (track.kind == MaterialTrack::Kind::TexturePattern) {
            if (!track.textures.empty()) {
                key_frame_ = int(track.textures.front().frame);
                texture_ = track.textures.front().texture;
            }
        } else if (!track.curves[channel].keys.empty()) {
            const auto &key = track.curves[channel].keys.front();
            key_frame_ = int(key.frame);
            value_ = key.value;
            slope_ = key.slope;
        }
    };
    ImGui::SeparatorText("Keyed channels");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##motion-search", "Search materials or channels", filter_,
                             sizeof(filter_));
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        return text;
    };
    const auto search = lower(filter_);
    unsigned rows = 0;
    if (ImGui::BeginTable("Keyed material channels", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp,
                          {0, ImGui::GetTextLineHeightWithSpacing() * 8})) {
        ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (unsigned i = 0; i < motion.tracks.size(); ++i) {
            const auto &track = motion.tracks[i];
            if (!local(track))
                continue;
            for (int channel = 0; channel < channel_count(track); ++channel) {
                auto count = key_count(track, channel);
                if (!count)
                    continue;
                auto name =
                    std::string(channel_name(track, channel)) + " / " +
                    (track.kind == MaterialTrack::Kind::ConstantColor ? "constant " : "unit ") +
                    std::to_string(track.slot);
                if (lower(track.material + " " + name).find(search) == std::string::npos)
                    continue;
                ++rows;
                ImGui::PushID(int(i));
                ImGui::PushID(channel);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(track.material.c_str(),
                                      track_ == label(track) && channel_ == channel,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    select_channel(track, channel);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\n%s: %zu keys", track.material.c_str(), name.c_str(),
                                      count);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%zu", count);
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (!rows)
        ImGui::TextDisabled(filter_[0] ? "No matching keyed channels."
                                       : "No keyed channels on this model.");
    if (studio::TutorialWidgets::TreeNode("material_motion_editor", "Add material track")) {
        material_ = std::clamp(material_, 0, int(preview.scene->materials.size()) - 1);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##track-material",
                              preview.scene->materials[material_].name.c_str())) {
            for (unsigned i = 0; i < preview.scene->materials.size(); ++i)
                if (ImGui::Selectable(preview.scene->materials[i].name.c_str(),
                                      material_ == int(i)))
                    material_ = int(i);
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##track-kind", &kind_, kinds, 3);
        slot_ = std::clamp(slot_, 0, kind_ == 1 ? 5 : 2);
        ImGui::SetNextItemWidth(-65);
        ImGui::SliderInt(kind_ == 1 ? "Constant" : "Unit", &slot_, 0, kind_ == 1 ? 5 : 2);
        ImGui::TextWrapped(kind_ == 1 ? "Choose a constant used by this material's combiner."
                                      : "Choose a texture unit already used by this material.");
        if (studio::TutorialWidgets::Button("material_motion_editor", "Add track")) {
            auto next = motion;
            MaterialTrack t;
            t.material = preview.scene->materials[material_].name;
            t.kind = MaterialTrack::Kind(kind_);
            t.slot = unsigned(slot_);
            if (kind_ == 0) {
                auto transform = preview.scene->materials[material_].inputs.at(t.slot).transform;
                for (unsigned i = 0; i < 5; ++i)
                    t.curves[i].keys.push_back({0, transform[i], 0});
            } else if (kind_ == 1) {
                auto color = doc.edits().at(material_).colors.at(t.slot);
                for (unsigned i = 0; i < 4; ++i)
                    t.curves[i].keys.push_back({0, color[i], 0});
            } else {
                auto name = preview.scene->materials[material_].texture_inputs.at(t.slot);
                if (name.starts_with(preview.texture_prefix))
                    t.textures.push_back({0, name.substr(preview.texture_prefix.size())});
            }
            track_ = label(t);
            next.tracks.push_back(t);
            apply(next);
        }
        ImGui::TreePop();
    }
    auto found = std::find_if(motion.tracks.begin(), motion.tracks.end(), [&](auto &t) {
        return label(t) == track_;
    });
    if (found == motion.tracks.end()) {
        found = std::find_if(motion.tracks.begin(), motion.tracks.end(), [&](const auto &track) {
            return local(track) && key_count(track, first_keyed_channel(track)) > 0;
        });
        if (found == motion.tracks.end())
            found = std::find_if(motion.tracks.begin(), motion.tracks.end(), local);
        if (found != motion.tracks.end())
            select_channel(*found, first_keyed_channel(*found));
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##material-track",
                          found == motion.tracks.end() ? "No material tracks" : track_.c_str())) {
        for (auto &t : motion.tracks)
            if (local(t) && ImGui::Selectable(label(t).c_str(), label(t) == track_)) {
                select_channel(t, first_keyed_channel(t));
            }
        ImGui::EndCombo();
    }
    found = std::find_if(motion.tracks.begin(), motion.tracks.end(), [&](auto &t) {
        return label(t) == track_;
    });
    if (found != motion.tracks.end()) {
        auto track_index = std::size_t(found - motion.tracks.begin());
        auto &t = *found;
        bool pattern = t.kind == MaterialTrack::Kind::TexturePattern;
        if (!pattern) {
            channel_ = std::clamp(channel_, 0, channel_count(t) - 1);
            auto channel_label = [&](int channel) {
                auto count = key_count(t, channel);
                return std::string(channel_name(t, channel)) + " (" +
                       (count ? std::to_string(count) + " keys" : "no keys") + ")";
            };
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##motion-channel", channel_label(channel_).c_str())) {
                for (int channel = 0; channel < channel_count(t); ++channel)
                    if (ImGui::Selectable(channel_label(channel).c_str(), channel_ == channel))
                        select_channel(t, channel);
                ImGui::EndCombo();
            }
        }
        ImGui::BeginChild("Motion keys", {0, 100}, ImGuiChildFlags_Borders);
        if (pattern) {
            for (auto &k : t.textures) {
                auto text = std::to_string(k.frame) + ": " + k.texture;
                if (ImGui::Selectable(text.c_str(), key_frame_ == int(k.frame))) {
                    key_frame_ = int(k.frame);
                    texture_ = k.texture;
                    scrub(key_frame_);
                }
            }
        } else {
            if (t.curves[channel_].keys.empty())
                ImGui::TextWrapped("No keys: uses the base material value.");
            for (auto &k : t.curves[channel_].keys) {
                char text[100];
                std::snprintf(text, sizeof(text), "%g: %.5g (slope %.5g)", k.frame, k.value,
                              k.slope);
                if (ImGui::Selectable(text, key_frame_ == int(k.frame))) {
                    key_frame_ = int(k.frame);
                    value_ = k.value;
                    slope_ = k.slope;
                    scrub(key_frame_);
                }
            }
        }
        ImGui::EndChild();
        ImGui::SetNextItemWidth(-80);
        ImGui::InputInt("Key frame", &key_frame_);
        if (studio::TutorialWidgets::Button("material_motion_editor", "Use current frame"))
            key_frame_ = frame;
        if (pattern) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##key-texture",
                                  texture_.empty() ? "Choose texture" : texture_.c_str())) {
                for (auto &[name, image] : preview.scene->textures)
                    if (name.starts_with(preview.texture_prefix)) {
                        auto raw = name.substr(preview.texture_prefix.size());
                        if (ImGui::Selectable(raw.c_str(), raw == texture_))
                            texture_ = raw;
                    }
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("Texture switches start at frame 0 and hold until the next key.");
        } else {
            ImGui::SetNextItemWidth(-65);
            ImGui::InputFloat("Value", &value_, 0, 0, "%.5f");
            ImGui::SetNextItemWidth(-65);
            ImGui::InputFloat("Slope", &slope_, 0, 0, "%.5f");
            ImGui::TextWrapped(
                "Slope: change per frame. One key is constant; no keys use the base material.");
            auto &curve = t.curves[channel_];
            float points[128];
            for (unsigned i = 0; i < 128; ++i)
                points[i] = curve.sample(motion.frames * i / 127, 0);
            ImGui::PlotLines("##curve", points, 128, 0, nullptr, FLT_MAX, FLT_MAX, {-1, 45});
        }
        bool valid =
            key_frame_ >= 0 && key_frame_ <= motion.frames && (!pattern || !texture_.empty());
        ImGui::BeginDisabled(!valid);
        if (studio::TutorialWidgets::Button("material_motion_editor", "Set key")) {
            auto next = motion;
            auto &target = next.tracks[track_index];
            if (pattern) {
                std::erase_if(target.textures, [&](auto &k) {
                    return k.frame == unsigned(key_frame_);
                });
                target.textures.push_back({unsigned(key_frame_), texture_});
                std::sort(target.textures.begin(), target.textures.end(), [](auto &a, auto &b) {
                    return a.frame < b.frame;
                });
            } else {
                auto &keys = target.curves[channel_].keys;
                std::erase_if(keys, [&](auto &k) {
                    return k.frame == key_frame_;
                });
                if (keys.empty() && key_frame_)
                    keys.push_back({0, value_, 0});
                keys.push_back({float(key_frame_), value_, slope_});
                std::sort(keys.begin(), keys.end(), [](auto &a, auto &b) {
                    return a.frame < b.frame;
                });
            }
            apply(next);
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("material_motion_editor", "Delete key")) {
            auto next = motion;
            auto &target = next.tracks[track_index];
            if (pattern)
                std::erase_if(target.textures, [&](auto &k) {
                    return k.frame == unsigned(key_frame_);
                });
            else {
                auto &keys = target.curves[channel_].keys;
                std::erase_if(keys, [&](auto &k) {
                    return k.frame == key_frame_;
                });
                if (keys.size() == 1)
                    keys[0].frame = 0;
            }
            apply(next);
        }
        ImGui::EndDisabled();
        if (studio::TutorialWidgets::Button("material_motion_editor", "Remove track")) {
            auto next = motion;
            next.tracks.erase(next.tracks.begin() + track_index);
            apply(next);
        }
    }
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
}
}
