#include "assets/visibility_motion.h"
#include <map>
#include <set>
namespace studio {
Bytes replace_visibility_motion(View original, const VisibilityMotion &motion) {
    auto before = decode_visibility_motion(original);
    if (before == motion)
        return Bytes(original.begin(), original.end());
    require(motion.clock == before.clock,
            "Keep the source duration and loop flag; other animation channels share them");
    require(motion.tracks.size() <= 16384, "Too many visibility tracks");
    std::set<std::string> used;
    Bytes names;
    for (auto &t : motion.tracks) {
        require(!t.mesh.empty() && t.mesh.size() < 256 && t.mesh.find('\0') == std::string::npos &&
                    used.insert(t.mesh).second,
                "Invalid or duplicate mesh track");
        require(t.frames.size() == std::size_t(motion.clock.frames) + 1,
                "Visibility tracks must cover every source frame");
        names.push_back(std::uint8_t(t.mesh.size()));
        names.insert(names.end(), t.mesh.begin(), t.mesh.end());
    }
    names.resize(aligned(names.size(), 4));
    Bytes data;
    append32(data, narrow(motion.tracks.size()));
    append32(data, narrow(names.size()));
    append(data, names);
    for (auto &t : motion.tracks) {
        auto p = data.size();
        data.resize(p + (t.frames.size() + 7) / 8);
        for (std::size_t f = 0; f < t.frames.size(); ++f)
            if (t.frames[f])
                data[p + f / 8] |= std::uint8_t(1u << (f % 8));
    }
    std::map<unsigned, Bytes> sections;
    for (unsigned i = 0; i < u32(original, 4); ++i) {
        auto p = 8 + i * 12;
        auto part = slice(original, u32(original, p + 8), u32(original, p + 4));
        sections[u32(original, p)] = {part.begin(), part.end()};
    }
    if (motion.tracks.empty())
        sections.erase(6);
    else
        sections[6] = std::move(data);
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
    require(decode_visibility_motion(out) == motion, "Visibility motion did not round-trip");
    return out;
}
}
