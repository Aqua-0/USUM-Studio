#pragma once
#include "scene/skeleton.h"
namespace studio {
struct MotionExchange {
    std::string source, model, name;
    SkeletalMotion skeletal;
    MaterialMotion material;
    VisibilityMotion visibility;
};
MotionExchange decode_motion_exchange(View bytes, const std::string &model,
                                      const std::string &name);
Bytes replace_motion_exchange(View original, const MotionExchange &motion);
std::string serialize_motion_exchange(const MotionExchange &motion);
MotionExchange parse_motion_exchange(const std::string &text);
}
