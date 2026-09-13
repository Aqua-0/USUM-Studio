#include "assets/skeletal_motion.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace studio {
Bytes replace_skeletal_motion(View original, const SkeletalMotion &motion) {
    auto before = decode_skeletal_motion(original);
    if (before == motion)
        return Bytes(original.begin(), original.end());
    require(motion.frames == before.frames && motion.looping == before.looping,
            "Keep the source duration and loop flag; other animation channels share them");
    require(motion.tracks.size() <= 4096, "Too many bone tracks");
    std::set<std::string> used;
    for (auto &t : motion.tracks) {
        require(!t.name.empty() && t.name.size() < 256 && used.insert(t.name).second,
                "Invalid or duplicate bone track");
        for (auto &curve : t.curves) {
            float previous = -1;
            for (auto &k : curve.keys) {
                require(std::isfinite(k.frame) && std::floor(k.frame) == k.frame && k.frame >= 0 &&
                            k.frame <= motion.frames && k.frame > previous &&
                            std::isfinite(k.value) && std::isfinite(k.slope),
                        "Keys need ordered whole frames and finite values/slopes");
                previous = k.frame;
            }
            if (curve.keys.size() == 1)
                require(curve.keys[0].frame == 0,
                        "A constant channel's single key must be at frame zero");
        }
    }
    std::map<unsigned, Bytes> sections;
    for (unsigned i = 0; i < u32(original, 4); ++i) {
        auto p = 8 + i * 12;
        auto data = slice(original, u32(original, p + 8), u32(original, p + 4));
        sections[u32(original, p)] = {data.begin(), data.end()};
    }
    std::map<std::string, Bytes> records;
    if (sections.contains(1)) {
        auto &data = sections.at(1);
        std::size_t p = 8 + u32(data, 4);
        for (auto &t : before.tracks) {
            auto record = slice(data, p, 8 + u32(data, p + 4));
            records[t.name] = {record.begin(), record.end()};
            p += record.size();
        }
    }
    Bytes names;
    for (auto &t : motion.tracks) {
        names.push_back(std::uint8_t(t.name.size()));
        names.insert(names.end(), t.name.begin(), t.name.end());
    }
    names.resize(aligned(names.size(), 4));
    Bytes data;
    append32(data, narrow(motion.tracks.size()));
    append32(data, narrow(names.size()));
    append(data, names);
    for (auto &t : motion.tracks) {
        auto old = std::find_if(before.tracks.begin(), before.tracks.end(), [&](auto &b) {
            return b.name == t.name;
        });
        if (old != before.tracks.end() && *old == t) {
            append(data, records.at(t.name));
            continue;
        }
        std::uint32_t flags =
            records.contains(t.name) ? u32(records.at(t.name), 0) & 0x78000000u : 0;
        flags |= t.axis_angle ? 0 : 0x80000000u;
        auto header = data.size();
        append32(data, 0);
        append32(data, 0);
        for (unsigned c = 0; c < 9; ++c) {
            auto &keys = t.curves[c].keys;
            if (keys.empty())
                continue;
            if (keys.size() == 1) {
                flags |= 3u << (c * 3);
                auto p = data.size();
                data.resize(p + 4);
                put_float(data, p, keys[0].value);
            } else {
                flags |= 5u << (c * 3);
                append32(data, narrow(keys.size()));
                for (auto &k : keys) {
                    data.push_back(std::uint8_t(unsigned(k.frame) & 255));
                    if (motion.frames > 255)
                        data.push_back(std::uint8_t(unsigned(k.frame) >> 8));
                }
                data.resize(aligned(data.size(), 4), 255);
                for (auto &k : keys) {
                    auto p = data.size();
                    data.resize(p + 8);
                    put_float(data, p, k.value);
                    put_float(data, p + 4, k.slope);
                }
            }
        }
        put32(data, header, flags);
        put32(data, header + 4, narrow(data.size() - header - 8));
    }
    if (motion.tracks.empty())
        sections.erase(1);
    else
        sections[1] = std::move(data);
    Bytes out(8 + sections.size() * 12);
    put32(out, 0, 0x60000);
    put32(out, 4, narrow(sections.size()));
    unsigned i = 0;
    for (auto &[type, part] : sections) {
        auto p = 8 + i++ * 12;
        put32(out, p, type);
        put32(out, p + 4, narrow(part.size()));
        put32(out, p + 8, narrow(out.size()));
        append(out, part);
    }
    require(decode_skeletal_motion(out) == motion, "Skeletal motion did not round-trip");
    return out;
}
}
