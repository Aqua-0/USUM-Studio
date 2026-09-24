#pragma once
#include "scene/skeleton.h"
namespace studio {
JointTrack offset_joint_motion(const JointTrack &track, const Joint &joint, unsigned frame,
                               unsigned axis, float amount, bool rotation);
JointTrack rotate_joint_motion(const JointTrack &track, const Joint &joint, unsigned frame,
                               std::array<float, 3> axis, float angle);
JointTrack scale_joint_motion(const JointTrack &track, const Joint &joint, unsigned frame,
                              std::array<float, 3> factors);
Bytes replace_skeletal_motion(View original, const SkeletalMotion &motion);
}
