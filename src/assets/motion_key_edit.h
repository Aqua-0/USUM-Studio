#pragma once
#include "scene/skeleton.h"
namespace studio {
enum class MotionTangent { Flat, Smooth, Linear };
std::vector<AnimationKey> copy_motion_keys(const AnimationCurve &, float first, float last);
void paste_motion_keys(AnimationCurve &, const std::vector<AnimationKey> &, float at,
                       float duration);
void retime_motion_keys(AnimationCurve &, float first, float last, float offset, float scale,
                        float duration);
void set_motion_tangents(AnimationCurve &, float first, float last, MotionTangent);
std::string serialize_motion_pose(const std::string &name, const SkeletalMotion &);
std::pair<std::string, SkeletalMotion> parse_motion_pose(const std::string &);
SkeletalMotion capture_motion_pose(const SkeletalMotion &, const std::vector<Joint> &, float frame);
void paste_motion_pose(SkeletalMotion &, const SkeletalMotion &, const std::vector<Joint> &,
                       float frame);
}
