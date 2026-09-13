#pragma once
#include <algorithm>
#include <chrono>
namespace studio {
struct FramePacing {
    bool limit_enabled = false, background_enabled = true;
    int fps = 60, background_fps = 15;
    int limit(bool focused) const {
        int active = limit_enabled ? std::clamp(fps, 1, 1000) : 0;
        if (!focused && background_enabled) {
            int background = std::clamp(background_fps, 1, 1000);
            return active ? std::min(active, background) : background;
        }
        return active;
    }
    std::chrono::nanoseconds remaining(std::chrono::steady_clock::time_point start,
                                       std::chrono::steady_clock::time_point now,
                                       bool focused) const {
        int cap = limit(focused);
        if (!cap)
            return std::chrono::nanoseconds::zero();
        auto budget = std::chrono::nanoseconds(1000000000 / cap);
        return std::max(std::chrono::nanoseconds::zero(),
                        budget - std::chrono::duration_cast<std::chrono::nanoseconds>(now - start));
    }
};
}
