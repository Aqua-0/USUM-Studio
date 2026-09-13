#pragma once
#include "core/binary.h"
#include <array>
namespace studio {
struct MotionKey {
    std::uint16_t frame;
    float value, slope;
};
struct JointMotion {
    std::string joint;
    std::uint16_t frames = 60;
    std::array<std::vector<MotionKey>, 9> channels;
    Bytes write(bool seamless = true) const;
    static JointMotion read(View bytes);
    float sample(unsigned channel, float frame) const;
};
}
