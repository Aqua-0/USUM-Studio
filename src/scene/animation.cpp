#include "scene/animation.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
namespace studio {
namespace {
float number(View b, std::size_t p) {
    auto v = f32(b, p);
    require(std::isfinite(v), "Non-finite animation value");
    return v;
}
std::vector<std::string> names(View b, std::size_t &p, unsigned count, unsigned length) {
    auto table = slice(b, p, length);
    std::size_t at = 0;
    std::vector<std::string> out;
    for (unsigned i = 0; i < count; ++i) {
        auto n = slice(table, at++, 1)[0];
        out.push_back(text(slice(table, at, n)));
        at += n;
    }
    p += length;
    return out;
}
}
float AnimationCurve::sample(float frame, float fallback) const {
    if (keys.empty())
        return fallback;
    if (frame <= keys.front().frame)
        return keys.front().value;
    if (frame >= keys.back().frame)
        return keys.back().value;
    auto next = std::upper_bound(keys.begin(), keys.end(), frame, [](float f, const auto &k) {
        return f < k.frame;
    });
    auto &a = *(next - 1);
    auto &b = *next;
    float span = b.frame - a.frame, t = (frame - a.frame) / span, t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a.value + (t3 - 2 * t2 + t) * span * a.slope +
           (-2 * t3 + 3 * t2) * b.value + (t3 - t2) * span * b.slope;
}
AnimationCurve decode_animation_curve(View b, std::size_t &p, unsigned mode, unsigned frames) {
    AnimationCurve out;
    if (mode == 0)
        return out;
    if (mode <= 2) {
        out.keys.push_back({0, float(mode - 1), 0});
        return out;
    }
    if (mode == 3) {
        out.keys.push_back({0, number(b, p), 0});
        p += 4;
        return out;
    }
    require(mode == 4 || mode == 5, "Unsupported animation curve encoding");
    auto count = u32(b, p);
    p += 4;
    require(count > 0 && count <= 65536, "Invalid animation key count");
    std::vector<unsigned> positions;
    for (unsigned i = 0; i < count; ++i) {
        unsigned f = frames <= 255 ? slice(b, p++, 1)[0] : u16(b, p);
        if (frames > 255)
            p += 2;
        require(f <= frames && (positions.empty() || f > positions.back()),
                "Invalid animation key order");
        positions.push_back(f);
    }
    while (p % 4 && slice(b, p, 1)[0] == 255)
        ++p;
    float vs = 1, vo = 0, ss = 1, so = 0;
    if (mode == 4) {
        vs = number(b, p);
        vo = number(b, p + 4);
        ss = number(b, p + 8);
        so = number(b, p + 12);
        p += 16;
    }
    for (auto f : positions) {
        float v = mode == 4 ? float(u16(b, p)) / 65535 * vs + vo : number(b, p),
              s = mode == 4 ? float(u16(b, p + 2)) / 65535 * ss + so : number(b, p + 4);
        p += mode == 4 ? 4 : 8;
        require(std::isfinite(v) && std::isfinite(s), "Excessive animation curve value");
        out.keys.push_back({float(f), v, s});
    }
    return out;
}
MaterialMotion decode_material_motion(View b) {
    require(u32(b, 0) == 0x60000, "Unsupported material motion version");
    auto count = u32(b, 4);
    require(count > 0 && count <= 64, "Invalid motion section count");
    std::map<unsigned, View> sections;
    for (unsigned i = 0; i < count; ++i) {
        auto p = 8 + i * 12;
        require(sections.emplace(u32(b, p), slice(b, u32(b, p + 8), u32(b, p + 4))).second,
                "Duplicate motion section");
    }
    require(sections.contains(0), "Missing motion header");
    MaterialMotion out;
    out.frames = float(u32(sections.at(0), 0));
    out.looping = (u16(sections.at(0), 4) & 1) != 0;
    require(out.frames > 0 && out.frames <= 65535, "Invalid material motion duration");
    for (auto &[type, data] : sections) {
        if (type != 3 && type != 4 && type != 5)
            continue;
        unsigned material_count = u32(data, 0), name_length = u32(data, 4);
        require(material_count <= 16384, "Excessive animated materials");
        std::size_t p = 8;
        unsigned texture_count = 0, texture_length = 0;
        if (type == 4) {
            texture_count = u32(data, p);
            texture_length = u32(data, p + 4);
            p += 8;
            require(texture_count <= 16384, "Excessive animation textures");
        }
        std::vector<unsigned> slots;
        for (unsigned i = 0; i < material_count; ++i) {
            auto n = u32(data, p);
            p += 4;
            require(n <= (type == 5 ? 6u : 3u), "Too many animation slots");
            slots.push_back(n);
        }
        auto materials = names(data, p, material_count, name_length);
        auto textures =
            type == 4 ? names(data, p, texture_count, texture_length) : std::vector<std::string>{};
        std::set<std::pair<std::string, unsigned>> used;
        for (unsigned i = 0; i < material_count; ++i)
            for (unsigned j = 0; j < slots[i]; ++j) {
                MaterialTrack track;
                track.material = materials[i];
                track.slot = u32(data, p);
                p += 4;
                require(track.slot < (type == 5 ? 6u : 3u), "Invalid animation slot");
                require(used.emplace(track.material, track.slot).second,
                        "Duplicate material animation track");
                if (type == 4) {
                    track.kind = MaterialTrack::Kind::TexturePattern;
                    auto keys = u32(data, p);
                    p += 4;
                    require(keys <= 65536, "Excessive texture pattern keys");
                    for (unsigned k = 0; k < keys; ++k) {
                        auto frame = u16(data, p), texture = u16(data, p + 2);
                        p += 4;
                        require(frame <= out.frames && texture < textures.size() &&
                                    (track.textures.empty() || frame > track.textures.back().frame),
                                "Invalid texture pattern key");
                        track.textures.push_back({frame, textures[texture]});
                    }
                } else {
                    track.kind = type == 3 ? MaterialTrack::Kind::TextureTransform
                                           : MaterialTrack::Kind::ConstantColor;
                    auto flags = u32(data, p), length = u32(data, p + 4);
                    p += 8;
                    auto end = p + length;
                    slice(data, p, length);
                    for (unsigned k = 0; k < (type == 3 ? 5u : 4u); ++k)
                        track.curves[k] = decode_animation_curve(data, p, (flags >> (k * 3)) & 7,
                                                                 unsigned(out.frames));
                    require(p == end, "Material curve payload mismatch");
                }
                out.tracks.push_back(std::move(track));
            }
    }
    return out;
}
VisibilityMotion decode_visibility_motion(View b) {
    require(u32(b, 0) == 0x60000, "Unsupported visibility motion version");
    auto count = u32(b, 4);
    require(count <= 64, "Excessive motion sections");
    VisibilityMotion out;
    View data;
    for (unsigned i = 0; i < count; ++i) {
        auto p = 8 + i * 12;
        auto part = slice(b, u32(b, p + 8), u32(b, p + 4));
        if (u32(b, p) == 0) {
            out.clock.frames = float(u32(part, 0));
            out.clock.looping = (u16(part, 4) & 1) != 0;
        }
        if (u32(b, p) == 6)
            data = part;
    }
    require(out.clock.frames > 0 && out.clock.frames <= 65535, "Invalid visibility duration");
    if (data.empty())
        return out;
    auto n = u32(data, 0), length = u32(data, 4);
    require(n <= 16384, "Excessive visibility tracks");
    std::size_t p = 8;
    auto mesh_names = names(data, p, n, length);
    auto frames = unsigned(out.clock.frames) + 1;
    for (auto &name : mesh_names) {
        VisibilityTrack track;
        track.mesh = name;
        auto bits = slice(data, p, (frames + 7) / 8);
        p += bits.size();
        for (unsigned f = 0; f < frames; ++f)
            track.frames.push_back((bits[f / 8] & (1u << (f % 8))) != 0);
        out.tracks.push_back(std::move(track));
    }
    return out;
}
float animation_frame(const MaterialMotion &motion, double seconds, bool daily, float hour) {
    if (daily)
        return std::clamp(hour, 0.f, 24.f) * motion.frames / 24.f;
    double frame = std::max(0.0, seconds) * 30.0;
    if (motion.looping && motion.frames > 0)
        frame = std::fmod(frame, motion.frames);
    return float(std::clamp(frame, 0.0, double(motion.frames)));
}
}
