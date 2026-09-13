#include "assets/motion_exchange.h"
#include "assets/skeletal_motion.h"
#include "assets/material_motion.h"
#include "assets/visibility_motion.h"
#include "core/digest.h"
#include <sstream>
#include <iomanip>
#include <cmath>
namespace studio {
MotionExchange decode_motion_exchange(View bytes, const std::string &model,
                                      const std::string &name) {
    return {sha256(bytes),
            model,
            name,
            decode_skeletal_motion(bytes),
            decode_material_motion(bytes),
            decode_visibility_motion(bytes)};
}
Bytes replace_motion_exchange(View original, const MotionExchange &motion) {
    require(sha256(original) == motion.source,
            "This motion changed after export; export it again before importing");
    auto bytes = replace_skeletal_motion(original, motion.skeletal);
    bytes = replace_material_motion(bytes, motion.material);
    return replace_visibility_motion(bytes, motion.visibility);
}
std::string serialize_motion_exchange(const MotionExchange &m) {
    std::ostringstream out;
    out << std::setprecision(9) << "USUMSTUDIO_MOTION 1\nsource " << std::quoted(m.source)
        << "\nmodel " << std::quoted(m.model) << "\nname " << std::quoted(m.name) << "\nclock "
        << m.skeletal.frames << ' ' << m.skeletal.looping << "\nbones " << m.skeletal.tracks.size()
        << '\n';
    auto curves = [&](auto &cs) {
        for (auto &c : cs) {
            out << "curve " << c.keys.size();
            for (auto k : c.keys)
                out << ' ' << k.frame << ' ' << k.value << ' ' << k.slope;
            out << '\n';
        }
    };
    for (auto &t : m.skeletal.tracks) {
        out << "bone " << std::quoted(t.name) << ' ' << t.axis_angle << '\n';
        curves(t.curves);
    }
    out << "materials " << m.material.tracks.size() << '\n';
    for (auto &t : m.material.tracks) {
        out << "material " << unsigned(t.kind) << ' ' << std::quoted(t.material) << ' ' << t.slot
            << '\n';
        curves(t.curves);
        out << "textures " << t.textures.size();
        for (auto &k : t.textures)
            out << ' ' << k.frame << ' ' << std::quoted(k.texture);
        out << '\n';
    }
    out << "visibility " << m.visibility.tracks.size() << '\n';
    for (auto &t : m.visibility.tracks) {
        out << "mesh " << std::quoted(t.mesh) << ' ' << t.frames.size();
        for (bool v : t.frames)
            out << ' ' << v;
        out << '\n';
    }
    out << "end\n";
    return out.str();
}
MotionExchange parse_motion_exchange(const std::string &text) {
    require(text.size() < 128 * 1024 * 1024, "Motion exchange is too large");
    std::istringstream in(text);
    std::string word;
    auto token = [&](const char *wanted) {
        require(bool(in >> word) && word == wanted, std::string("Expected ") + wanted);
    };
    auto count = [&](std::size_t max) {
        std::size_t n;
        require(bool(in >> n) && n <= max, "Invalid motion record count");
        return n;
    };
    auto name = [&] {
        std::string value;
        require(bool(in >> std::quoted(value)) && !value.empty() && value.size() < 1024,
                "Invalid motion name");
        return value;
    };
    token("USUMSTUDIO_MOTION");
    require(count(1) == 1, "Unsupported motion exchange version");
    MotionExchange m;
    token("source");
    m.source = name();
    token("model");
    m.model = name();
    token("name");
    m.name = name();
    token("clock");
    require(bool(in >> m.skeletal.frames >> m.skeletal.looping) && m.skeletal.frames >= 1 &&
                m.skeletal.frames <= 65535 && std::floor(m.skeletal.frames) == m.skeletal.frames,
            "Invalid motion clock");
    m.material.frames = m.visibility.clock.frames = m.skeletal.frames;
    m.material.looping = m.visibility.clock.looping = m.skeletal.looping;
    auto curves = [&](auto &cs) {
        for (auto &c : cs) {
            token("curve");
            auto n = count(65536);
            float previous = -1;
            for (std::size_t i = 0; i < n; ++i) {
                AnimationKey k;
                require(bool(in >> k.frame >> k.value >> k.slope) && std::isfinite(k.frame) &&
                            std::isfinite(k.value) && std::isfinite(k.slope) && k.frame >= 0 &&
                            k.frame <= m.skeletal.frames && k.frame > previous &&
                            std::floor(k.frame) == k.frame,
                        "Invalid motion key");
                previous = k.frame;
                c.keys.push_back(k);
            }
        }
    };
    token("bones");
    auto n = count(4096);
    for (std::size_t i = 0; i < n; ++i) {
        token("bone");
        JointTrack t;
        t.name = name();
        require(bool(in >> t.axis_angle), "Invalid rotation encoding");
        curves(t.curves);
        m.skeletal.tracks.push_back(t);
    }
    token("materials");
    n = count(4096);
    for (std::size_t i = 0; i < n; ++i) {
        token("material");
        MaterialTrack t;
        t.kind = MaterialTrack::Kind(count(2));
        t.material = name();
        t.slot = unsigned(count(255));
        curves(t.curves);
        token("textures");
        auto keys = count(65536);
        for (std::size_t k = 0; k < keys; ++k) {
            auto frame = count(unsigned(m.skeletal.frames));
            t.textures.push_back({unsigned(frame), name()});
        }
        m.material.tracks.push_back(t);
    }
    token("visibility");
    n = count(4096);
    for (std::size_t i = 0; i < n; ++i) {
        token("mesh");
        VisibilityTrack t;
        t.mesh = name();
        auto frames = count(65536);
        for (std::size_t f = 0; f < frames; ++f) {
            bool v;
            require(bool(in >> v), "Invalid visibility value");
            t.frames.push_back(v);
        }
        m.visibility.tracks.push_back(t);
    }
    token("end");
    require(!(in >> word), "Unexpected motion exchange content");
    return m;
}
}
