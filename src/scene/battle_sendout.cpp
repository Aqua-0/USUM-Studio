#include "scene/battle_sendout.h"
#include "formats/archive.h"
#include <algorithm>
#include <cmath>
#include <map>
namespace studio {
BattleCameraMotion decode_battle_camera(View env, View bytes) {
    require(text(slice(env, 0, 8)) == "GFBENV" && u16(env, 8) == 1 && u16(env, 24) == 1,
            "Send-out needs one environment camera");
    unsigned count = 1 + u16(env, 20) + u16(env, 22) + u16(env, 24), slot = count - 1;
    auto camera = slice(env, u32(env, 28 + slot * 4), u32(env, 28 + count * 4 + slot * 4));
    require(camera.size() == 224, "Unsupported camera environment layout");
    auto name = text(slice(camera, 16, 80));
    BattleCameraMotion out;
    for (unsigned i = 0; i < 4; ++i)
        out.defaults[i] = f32(camera, 208 + i * 4);
    for (unsigned i = 0; i < 3; ++i) {
        out.defaults[4 + i] = f32(camera, 192 + i * 4);
        out.defaults[7 + i] = f32(camera, 176 + i * 4);
    }
    require(u32(bytes, 0) == 0x60000 && u32(bytes, 4) <= 64, "Unsupported camera motion");
    std::map<unsigned, View> sections;
    for (unsigned i = 0; i < u32(bytes, 4); ++i) {
        auto p = 8 + i * 12;
        require(sections.emplace(u32(bytes, p), slice(bytes, u32(bytes, p + 8), u32(bytes, p + 4)))
                    .second,
                "Duplicate camera motion section");
    }
    require(sections.contains(0) && sections.contains(9), "Camera motion lacks tracks");
    out.frames = float(u32(sections.at(0), 0));
    require(out.frames > 0 && out.frames <= 65535, "Invalid camera duration");
    auto data = sections.at(9);
    require(u32(data, 0) == 1, "Expected one animated camera");
    auto names = slice(data, 8, u32(data, 4));
    require(text(slice(names, 1, slice(names, 0, 1)[0])) == name,
            "Camera name does not match environment");
    std::size_t p = 8 + names.size();
    auto flags = std::uint64_t(u32(data, p)) | (std::uint64_t(u32(data, p + 4)) << 32);
    auto end = p + 12 + u32(data, p + 8);
    slice(data, 0, end);
    p += 12;
    for (unsigned i = 0; i < 10; ++i)
        out.curves[i] =
            decode_animation_curve(data, p, unsigned((flags >> (i * 3)) & 7), unsigned(out.frames));
    require(p == end, "Camera curve payload mismatch");
    for (float f : out.defaults)
        require(std::isfinite(f), "Invalid camera default");
    return out;
}
BattleCameraPose BattleCameraMotion::sample(float frame) const {
    auto v = defaults;
    for (unsigned i = 0; i < 10; ++i)
        v[i] = curves[i].sample(std::clamp(frame, 0.f, frames), v[i]);
    BattleCameraPose p;
    p.near_clip = v[0];
    p.far_clip = v[1];
    p.fov = v[2] * 180.f / 3.141592653589793f;
    p.aspect = v[3];
    p.eye = {v[7], v[8], v[9]};
    float cx = std::cos(v[4]), sx = std::sin(v[4]), cy = std::cos(v[5]), sy = std::sin(v[5]),
          cz = std::cos(v[6]), sz = std::sin(v[6]);
    std::array<float, 3> front{cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx};
    p.up = {cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx};
    float distance = std::sqrt(v[7] * v[7] + v[8] * v[8] + v[9] * v[9]);
    for (unsigned i = 0; i < 3; ++i)
        p.target[i] = p.eye[i] - front[i] * std::max(distance, 1.f);
    require(p.fov > 0 && p.fov < 179 && p.near_clip > 0 && p.far_clip > p.near_clip && p.aspect > 0,
            "Invalid camera projection");
    return p;
}
BattleSendOut load_battle_sendout(const std::filesystem::path &dump, bool alternate) {
    BattleSendOut out;
    out.member =
        alternate ? BattleProfile::single_sendout_alternate : BattleProfile::single_sendout;
    auto bytes = read_archive_subfile(dump / BattleProfile::sequences_archive, out.member);
    require(text(slice(bytes, 0, 4)) == "SESD" && u32(bytes, 4) == 3,
            "Unsupported send-out sequence");
    float release = -1;
    out.frames = float(u32(bytes, 12));
    struct Command {
        unsigned start, type;
        Bytes data;
    };
    std::vector<Command> commands;
    std::size_t p = 16;
    while (p + 4 <= bytes.size()) {
        auto start = u32(bytes, p);
        if (start == 0xffffffffu)
            break;
        auto type = u16(bytes, p + 14);
        require(start <= u32(bytes, 12) && u32(bytes, p + 4) >= start, "Invalid sequence timing");
        auto schema = std::find_if(BattleProfile::sendout_commands.begin(),
                                   BattleProfile::sendout_commands.end(), [&](auto &s) {
                                       return s[0] == type;
                                   });
        require(schema != BattleProfile::sendout_commands.end(),
                "Unsupported command in standard send-out sequence");
        auto data = slice(bytes, p + 16, (*schema)[1]);
        if (u16(bytes, p + 10) == 0 && u16(bytes, p + 12) == 0) {
            commands.push_back({start, type, Bytes(data.begin(), data.end())});
            if (type == 20) {
                require(release < 0 && u32(data, 0) == 0, "Expected one send-out subject");
                release = float(start);
            }
        }
        p += 16 + (*schema)[1];
    }
    require(release >= 0 && out.frames > release, "Sequence has no Pokemon entrance");
    out.frames -= release;
    out.camera_end = out.frames;
    for (auto &command : commands) {
        if (command.start < release)
            continue;
        auto &b = command.data;
        float at = command.start - release;
        if (command.type == 41) {
            require(u32(b, 8) == 0 && u32(b, 28) == 0 && u32(b, 36) == 0,
                    "Unsupported send-out camera targeting");
            SendOutCamera c;
            c.start = at;
            c.node = u32(b, 12);
            c.scale = u32(b, 32) != 0;
            require(c.node == 50 || c.node == 16 || c.node == 15, "Unsupported send-out anchor");
            for (unsigned i = 0; i < 3; ++i)
                require(f32(b, 16 + i * 4) == 0, "Unsupported send-out camera offset");
            c.motion = decode_battle_camera(
                read_archive_subfile(dump / BattleProfile::effects_archive, u32(b, 0)),
                read_archive_subfile(dump / BattleProfile::effects_archive, u32(b, 4)));
            out.cameras.push_back(std::move(c));
        } else if (command.type == 48) {
            require(!out.cameras.empty() && out.cameras.back().start == at,
                    "Unsupported camera speed transition");
            out.cameras.back().speed = f32(b, 0);
            require(out.cameras.back().speed > 0 && out.cameras.back().speed <= 4,
                    "Invalid camera speed");
        } else if (command.type == 45)
            out.camera_end = at;
    }
    require(out.cameras.size() == 2 && out.cameras.front().start == 0,
            "Unexpected standard send-out camera sequence");
    return out;
}
SendOutPhase battle_sendout_phase(float frame, float jump, float landing, float alternate,
                                  float center) {
    frame = std::max(0.f, frame);
    SendOutPhase p;
    p.growth = .01f + .99f * std::clamp(frame / 7.f, 0.f, 1.f);
    float offset = center * (1 - std::clamp(frame / 7.f, 0.f, 1.f));
    if (jump > 0) {
        float height = 200 - center;
        if (frame < jump) {
            p.slot = 3;
            p.loop = false;
            p.height = height;
        } else if (frame < jump + 5) {
            p.slot = 4;
            p.start = jump;
            p.height = height * (1 - (frame - jump) / 5);
        } else if (frame < jump + 5 + landing) {
            p.slot = 5;
            p.start = jump + 5;
            p.loop = false;
        } else
            p.start = jump + 5 + landing;
    } else if (alternate > 0) {
        if (frame < alternate) {
            p.slot = 6;
            p.loop = false;
        } else
            p.start = alternate;
    }
    p.height += offset;
    return p;
}
}
