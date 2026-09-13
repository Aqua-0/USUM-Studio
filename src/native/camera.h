#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace studio {
struct ViewportCamera {
    using Position = std::array<float, 3>;
    Position target{}, bounds_low{-1500, -1500, -1500}, bounds_high{1500, 1500, 1500};
    float distance = 3000, yaw = 0, pitch = .85f, extent = 3000, speed = 600, fast_multiplier = 4;
    Position forward() const {
        return {-std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                -std::cos(yaw) * std::cos(pitch)};
    }
    Position right() const {
        return {std::cos(yaw), 0, -std::sin(yaw)};
    }
    Position up() const {
        return {-std::sin(yaw) * std::sin(pitch), std::cos(pitch),
                -std::cos(yaw) * std::sin(pitch)};
    }
    Position eye() const {
        auto result = target, f = forward();
        for (unsigned i = 0; i < 3; ++i)
            result[i] -= f[i] * distance;
        return result;
    }
    void fit(const Position &low, const Position &high) {
        bounds_low = low;
        bounds_high = high;
        for (unsigned i = 0; i < 3; ++i)
            target[i] = (low[i] + high[i]) * .5f;
        extent = std::max({high[0] - low[0], high[1] - low[1], high[2] - low[2], 10.f});
        distance = extent * 1.3f;
        speed = std::clamp(extent * .2f, 1.f, 100000.f);
    }
    void focus_start(const Position &position) {
        target = position;
        target[1] += 80;
        distance = 1000;
        yaw = 0;
        pitch = .85f;
        speed = 400;
    }
    void rotate(float dx, float dy, bool from_position) {
        auto position = eye();
        yaw -= dx * .006f;
        pitch = std::clamp(pitch + dy * .006f, -1.5f, 1.5f);
        if (from_position) {
            auto f = forward();
            for (unsigned i = 0; i < 3; ++i)
                target[i] = position[i] + f[i] * distance;
        }
    }
    void pan(float dx, float dy) {
        auto r = right(), u = up();
        for (unsigned i = 0; i < 3; ++i)
            target[i] += distance * .001f * (-dx * r[i] + dy * u[i]);
    }
    void fly(float sideways, float vertical, float ahead, float seconds, float multiplier) {
        float length = std::sqrt(sideways * sideways + vertical * vertical + ahead * ahead);
        if (length == 0)
            return;
        auto r = right(), u = up(), f = forward();
        auto step = speed * multiplier * std::clamp(seconds, 0.f, .1f) / length;
        for (unsigned i = 0; i < 3; ++i)
            target[i] += (sideways * r[i] + vertical * u[i] + ahead * f[i]) * step;
    }
    float near_clip() const {
        auto p = eye();
        float squared = 0;
        for (unsigned i = 0; i < 3; ++i) {
            float gap = std::max({bounds_low[i] - p[i], p[i] - bounds_high[i], 0.f});
            squared += gap * gap;
        }
        return std::clamp(std::sqrt(squared) * .1f, 1.f, 1000.f);
    }
    float far_clip() const {
        return std::max({extent * 100, distance * 10, 1000000.f});
    }
    void adjust_fast_speed(float delta) {
        fast_multiplier = std::clamp(fast_multiplier * std::exp(delta * .15f), 1.f, 64.f);
    }
    void wheel(float delta, bool flying) {
        if (flying)
            speed = std::clamp(speed * std::exp(delta * .15f), 1.f, 100000.f);
        else
            distance = std::clamp(distance * std::exp(-delta * .15f), 1.f, 300000.f);
    }
};
}
