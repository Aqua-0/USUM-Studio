#include "assets/motion_key_edit.h"
#include "assets/motion_exchange.h"
#include <algorithm>
#include <cmath>
namespace studio {
namespace {
void range(float first, float last) {
    require(std::isfinite(first) && std::isfinite(last) && first >= 0 && last >= first,
            "Choose a valid key range");
}
void validate(std::vector<AnimationKey> &keys, float duration) {
    std::sort(keys.begin(), keys.end(), [](auto a, auto b) {
        return a.frame < b.frame;
    });
    for (unsigned i = 0; i < keys.size(); ++i) {
        auto k = keys[i];
        require(std::isfinite(k.frame) && k.frame >= 0 && k.frame <= duration &&
                    std::floor(k.frame) == k.frame,
                "Keys must land on whole frames inside the motion");
        require(std::isfinite(k.value) && std::isfinite(k.slope), "Key values must be finite");
        require(!i || keys[i - 1].frame != k.frame, "Retiming would overlap another key");
    }
}
}
std::vector<AnimationKey> copy_motion_keys(const AnimationCurve &curve, float first, float last) {
    range(first, last);
    std::vector<AnimationKey> result;
    for (auto k : curve.keys)
        if (k.frame >= first && k.frame <= last) {
            k.frame -= first;
            result.push_back(k);
        }
    return result;
}
void paste_motion_keys(AnimationCurve &curve, const std::vector<AnimationKey> &keys, float at,
                       float duration) {
    require(!keys.empty(), "Copy keys before pasting");
    auto next = curve.keys;
    for (auto k : keys) {
        k.frame += at;
        std::erase_if(next, [&](auto old) {
            return old.frame == k.frame;
        });
        next.push_back(k);
    }
    validate(next, duration);
    curve.keys = std::move(next);
}
void retime_motion_keys(AnimationCurve &curve, float first, float last, float offset, float scale,
                        float duration) {
    range(first, last);
    require(std::isfinite(scale) && scale > 0 && std::isfinite(offset),
            "Time scale must be positive and finite");
    auto next = curve.keys;
    for (auto &k : next)
        if (k.frame >= first && k.frame <= last) {
            k.frame = first + (k.frame - first) * scale + offset;
            k.slope /= scale;
        }
    validate(next, duration);
    curve.keys = std::move(next);
}
void set_motion_tangents(AnimationCurve &curve, float first, float last, MotionTangent mode) {
    range(first, last);
    for (unsigned i = 0; i < curve.keys.size(); ++i) {
        auto &k = curve.keys[i];
        if (k.frame < first || k.frame > last)
            continue;
        unsigned a = i ? i - 1 : i, b = std::min(i + 1, unsigned(curve.keys.size()) - 1);
        if (mode == MotionTangent::Linear && i + 1 < curve.keys.size())
            a = i;
        k.slope = mode == MotionTangent::Flat || a == b
                      ? 0
                      : (curve.keys[b].value - curve.keys[a].value) /
                            (curve.keys[b].frame - curve.keys[a].frame);
    }
}
std::string serialize_motion_pose(const std::string &name, const SkeletalMotion &pose) {
    MotionExchange exchange;
    exchange.source = "USUMSTUDIO_POSE";
    exchange.name = name;
    exchange.model = "Pose";
    exchange.skeletal = pose;
    exchange.skeletal.frames = 1;
    return serialize_motion_exchange(exchange);
}
std::pair<std::string, SkeletalMotion> parse_motion_pose(const std::string &text) {
    auto exchange = parse_motion_exchange(text);
    require(exchange.source == "USUMSTUDIO_POSE" && exchange.skeletal.frames == 1 &&
                !exchange.skeletal.tracks.empty(),
            "Choose a Studio pose file");
    for (auto &track : exchange.skeletal.tracks)
        for (auto &curve : track.curves)
            require(curve.keys.size() == 1 && curve.keys[0].frame == 0,
                    "Pose must contain one value per channel");
    exchange.skeletal.frames = 0;
    return {exchange.name, std::move(exchange.skeletal)};
}
SkeletalMotion capture_motion_pose(const SkeletalMotion &motion, const std::vector<Joint> &joints,
                                   float frame) {
    require(std::isfinite(frame) && frame >= 0 && frame <= motion.frames,
            "Pose frame is outside the motion");
    SkeletalMotion result;
    for (auto &joint : joints) {
        auto found = std::find_if(motion.tracks.begin(), motion.tracks.end(), [&](auto &t) {
            return t.name == joint.name;
        });
        JointTrack track;
        if (found != motion.tracks.end())
            track = *found;
        track.name = joint.name;
        for (unsigned c = 0; c < 9; ++c) {
            float fallback = c < 3   ? joint.scale[c]
                             : c < 6 ? (track.axis_angle ? 0 : joint.rotation[c - 3])
                                     : joint.translation[c - 6];
            track.curves[c].keys = {{0, track.curves[c].sample(frame, fallback), 0}};
        }
        result.tracks.push_back(std::move(track));
    }
    return result;
}
void paste_motion_pose(SkeletalMotion &motion, const SkeletalMotion &pose,
                       const std::vector<Joint> &joints, float frame) {
    require(!pose.tracks.empty(), "Capture a pose first");
    auto next = motion;
    for (auto &saved : pose.tracks) {
        auto joint = std::find_if(joints.begin(), joints.end(), [&](auto &j) {
            return j.name == saved.name;
        });
        require(joint != joints.end(), "Pose bone is missing in this model");
        auto found = std::find_if(next.tracks.begin(), next.tracks.end(), [&](auto &t) {
            return t.name == saved.name;
        });
        if (found == next.tracks.end()) {
            next.tracks.push_back({});
            found = next.tracks.end() - 1;
            found->name = saved.name;
            found->axis_angle = saved.axis_angle;
        }
        require(found->axis_angle == saved.axis_angle,
                "Pose rotation representation does not match this motion");
        for (unsigned c = 0; c < 9; ++c) {
            if (found->curves[c].keys.empty() && frame > 0) {
                float bind = c < 3   ? joint->scale[c]
                             : c < 6 ? (found->axis_angle ? 0 : joint->rotation[c - 3])
                                     : joint->translation[c - 6];
                found->curves[c].keys.push_back({0, bind, 0});
            }
            paste_motion_keys(found->curves[c], saved.curves[c].keys, frame, motion.frames);
        }
    }
    motion = std::move(next);
}
}
