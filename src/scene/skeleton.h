#pragma once
#include "formats/skinning.h"
#include "scene/animation.h"
namespace studio {
struct JointTrack {
    std::string name;
    bool axis_angle = false;
    std::array<AnimationCurve, 9> curves;
    bool operator==(const JointTrack &) const = default;
};
struct SkeletalMotion {
    float frames = 0;
    bool looping = false;
    std::vector<JointTrack> tracks;
    bool operator==(const SkeletalMotion &) const = default;
};
struct SkeletalLayer {
    SkeletalMotion motion;
    std::vector<int> tracks;
};
struct SceneSkeleton {
    std::vector<Joint> joints;
    SkeletalMotion motion;
    std::vector<int> tracks;
    std::vector<SkeletalLayer> overlays;
    Matrix placement{}, inverse_placement{};
    double seconds_offset = 0;
    bool daily = false;
    int parent_skeleton = -1, parent_joint = -1;
    Matrix attachment_transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    int player = -1;
    std::array<SkeletalMotion, 3> locomotion;
};
SkeletalMotion decode_skeletal_motion(View bytes);
Matrix pose_identity();
Matrix pose_multiply(const Matrix &a, const Matrix &b);
Matrix pose_inverse(const Matrix &m);
std::vector<Matrix> evaluate_skeleton(const SceneSkeleton &skeleton, double seconds, float hour,
                                      bool enabled);
}
