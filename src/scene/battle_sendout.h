#pragma once
#include "scene/animation.h"
#include "assets/battle_profile.h"
namespace studio {
struct BattleCameraPose {
    std::array<float, 3> eye{}, target{}, up{0, 1, 0};
    float fov = 30, near_clip = 1, far_clip = 12000, aspect = 5.f / 3.f;
};
struct BattleCameraMotion {
    float frames = 0;
    std::array<float, 10> defaults{};
    std::array<AnimationCurve, 10> curves;
    BattleCameraPose sample(float frame) const;
};
BattleCameraMotion decode_battle_camera(View environment, View motion);
struct SendOutCamera {
    float start = 0, speed = 1;
    unsigned node = 0;
    bool scale = false;
    BattleCameraMotion motion;
};
struct BattleSendOut {
    float frames = 0, camera_end = 0;
    unsigned member = 0;
    std::vector<SendOutCamera> cameras;
};
BattleSendOut load_battle_sendout(const std::filesystem::path &dump, bool alternate);
struct SendOutPhase {
    unsigned slot = 0;
    float start = 0, height = 0, growth = 1;
    bool loop = true;
};
SendOutPhase battle_sendout_phase(float frame, float jump_frames, float landing_frames,
                                  float alternate_frames, float center_height);
}
