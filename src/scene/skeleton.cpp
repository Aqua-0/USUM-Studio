#include "scene/skeleton.h"
#include <cmath>
#include <algorithm>
#include <functional>
#include <set>
namespace studio {
namespace {
using Quaternion = std::array<float, 4>;
Quaternion rotation_quaternion(const std::array<float, 3> &rotation, bool axis) {
    if (axis) {
        float a = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                            rotation[2] * rotation[2]);
        float s = a > 1e-8f ? std::sin(a) / a : 1.f;
        return {rotation[0] * s, rotation[1] * s, rotation[2] * s, std::cos(a)};
    }
    float x = rotation[0] * .5f, y = rotation[1] * .5f, z = rotation[2] * .5f;
    float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z),
          sz = std::sin(z);
    return {sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz};
}
Quaternion interpolate_rotation(Quaternion a, Quaternion b, float t) {
    float dot = 0;
    for (unsigned c = 0; c < 4; ++c)
        dot += a[c] * b[c];
    if (dot < 0) {
        for (auto &v : b)
            v = -v;
        dot = -dot;
    }
    float left = 1 - t, right = t;
    if (dot < .9995f) {
        float angle = std::acos(std::clamp(dot, 0.f, 1.f)), s = std::sin(angle);
        left = std::sin((1 - t) * angle) / s;
        right = std::sin(t * angle) / s;
    }
    float length = 0;
    for (unsigned c = 0; c < 4; ++c) {
        a[c] = a[c] * left + b[c] * right;
        length += a[c] * a[c];
    }
    for (auto &v : a)
        v /= std::sqrt(length);
    return a;
}
}
Matrix pose_identity() {
    Matrix out{};
    for (unsigned i = 0; i < 4; ++i)
        out[i * 5] = 1;
    return out;
}
Matrix pose_multiply(const Matrix &a, const Matrix &b) {
    Matrix out{};
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned k = 0; k < 4; ++k)
                out[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
    return out;
}
Matrix pose_inverse(const Matrix &m) {
    auto out = pose_identity();
    float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                m[2] * (m[4] * m[9] - m[5] * m[8]);
    require(std::isfinite(det) && std::abs(det) > 1e-12f, "Singular skeletal transform");
    out[0] = (m[5] * m[10] - m[6] * m[9]) / det;
    out[1] = (m[2] * m[9] - m[1] * m[10]) / det;
    out[2] = (m[1] * m[6] - m[2] * m[5]) / det;
    out[4] = (m[6] * m[8] - m[4] * m[10]) / det;
    out[5] = (m[0] * m[10] - m[2] * m[8]) / det;
    out[6] = (m[2] * m[4] - m[0] * m[6]) / det;
    out[8] = (m[4] * m[9] - m[5] * m[8]) / det;
    out[9] = (m[1] * m[8] - m[0] * m[9]) / det;
    out[10] = (m[0] * m[5] - m[1] * m[4]) / det;
    for (unsigned r = 0; r < 3; ++r)
        out[r * 4 + 3] = -out[r * 4] * m[3] - out[r * 4 + 1] * m[7] - out[r * 4 + 2] * m[11];
    return out;
}
SkeletalMotion decode_skeletal_motion(View b) {
    require(u32(b, 0) == 0x60000, "Unsupported skeletal motion version");
    auto count = u32(b, 4);
    require(count <= 64, "Excessive motion sections");
    View header, data;
    std::set<unsigned> seen;
    for (unsigned i = 0; i < count; ++i) {
        auto p = 8 + i * 12;
        auto type = u32(b, p);
        require(seen.insert(type).second, "Duplicate motion section");
        auto part = slice(b, u32(b, p + 8), u32(b, p + 4));
        if (type == 0)
            header = part;
        if (type == 1)
            data = part;
    }
    SkeletalMotion out;
    out.frames = float(u32(header, 0));
    out.looping = (u16(header, 4) & 1) != 0;
    require(out.frames > 0 && out.frames <= 65535, "Invalid skeletal duration");
    if (data.empty())
        return out;
    auto joints = u32(data, 0), length = u32(data, 4);
    require(joints <= 4096, "Excessive motion joints");
    auto names = slice(data, 8, length);
    std::size_t name = 0, p = 8 + length;
    std::set<std::string> used;
    for (unsigned i = 0; i < joints; ++i) {
        JointTrack track;
        auto n = slice(names, name++, 1)[0];
        track.name = text(slice(names, name, n));
        name += n;
        require(!track.name.empty() && used.insert(track.name).second, "Invalid motion joint name");
        auto flags = u32(data, p);
        auto end = p + 8 + u32(data, p + 4);
        auto payload = slice(data, 0, end);
        p += 8;
        track.axis_angle = (flags & 0x80000000) == 0;
        for (unsigned c = 0; c < 9; ++c)
            track.curves[c] =
                decode_animation_curve(payload, p, (flags >> (c * 3)) & 7, unsigned(out.frames));
        require(p == end, "Skeletal curve payload mismatch");
        out.tracks.push_back(std::move(track));
    }
    return out;
}
std::vector<Matrix> evaluate_skeleton(const SceneSkeleton &rig, double seconds, float hour,
                                      bool enabled) {
    auto count = rig.joints.size();
    std::vector<Matrix> world(count), result(count);
    std::vector<unsigned> state(count);
    std::vector<std::array<float, 3>> scales(count);
    MaterialMotion clock;
    clock.frames = rig.motion.frames;
    clock.looping = rig.motion.looping;
    float frame =
        enabled ? animation_frame(clock, seconds + rig.seconds_offset, rig.daily, hour) : 0;
    std::function<void(std::size_t)> visit = [&](std::size_t i) {
        require(state[i] != 1, "Cyclic skeletal pose");
        if (state[i] == 2)
            return;
        state[i] = 1;
        auto &j = rig.joints[i];
        auto scale = j.scale, translation = j.translation;
        auto quaternion = rotation_quaternion(j.rotation, false);
        auto apply_track = [&](const SkeletalMotion &motion, const std::vector<int> &tracks,
                               float at) {
            if (!enabled || i >= tracks.size() || tracks[i] < 0)
                return;
            auto &track = motion.tracks.at(std::size_t(tracks[i]));
            bool rotates = false;
            for (unsigned c = 3; c < 6; ++c)
                rotates |= !track.curves[c].keys.empty();
            for (unsigned c = 0; c < 3; ++c) {
                scale[c] = track.curves[c].sample(at, scale[c]);
                translation[c] = track.curves[c + 6].sample(at, translation[c]);
            }
            if (rotates) {
                // Angle wraps are valid at whole frames, but not between them.
                float first = std::floor(at), last = std::min(first + 1, motion.frames);
                auto a = track.axis_angle ? std::array<float, 3>{} : j.rotation, b = a;
                for (unsigned c = 0; c < 3; ++c) {
                    a[c] = track.curves[c + 3].sample(first, a[c]);
                    b[c] = track.curves[c + 3].sample(last, b[c]);
                }
                quaternion =
                    interpolate_rotation(rotation_quaternion(a, track.axis_angle),
                                         rotation_quaternion(b, track.axis_angle), at - first);
            }
        };
        apply_track(rig.motion, rig.tracks, frame);
        for (auto &layer : rig.overlays) {
            MaterialMotion clock;
            clock.frames = layer.motion.frames;
            clock.looping = layer.motion.looping;
            apply_track(layer.motion, layer.tracks, animation_frame(clock, seconds, false, hour));
        }
        auto local = pose_identity();
        auto [x, y, z, w] = quaternion;
        local[0] = 1 - 2 * (y * y + z * z);
        local[1] = 2 * (x * y - z * w);
        local[2] = 2 * (x * z + y * w);
        local[4] = 2 * (x * y + z * w);
        local[5] = 1 - 2 * (x * x + z * z);
        local[6] = 2 * (y * z - x * w);
        local[8] = 2 * (x * z - y * w);
        local[9] = 2 * (y * z + x * w);
        local[10] = 1 - 2 * (x * x + y * y);
        for (unsigned r = 0; r < 3; ++r) {
            for (unsigned c = 0; c < 3; ++c)
                local[r * 4 + c] *= scale[c];
            local[r * 4 + 3] = translation[r];
        }
        scales[i] = scale;
        if (j.parent >= 0) {
            auto parent = std::size_t(j.parent);
            require(parent < count, "Missing pose parent");
            visit(parent);
            if ((j.flags & 2) == 0)
                for (unsigned r = 0; r < 3; ++r) {
                    if (std::abs(scales[parent][r]) > 1e-8f)
                        for (unsigned c = 0; c < 3; ++c)
                            local[r * 4 + c] /= scales[parent][r];
                }
            world[i] = pose_multiply(world[parent], local);
        } else
            world[i] = local;
        result[i] =
            pose_multiply(pose_multiply(pose_multiply(rig.placement, world[i]), j.inverse_bind),
                          rig.inverse_placement);
        state[i] = 2;
    };
    for (std::size_t i = 0; i < count; ++i)
        visit(i);
    return result;
}
}
