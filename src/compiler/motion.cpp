#include "compiler/motion.h"
#include <algorithm>
#include <cmath>
namespace studio {
Bytes JointMotion::write(bool seamless) const {
    require(!joint.empty() && joint.size() <= 63 && frames > 0,
            "Invalid joint motion identity or duration");
    Bytes curve;
    std::uint32_t flags = 0x80000000;
    for (unsigned channel = 0; channel < 9; ++channel) {
        const auto &keys = channels[channel];
        require(!keys.empty() && keys.size() <= std::size_t(frames) + 1,
                "Invalid motion key count");
        for (std::size_t i = 0; i < keys.size(); ++i)
            require(keys[i].frame <= frames && (i == 0 || keys[i].frame > keys[i - 1].frame) &&
                        std::isfinite(keys[i].value) && std::isfinite(keys[i].slope),
                    "Invalid motion key");
        if (keys.size() == 1) {
            require(keys[0].frame == 0, "Constant curve must start at zero");
            flags |= 3u << (channel * 3);
            auto at = curve.size();
            curve.resize(at + 4);
            put_float(curve, at, keys[0].value);
        } else {
            require(keys.front().frame == 0 && keys.back().frame == frames,
                    "Loop curve must span its full duration");
            require(!seamless || (keys.front().value == keys.back().value &&
                                  keys.front().slope == keys.back().slope),
                    "Loop endpoints must match");
            flags |= 5u << (channel * 3);
            append32(curve, narrow(keys.size()));
            for (const auto &key : keys) {
                curve.push_back(static_cast<std::uint8_t>(key.frame & 255));
                if (frames > 255)
                    curve.push_back(static_cast<std::uint8_t>(key.frame >> 8));
            }
            curve.resize(aligned(curve.size(), 4), 255);
            for (const auto &key : keys) {
                auto at = curve.size();
                curve.resize(at + 8);
                put_float(curve, at, key.value);
                put_float(curve, at + 4, key.slope);
            }
        }
    }
    Bytes skeleton(8);
    put32(skeleton, 0, 1);
    skeleton.push_back(static_cast<std::uint8_t>(joint.size()));
    append(skeleton, View(reinterpret_cast<const std::uint8_t *>(joint.data()), joint.size()));
    skeleton.resize(aligned(skeleton.size(), 4));
    put32(skeleton, 4, narrow(skeleton.size() - 8));
    append32(skeleton, flags);
    append32(skeleton, narrow(curve.size()));
    append(skeleton, curve);
    Bytes out(32);
    put32(out, 0, 0x60000);
    put32(out, 4, 2);
    put32(out, 8, 0);
    put32(out, 12, 36);
    put32(out, 16, 32);
    put32(out, 20, 1);
    put32(out, 24, narrow(skeleton.size()));
    put32(out, 28, 68);
    Bytes top(36);
    put32(top, 0, frames);
    put16(top, 4, 1);
    put16(top, 6, 1);
    for (unsigned i = 0; i < 3; ++i) {
        put_float(top, 8 + i * 4, -100);
        put_float(top, 20 + i * 4, 100);
    }
    append(out, top);
    append(out, skeleton);
    out.resize(aligned(out.size(), 128));
    return out;
}
JointMotion JointMotion::read(View bytes) {
    require(u32(bytes, 0) == 0x60000 && u32(bytes, 4) == 2, "Expected generated joint motion");
    require(u32(bytes, 8) == 0 && u32(bytes, 20) == 1, "Unexpected motion sections");
    auto top = slice(bytes, u32(bytes, 16), u32(bytes, 12));
    auto skeleton = slice(bytes, u32(bytes, 28), u32(bytes, 24));
    require(u32(top, 0) > 0 && u32(top, 0) <= 65535 && u16(top, 4) == 1,
            "Invalid looping motion duration");
    JointMotion out;
    out.frames = static_cast<std::uint16_t>(u32(top, 0));
    require(u32(skeleton, 0) == 1, "Motion profile expects one joint track");
    auto length = slice(skeleton, 8, 1)[0];
    out.joint = text(slice(skeleton, 9, length));
    require(out.joint.size() == length && !out.joint.empty(), "Invalid motion joint name");
    auto pos = 8 + std::size_t(u32(skeleton, 4));
    require(pos >= 9 + std::size_t(length), "Motion names overlap curves");
    auto flags = u32(skeleton, pos);
    require((flags & 0x80000000) != 0, "Expected Euler rotation channels");
    auto end = pos + 8 + u32(skeleton, pos + 4);
    require(end == skeleton.size(), "Motion curve extent mismatch");
    pos += 8;
    for (unsigned channel = 0; channel < 9; ++channel) {
        auto mode = (flags >> (channel * 3)) & 7;
        if (mode == 3) {
            out.channels[channel].push_back({0, f32(skeleton, pos), 0});
            pos += 4;
        } else {
            require(mode == 5, "Unsupported authored motion channel");
            auto count = u32(skeleton, pos);
            pos += 4;
            require(count >= 2 && count <= std::size_t(out.frames) + 1,
                    "Invalid keyed curve count");
            std::vector<std::uint16_t> frames;
            for (std::size_t i = 0; i < count; ++i) {
                frames.push_back(out.frames > 255 ? u16(skeleton, pos)
                                                  : slice(skeleton, pos, 1)[0]);
                pos += out.frames > 255 ? 2 : 1;
            }
            pos = aligned(pos, 4);
            for (auto frame : frames) {
                out.channels[channel].push_back(
                    {frame, f32(skeleton, pos), f32(skeleton, pos + 4)});
                pos += 8;
            }
        }
    }
    require(pos == end, "Unaccounted motion curve bytes");
    out.write(false);
    return out;
}
float JointMotion::sample(unsigned channel, float frame) const {
    require(channel < 9 && std::isfinite(frame), "Invalid motion sample");
    const auto &keys = channels[channel];
    require(!keys.empty(), "Empty motion channel");
    frame = std::clamp(frame, 0.f, float(frames));
    if (keys.size() == 1 || frame <= keys.front().frame)
        return keys.front().value;
    if (frame >= keys.back().frame)
        return keys.back().value;
    auto right = std::upper_bound(keys.begin(), keys.end(), frame, [](float f, const auto &k) {
        return f < k.frame;
    });
    auto left = right - 1;
    float span = float(right->frame - left->frame), t = (frame - left->frame) / span, t2 = t * t,
          t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * left->value + (t3 - 2 * t2 + t) * span * left->slope +
           (-2 * t3 + 3 * t2) * right->value + (t3 - t2) * span * right->slope;
}
}
