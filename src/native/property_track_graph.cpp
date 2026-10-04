#include "native/property_track_graph.h"
#include "native/tutorial_widgets.h"
#include "assets/motion_key_edit.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
namespace studio {
namespace {
std::string lower(std::string text) {
    for (auto &c : text)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
ImU32 color(unsigned index) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(std::fmod(index * .618034f, 1.f), .55f, .95f, r, g, b);
    return ImGui::ColorConvertFloat4ToU32({r, g, b, 1});
}
void validate_range(const PropertyGraphResult &edit, float duration) {
    require(edit.first >= 0 && edit.last >= edit.first && edit.last <= duration,
            "Choose an ordered frame range inside the motion");
}
}
MaterialMotion edit_material_graph_range(const MaterialMotion &motion,
                                         const std::vector<PropertyGraphTrack> &rows,
                                         const PropertyGraphResult &edit) {
    validate_range(edit, motion.frames);
    require(edit.action == PropertyGraphResult::Action::Retime ||
                edit.action == PropertyGraphResult::Action::Delete,
            "Choose a material curve operation");
    auto next = motion;
    for (int index : edit.shown) {
        const auto &row = rows.at(index);
        if (!row.curve)
            continue;
        auto &curve = next.tracks.at(row.source).curves.at(row.channel);
        if (edit.action == PropertyGraphResult::Action::Delete)
            std::erase_if(curve.keys, [&](auto &key) {
                return key.frame >= edit.first && key.frame <= edit.last;
            });
        else
            retime_motion_keys(curve, float(edit.first), float(edit.last), float(edit.shift),
                               edit.scale, motion.frames);
    }
    return next;
}
VisibilityMotion edit_visibility_graph_range(const VisibilityMotion &motion,
                                             const std::vector<PropertyGraphTrack> &rows,
                                             const PropertyGraphResult &edit) {
    validate_range(edit, motion.clock.frames);
    require(edit.action == PropertyGraphResult::Action::Show ||
                edit.action == PropertyGraphResult::Action::Hide,
            "Choose whether the meshes should be shown or hidden");
    auto next = motion;
    for (int index : edit.shown) {
        const auto &row = rows.at(index);
        auto track = std::find_if(next.tracks.begin(), next.tracks.end(), [&](auto &t) {
            return t.mesh == row.name;
        });
        if (track == next.tracks.end()) {
            next.tracks.push_back({row.name, {}});
            track = next.tracks.end() - 1;
        }
        track->frames.resize(std::size_t(motion.clock.frames) + 1, true);
        std::fill(track->frames.begin() + edit.first, track->frames.begin() + edit.last + 1,
                  edit.action == PropertyGraphResult::Action::Show);
    }
    return next;
}
PropertyGraphResult PropertyTrackGraph::draw(const std::vector<PropertyGraphTrack> &tracks,
                                             const std::string &identity, float duration,
                                             float playhead, const std::string &selected,
                                             bool visibility, bool &playing,
                                             const std::string &error) {
    PropertyGraphResult result;
    if (visibility) {
        if (TutorialWidgets::Button("property_tracks", "All visibility tracks..."))
            open_ = true;
    } else if (TutorialWidgets::Button("property_tracks", "All material tracks..."))
        open_ = true;
    if (identity_ != identity) {
        identity_ = identity;
        shown_.clear();
        search_[0] = 0;
        first_ = 0;
        last_ = int(duration);
        range_drag_ = false;
    }
    if (!open_)
        return result;
    ImGui::SetNextWindowSize({1100, 740}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(visibility ? "Visibility track graph" : "Material track graph", &open_)) {
        ImGui::End();
        return result;
    }
    if (ImGui::Button(playing ? "Pause" : "Play"))
        playing = !playing;
    ImGui::SameLine();
    if (ImGui::Button("Restart"))
        result.frame = 0;
    if (!visibility) {
        ImGui::SameLine();
        ImGui::Checkbox("Normalize each curve", &normalized_);
        for (int g = 0; g < 3; ++g) {
            ImGui::SameLine();
            ImGui::Checkbox(g == 0 ? "UV" : g == 1 ? "Color" : "Texture switches", &groups_[g]);
        }
    }
    ImGui::TextWrapped("Click a track to select it in the editor. Drag to scrub the model; "
                       "Shift-drag selects an inclusive frame range across displayed tracks.");
    if (!visibility)
        ImGui::TextDisabled(normalized_ ? "Curves have individual scales; hover for actual values."
                                        : "UV and color curves use separate shared value scales.");
    float body_height = std::max(200.f, ImGui::GetContentRegionAvail().y - 130);
    if (ImGui::BeginTable("Property tracks", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthFixed, 310);
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Button("Show all"))
            for (auto &t : tracks)
                shown_[t.name] = true;
        ImGui::SameLine();
        if (ImGui::Button("Hide all"))
            for (auto &t : tracks)
                shown_[t.name] = false;
        ImGui::SameLine();
        if (ImGui::Button("Only selected"))
            for (auto &t : tracks)
                shown_[t.name] = t.name == selected;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "Filter tracks", search_, sizeof(search_));
        auto needle = lower(search_);
        ImGui::BeginChild("Property track list", {0, body_height - 60}, ImGuiChildFlags_Borders);
        for (unsigned i = 0; i < tracks.size(); ++i) {
            auto &t = tracks[i];
            auto [it, inserted] = shown_.try_emplace(t.name, true);
            if ((!visibility && !groups_.at(t.group)) ||
                (!needle.empty() && lower(t.name).find(needle) == std::string::npos))
                continue;
            ImGui::PushID(int(i));
            ImGui::Checkbox("##shown", &it->second);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, color(i));
            if (ImGui::Selectable(t.name.c_str(), selected == t.name))
                result.selected = int(i);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", t.name.c_str());
            ImGui::PopID();
            if (it->second)
                result.shown.push_back(int(i));
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginChild("Property graph canvas", {0, body_height}, ImGuiChildFlags_Borders);
        const float span = std::max(1.f, duration);
        float width = std::max(120.f, ImGui::GetContentRegionAvail().x), left = 52,
              plot = width - 62;
        struct Lane {
            std::string name;
            std::vector<int> rows;
            bool discrete;
        };
        std::vector<Lane> lanes;
        for (int group = 0; group < 2 && !visibility; ++group) {
            Lane lane{group == 0 ? "UV transforms" : "Material colors", {}, false};
            for (int i : result.shown)
                if (tracks[i].curve && tracks[i].group == group)
                    lane.rows.push_back(i);
            if (!lane.rows.empty())
                lanes.push_back(std::move(lane));
        }
        for (int i : result.shown)
            if (!tracks[i].curve)
                lanes.push_back({tracks[i].name, {i}, true});
        if (lanes.empty())
            ImGui::TextWrapped("No displayed tracks. Enable tracks or clear the filter.");
        for (unsigned lane_index = 0; lane_index < lanes.size(); ++lane_index) {
            auto &lane = lanes[lane_index];
            ImGui::PushID(int(lane_index));
            ImGui::TextUnformatted(lane.name.c_str());
            auto origin = ImGui::GetCursorScreenPos();
            float height = lane.discrete ? 48.f : 175.f;
            ImGui::InvisibleButton("##lane", {width, height});
            bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive(),
                 clicked = ImGui::IsItemClicked();
            auto *draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(origin, {origin.x + width, origin.y + height}, true);
            draw->AddRectFilled(origin, {origin.x + width, origin.y + height},
                                IM_COL32(20, 29, 35, 255));
            auto x = [&](float f) {
                return origin.x + left + f / span * plot;
            };
            float at = std::clamp(std::round((ImGui::GetIO().MousePos.x - x(0)) / plot * span), 0.f,
                                  duration);
            for (int tick = 0; tick <= 4; ++tick) {
                float f = duration * tick / 4;
                draw->AddLine({x(f), origin.y}, {x(f), origin.y + height},
                              IM_COL32(50, 62, 73, 255));
                char text[24];
                std::snprintf(text, sizeof(text), "%.0f", f);
                draw->AddText({x(f) - 5, origin.y + height - 15}, IM_COL32(180, 190, 200, 255),
                              text);
            }
            int picked = -1;
            float distance = 8;
            if (lane.discrete) {
                int index = lane.rows.front();
                auto &t = tracks[index];
                picked = index;
                for (unsigned k = 0; k < t.steps.size(); ++k) {
                    auto &step = t.steps[k];
                    float end = k + 1 < t.steps.size() ? t.steps[k + 1].frame : span;
                    auto fill = visibility && step.label == "Hidden" ? IM_COL32(90, 65, 70, 255)
                                                                     : color(k + index);
                    draw->AddRectFilled({x(step.frame), origin.y + 3}, {x(end), origin.y + 28},
                                        fill);
                    draw->AddLine({x(step.frame), origin.y + 3}, {x(step.frame), origin.y + 28},
                                  IM_COL32(220, 230, 240, 255));
                    draw->PushClipRect({x(step.frame) + 2, origin.y + 3}, {x(end), origin.y + 28},
                                       true);
                    draw->AddText({x(step.frame) + 3, origin.y + 7},
                                  visibility && step.label == "Hidden"
                                      ? IM_COL32(240, 220, 220, 255)
                                      : IM_COL32(15, 20, 25, 255),
                                  step.label.c_str());
                    draw->PopClipRect();
                    if (hovered && at >= step.frame && (at < end || k + 1 == t.steps.size()))
                        ImGui::SetTooltip("%s\nFrame %.0f: %s", t.name.c_str(), at,
                                          step.label.c_str());
                }
            } else {
                struct Samples {
                    int index;
                    std::array<float, 129> values;
                    float low, high;
                };
                std::vector<Samples> samples;
                float low = 0, high = 0;
                for (int index : lane.rows) {
                    Samples s{index, {}, 0, 0};
                    for (unsigned k = 0; k < s.values.size(); ++k) {
                        float v = tracks[index].curve->sample(duration * k / 128.f, 0);
                        s.values[k] = std::isfinite(v) ? v : 0;
                        if (!k)
                            s.low = s.high = s.values[k];
                        s.low = std::min(s.low, s.values[k]);
                        s.high = std::max(s.high, s.values[k]);
                    }
                    float margin = std::max(.001f, (s.high - s.low) * .08f);
                    s.low -= margin;
                    s.high += margin;
                    if (samples.empty()) {
                        low = s.low;
                        high = s.high;
                    }
                    low = std::min(low, s.low);
                    high = std::max(high, s.high);
                    samples.push_back(s);
                }
                std::stable_sort(samples.begin(), samples.end(), [&](auto &a, auto &b) {
                    return (tracks[a.index].name == selected) < (tracks[b.index].name == selected);
                });
                for (auto &s : samples) {
                    auto &t = tracks[s.index];
                    auto y = [&](float v) {
                        float lo = normalized_ ? s.low : low, hi = normalized_ ? s.high : high;
                        return origin.y + 5 + (hi - v) / (hi - lo) * (height - 25);
                    };
                    bool chosen = selected == t.name;
                    auto ink = chosen ? IM_COL32(255, 212, 105, 255) : color(s.index);
                    for (unsigned k = 1; k < s.values.size(); ++k)
                        draw->AddLine({x(duration * (k - 1) / 128), y(s.values[k - 1])},
                                      {x(duration * k / 128), y(s.values[k])}, ink,
                                      chosen ? 2.5f : 1.3f);
                    if (chosen || samples.size() < 25)
                        for (auto &key : t.curve->keys)
                            draw->AddCircleFilled({x(key.frame), y(key.value)}, 3, ink);
                    float d = std::abs(ImGui::GetIO().MousePos.y - y(t.curve->sample(at, 0)));
                    if (d <= distance) {
                        distance = d;
                        picked = s.index;
                    }
                }
                if (hovered && picked >= 0)
                    ImGui::SetTooltip("%s\nFrame %.0f: %.6g", tracks[picked].name.c_str(), at,
                                      tracks[picked].curve->sample(at, 0));
                char values[64];
                std::snprintf(values, sizeof(values), "%.3g", normalized_ ? 1.f : high);
                draw->AddText({origin.x + 2, origin.y + 2}, IM_COL32(180, 190, 200, 255), values);
                std::snprintf(values, sizeof(values), "%.3g", normalized_ ? 0.f : low);
                draw->AddText({origin.x + 2, origin.y + height - 30}, IM_COL32(180, 190, 200, 255),
                              values);
            }
            draw->AddRectFilled({x(float(first_)), origin.y},
                                {x(float(last_)), origin.y + height - 16},
                                IM_COL32(90, 150, 220, 30));
            draw->AddLine({x(playhead), origin.y}, {x(playhead), origin.y + height},
                          IM_COL32(255, 95, 85, 255), 2);
            if (selected == tracks[lane.rows.front()].name && lane.discrete)
                draw->AddRect(origin, {origin.x + width, origin.y + height},
                              IM_COL32(255, 212, 105, 255));
            if (clicked) {
                range_drag_ = ImGui::GetIO().KeyShift;
                anchor_ = int(at);
                if (!range_drag_ && picked >= 0)
                    result.selected = picked;
            }
            if (active && ImGui::IsMouseDown(0)) {
                if (range_drag_) {
                    first_ = std::min(anchor_, int(at));
                    last_ = std::max(anchor_, int(at));
                } else
                    result.frame = int(at);
            }
            draw->PopClipRect();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::Text("%zu displayed tracks", result.shown.size());
    ImGui::SetNextItemWidth(75);
    ImGui::InputInt("First", &first_, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(75);
    ImGui::InputInt("Last", &last_, 0);
    if (!visibility) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(75);
        ImGui::InputInt("Shift", &shift_, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(75);
        ImGui::InputFloat("Time scale", &scale_, 0, 0, "%.3f");
    }
    bool numeric = std::any_of(result.shown.begin(), result.shown.end(), [&](int i) {
        return tracks[i].curve != nullptr;
    });
    ImGui::BeginDisabled(result.shown.empty() || (!visibility && !numeric));
    if (visibility) {
        if (TutorialWidgets::Button("property_tracks", "Show displayed meshes in range"))
            result.action = PropertyGraphResult::Action::Show;
        ImGui::SameLine();
        if (TutorialWidgets::Button("property_tracks", "Hide displayed meshes in range"))
            result.action = PropertyGraphResult::Action::Hide;
    } else {
        if (TutorialWidgets::Button("property_tracks", "Retime displayed curves"))
            result.action = PropertyGraphResult::Action::Retime;
        ImGui::SameLine();
        if (TutorialWidgets::Button("property_tracks", "Delete displayed curve keys"))
            result.action = PropertyGraphResult::Action::Delete;
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped(visibility ? "Range changes affect displayed mesh tracks, including meshes "
                                    "without authored tracks."
                                  : "Range edits affect displayed UV/color curves. Select a "
                                    "texture-switch lane to edit its keys in the inspector.");
    if (!error.empty())
        ImGui::TextWrapped("%s", error.c_str());
    result.first = first_;
    result.last = last_;
    result.shift = shift_;
    result.scale = scale_;
    ImGui::End();
    return result;
}
}
