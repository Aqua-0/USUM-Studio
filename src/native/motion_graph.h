#pragma once
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
namespace studio {
struct MotionGraphKey {
    float frame, value;
};
struct MotionGraphResult {
    int key = -1;
    int frame = -1;
};
template <class Sample>
MotionGraphResult motion_graph(const char *title, float duration, float playhead,
                               const std::vector<MotionGraphKey> &keys, Sample sample,
                               bool &expanded, bool stepped = false) {
    ImGui::Checkbox("Expanded timeline", &expanded);
    const bool window = expanded;
    bool visible = true;
    if (expanded) {
        ImGui::SetNextWindowSize({760, 300}, ImGuiCond_FirstUseEver);
        visible = ImGui::Begin(title, &expanded);
    }
    MotionGraphResult result;
    if (visible) {
        ImGui::TextDisabled("Click key: select | Drag: scrub");
        const auto origin = ImGui::GetCursorScreenPos();
        ImVec2 size(std::max(100.f, ImGui::GetContentRegionAvail().x),
                    expanded ? std::max(140.f, ImGui::GetContentRegionAvail().y) : 175.f);
        ImGui::InvisibleButton("##motion-graph", size);
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(20, 29, 35, 255));
        const float left = origin.x + 42, top = origin.y + 10;
        const float width = std::max(1.f, size.x - 52), height = size.y - 36;
        duration = std::max(1.f, duration);
        float values[257], low = 0, high = 0;
        for (int i = 0; i <= 256; ++i) {
            values[i] = sample(duration * i / 256);
            if (!std::isfinite(values[i]))
                values[i] = 0;
            if (!i)
                low = high = values[i];
            low = std::min(low, values[i]);
            high = std::max(high, values[i]);
        }
        for (auto &key : keys)
            if (std::isfinite(key.value)) {
                low = std::min(low, key.value);
                high = std::max(high, key.value);
            }
        float margin = std::max(.1f, (high - low) * .1f);
        low -= margin;
        high += margin;
        auto x = [&](float frame) {
            return left + frame / duration * width;
        };
        auto y = [&](float value) {
            return top + (high - value) / (high - low) * height;
        };
        for (int i = 0; i <= 4; ++i) {
            float px = left + width * i / 4, py = top + height * i / 4;
            draw->AddLine({px, top}, {px, top + height}, IM_COL32(55, 67, 75, 255));
            draw->AddLine({left, py}, {left + width, py}, IM_COL32(55, 67, 75, 255));
            char label[32];
            std::snprintf(label, sizeof(label), "%.0f", duration * i / 4);
            draw->AddText({std::clamp(px - 6, origin.x, origin.x + size.x - 25), top + height + 3},
                          IM_COL32(170, 185, 195, 255), label);
            std::snprintf(label, sizeof(label), "%.3g", high - (high - low) * i / 4);
            draw->AddText({origin.x + 2, py - 6}, IM_COL32(170, 185, 195, 255), label);
        }
        if (stepped) {
            float previous_frame = 0, previous_value = sample(0);
            for (auto &key : keys) {
                draw->AddLine({x(previous_frame), y(previous_value)},
                              {x(key.frame), y(previous_value)}, IM_COL32(85, 209, 184, 255), 2);
                draw->AddLine({x(key.frame), y(previous_value)}, {x(key.frame), y(key.value)},
                              IM_COL32(85, 209, 184, 255), 2);
                previous_frame = key.frame;
                previous_value = key.value;
            }
            draw->AddLine({x(previous_frame), y(previous_value)}, {x(duration), y(previous_value)},
                          IM_COL32(85, 209, 184, 255), 2);
        } else {
            for (int i = 1; i <= 256; ++i)
                draw->AddLine({x(duration * (i - 1) / 256), y(values[i - 1])},
                              {x(duration * i / 256), y(values[i])}, IM_COL32(85, 209, 184, 255),
                              2);
        }
        int hovered = -1;
        float nearest = 64;
        auto mouse = ImGui::GetIO().MousePos;
        for (int i = 0; i < int(keys.size()); ++i) {
            auto &key = keys[i];
            if (!std::isfinite(key.value))
                continue;
            ImVec2 p(x(key.frame), y(key.value));
            float d = (mouse.x - p.x) * (mouse.x - p.x) + (mouse.y - p.y) * (mouse.y - p.y);
            if (d < nearest) {
                nearest = d;
                hovered = i;
            }
            draw->AddQuadFilled({p.x, p.y - 4}, {p.x + 4, p.y}, {p.x, p.y + 4}, {p.x - 4, p.y},
                                IM_COL32(245, 194, 93, 255));
        }
        float cursor = x(std::clamp(playhead, 0.f, duration));
        draw->AddLine({cursor, top}, {cursor, top + height}, IM_COL32(245, 105, 92, 255), 2);
        if (ImGui::IsItemHovered() && hovered >= 0)
            ImGui::SetTooltip("Frame %.0f | %.5g", keys[hovered].frame, keys[hovered].value);
        if (ImGui::IsItemClicked() && hovered >= 0) {
            result.key = hovered;
            result.frame = int(keys[hovered].frame);
        } else if (ImGui::IsItemActive() && ImGui::IsMouseDown(0))
            result.frame =
                int(std::round(std::clamp((mouse.x - left) / width, 0.f, 1.f) * duration));
        draw->PopClipRect();
    }
    // Begin requires End even when the close button changes expanded.
    if (window)
        ImGui::End();
    return result;
}
}
