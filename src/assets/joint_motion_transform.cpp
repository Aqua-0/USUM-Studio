#include "assets/skeletal_motion.h"
#include <algorithm>
#include <cmath>
namespace studio {
namespace {
using Vector = std::array<float, 3>;
using Quaternion = std::array<float, 4>;
constexpr float pi = 3.14159265358979323846f;
Quaternion quaternion(Vector r, bool axis) {
    if (axis) {
        float a = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]),
              s = a > 1e-8f ? std::sin(a) / a : 1;
        return {r[0] * s, r[1] * s, r[2] * s, std::cos(a)};
    }
    float x = r[0] / 2, y = r[1] / 2, z = r[2] / 2;
    return {std::sin(x) * std::cos(y) * std::cos(z) - std::cos(x) * std::sin(y) * std::sin(z),
            std::cos(x) * std::sin(y) * std::cos(z) + std::sin(x) * std::cos(y) * std::sin(z),
            std::cos(x) * std::cos(y) * std::sin(z) - std::sin(x) * std::sin(y) * std::cos(z),
            std::cos(x) * std::cos(y) * std::cos(z) + std::sin(x) * std::sin(y) * std::sin(z)};
}
Quaternion multiply(Quaternion a, Quaternion b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
            a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
            a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}
Vector rotation(Quaternion q, Vector before, bool axis) {
    if (axis) {
        float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
        Vector direction{};
        float angle = 0;
        if (n > 1e-7f) {
            for (unsigned i = 0; i < 3; ++i)
                direction[i] = q[i] / n;
            angle = std::atan2(n, q[3]);
        } else {
            float old =
                std::sqrt(before[0] * before[0] + before[1] * before[1] + before[2] * before[2]);
            if (old < 1e-7f)
                return {};
            for (unsigned i = 0; i < 3; ++i)
                direction[i] = before[i] / old;
        }
        float along = 0;
        for (unsigned i = 0; i < 3; ++i)
            along += before[i] * direction[i];
        angle += std::round((along - angle) / pi) * pi;
        for (auto &v : direction)
            v *= angle;
        return direction;
    }
    auto [x, y, z, w] = q;
    float sine = std::clamp(2 * (w * y - z * x), -1.f, 1.f),
          cosine = std::hypot(1 - 2 * (y * y + z * z), 2 * (w * z + x * y));
    Vector a;
    if (cosine < 1e-6f)
        a = {before[0], std::copysign(pi / 2, sine),
             2 * std::atan2(z, w) + std::copysign(1.f, sine) * before[0]};
    else
        a = {std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)), std::atan2(sine, cosine),
             std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))};
    Vector b{a[0] + pi, pi - a[1], a[2] + pi};
    auto nearest = [&](Vector &v) {
        float distance = 0;
        for (unsigned i = 0; i < 3; ++i) {
            v[i] += std::round((before[i] - v[i]) / (2 * pi)) * 2 * pi;
            distance += (v[i] - before[i]) * (v[i] - before[i]);
        }
        return distance;
    };
    float da = nearest(a), db = nearest(b);
    return da <= db ? a : b;
}
void key(AnimationCurve &curve, unsigned frame, float value, float fallback) {
    std::erase_if(curve.keys, [&](auto &k) {
        return k.frame == frame;
    });
    if (curve.keys.empty() && frame)
        curve.keys.push_back({0, fallback, 0});
    curve.keys.push_back({float(frame), value, 0});
    std::sort(curve.keys.begin(), curve.keys.end(), [](auto &a, auto &b) {
        return a.frame < b.frame;
    });
}
}
JointTrack offset_joint_motion(const JointTrack &track, const Joint &joint, unsigned frame,
                               unsigned axis, float amount, bool rotate) {
    require(axis < 3 && std::isfinite(amount), "Invalid bone transform");
    if (amount == 0)
        return track;
    auto result = track;
    if (!rotate) {
        auto fallback = joint.translation[axis];
        key(result.curves[axis + 6], frame,
            track.curves[axis + 6].sample(float(frame), fallback) + amount, fallback);
        return result;
    }
    Vector base = track.axis_angle ? Vector{} : joint.rotation, current = base;
    for (unsigned i = 0; i < 3; ++i)
        current[i] = track.curves[i + 3].sample(float(frame), base[i]);
    Quaternion delta{};
    delta[axis] = std::sin(amount / 2);
    delta[3] = std::cos(amount / 2);
    auto q = multiply(delta, quaternion(current, track.axis_angle));
    float length = 0;
    for (auto v : q)
        length += v * v;
    for (auto &v : q)
        v /= std::sqrt(length);
    auto values = rotation(q, current, track.axis_angle);
    for (unsigned i = 0; i < 3; ++i)
        key(result.curves[i + 3], frame, values[i], base[i]);
    return result;
}
}
