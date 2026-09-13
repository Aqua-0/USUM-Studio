#include "scene/lighting.h"
#include "scene/environment.h"
#include "field/area.h"
#include <algorithm>
#include <cmath>
namespace studio {
namespace {
float number(View b, std::size_t p) {
    auto v = f32(b, p);
    require(std::isfinite(v), "Non-finite lighting value");
    return v;
}
}
std::map<std::uint32_t, LightingTable> model_lighting_tables(const Model &model) {
    auto b = slice(model.original, 0, model.sections.front().offset + model.sections.front().size);
    std::size_t p = model.bounds_offset + 96;
    auto opaque = std::size_t(u32(b, p)) + u32(b, p + 4);
    p += 16 + opaque;
    auto bones = u32(b, p);
    p += 16;
    for (unsigned i = 0; i < bones; ++i) {
        p += 1 + slice(b, p, 1)[0];
        p += 1 + slice(b, p, 1)[0];
        p += 37;
        slice(b, 0, p);
    }
    p = aligned(p, 16);
    auto count = u32(b, p), length = u32(b, p + 4);
    p = aligned(p + 8, 16);
    require(count <= 4096, "Too many lighting tables");
    std::map<std::uint32_t, LightingTable> out;
    for (unsigned i = 0; i < count; ++i) {
        auto hash = u32(b, p);
        auto stream = slice(b, p + 16, length);
        p += 16 + length;
        LightingTable table;
        std::array<bool, 256> written{};
        unsigned index = 0;
        for (auto c : commands(stream)) {
            if (c.reg == 0x1c5)
                index = c.value & 255;
            else if (c.reg >= 0x1c8 && c.reg <= 0x1cf) {
                require(index < 256, "Lighting table index overflow");
                table.values[index] = float(c.value & 4095) / 4095.f;
                written[index++] = true;
            }
        }
        require(std::all_of(written.begin(), written.end(),
                            [](bool v) {
                                return v;
                            }),
                "Incomplete model lighting table");
        require(out.emplace(hash, table).second, "Duplicate model lighting table hash");
    }
    return out;
}
std::vector<LightSet> decode_light_sets(View b) {
    require(text(slice(b, 0, 8)) == "GFBENV" && u16(b, 8) == 1 && u16(b, 10) == 0,
            "Unsupported environment resource");
    auto textures = u16(b, 20), sets = u16(b, 22), cameras = u16(b, 24);
    auto count = 1u + textures + sets + cameras;
    require(count <= 256, "Excessive environment sections");
    slice(b, 28, count * 8);
    std::vector<LightSet> out;
    for (unsigned i = 0; i < sets; ++i) {
        auto slot = 1 + textures + i;
        auto data = slice(b, u32(b, 28 + slot * 4), u32(b, 28 + count * 4 + slot * 4));
        LightSet set;
        set.index = u32(data, 0);
        auto n = u32(data, 4);
        require(n <= 32 && data.size() == 8 + std::size_t(n) * 144,
                "Unsupported light record layout");
        for (unsigned j = 0; j < n; ++j) {
            auto p = 8 + j * 144;
            SceneLight light;
            light.name = text(slice(data, p + 4, 80));
            light.type = u32(data, p + 136);
            require(light.type <= 1, "Point/spot environment lights require attenuation support");
            for (unsigned k = 0; k < 3; ++k) {
                light.position[k] = number(data, p + 84 + k * 4);
                light.color[k] = number(data, p + 100 + k * 4);
                light.direction[k] = number(data, p + 116 + k * 4);
            }
            light.color[3] = 1;
            set.lights.push_back(light);
        }
        out.push_back(std::move(set));
    }
    return out;
}
EnvironmentEffects decode_environment_effects(View b) {
    require(text(slice(b, 0, 8)) == "GFBENV" && u16(b, 8) == 1,
            "Unsupported environment effects resource");
    auto count = 1u + u16(b, 20) + u16(b, 22) + u16(b, 24);
    auto data = slice(b, u32(b, 28), u32(b, 28 + count * 4));
    slice(data, 0, 424);
    EnvironmentEffects out;
    for (unsigned i = 0; i < 4; ++i) {
        auto p = 256 + i * 32;
        auto &fog = out.fog[i];
        fog.enabled = u32(data, p) != 0;
        fog.near_distance = number(data, p + 4);
        fog.far_distance = number(data, p + 8);
        fog.strength = number(data, p + 12);
        for (unsigned c = 0; c < 4; ++c)
            fog.color[c] = number(data, p + 16 + c * 4);
    }
    auto &bloom = out.bloom;
    bloom.enabled = u16(data, 384) != 0;
    bloom.reductions = u16(data, 386);
    bloom.strength = number(data, 388);
    bloom.range = number(data, 392);
    bloom.threshold = number(data, 396);
    for (unsigned c = 0; c < 4; ++c)
        bloom.weight[c] = number(data, 400 + c * 4);
    return out;
}
EnvironmentEffects evaluate_environment_effects(const LightingContext &context, unsigned motion,
                                                float hour) {
    auto out = context.effects;
    if (motion >= context.motions.size())
        return out;
    auto &m = context.motions[motion];
    float frame = std::clamp(hour, 0.f, 24.f) / 24 * m.frames;
    for (auto &track : m.fog) {
        auto &fog = out.fog.at(track.slot);
        fog.near_distance = track.curves[0].sample(frame, fog.near_distance);
        fog.far_distance = track.curves[1].sample(frame, fog.far_distance);
        for (unsigned c = 0; c < 3; ++c)
            fog.color[c] = track.curves[c + 2].sample(frame, fog.color[c]);
        fog.strength = track.curves[5].sample(frame, fog.strength);
    }
    auto &bloom = out.bloom;
    bloom.strength = m.bloom[0].sample(frame, bloom.strength);
    bloom.range = m.bloom[1].sample(frame, bloom.range);
    bloom.threshold = m.bloom[2].sample(frame, bloom.threshold);
    for (unsigned c = 0; c < 3; ++c)
        bloom.weight[c] = m.bloom[c + 3].sample(frame, bloom.weight[c]);
    return out;
}
LightMotion decode_light_motion(View b) {
    require(u32(b, 0) == 0x60000, "Unsupported environment motion version");
    auto sections = u32(b, 4);
    require(sections <= 32, "Excessive motion sections");
    LightMotion out;
    View tracks, fog, glare;
    for (unsigned i = 0; i < sections; ++i) {
        auto p = 8 + i * 12;
        auto data = slice(b, u32(b, p + 8), u32(b, p + 4));
        if (u32(b, p) == 0)
            out.frames = float(u32(data, 0));
        if (u32(b, p) == 8)
            tracks = data;
        if (u32(b, p) == 10)
            fog = data;
        if (u32(b, p) == 11)
            glare = data;
    }
    require(out.frames > 0 && out.frames <= 65535, "Invalid environment motion duration");
    if (!fog.empty()) {
        auto n = u32(fog, 0);
        require(n <= 4, "Excessive fog animation slots");
        std::size_t p = 8;
        for (unsigned i = 0; i < n; ++i) {
            FogTrack track;
            track.slot = u32(fog, p);
            require(track.slot < 4, "Invalid fog animation slot");
            auto flags = std::uint64_t(u32(fog, p + 8)) | (std::uint64_t(u32(fog, p + 12)) << 32);
            auto end = p + 20 + u32(fog, p + 16);
            auto payload = slice(fog, 0, end);
            p += 20;
            for (unsigned c = 0; c < 6; ++c)
                track.curves[c] = decode_animation_curve(
                    payload, p, unsigned((flags >> (c * 3)) & 7), unsigned(out.frames));
            require(p == end, "Fog curve extent mismatch");
            out.fog.push_back(std::move(track));
        }
    }
    if (!glare.empty()) {
        auto flags = std::uint64_t(u32(glare, 0)) | (std::uint64_t(u32(glare, 4)) << 32);
        auto end = 12 + std::size_t(u32(glare, 8));
        auto payload = slice(glare, 0, end);
        std::size_t p = 12;
        for (unsigned c = 0; c < 6; ++c)
            out.bloom[c] = decode_animation_curve(payload, p, unsigned((flags >> (c * 3)) & 7),
                                                  unsigned(out.frames));
        require(p == end, "Bloom curve extent mismatch");
    }
    if (tracks.empty())
        return out;
    auto count = u32(tracks, 0);
    require(count <= 256, "Excessive animated lights");
    std::size_t name = 8, p = 8 + std::size_t(u32(tracks, 4));
    slice(tracks, 0, p);
    for (unsigned i = 0; i < count; ++i) {
        LightTrack track;
        auto n = slice(tracks, name++, 1)[0];
        track.name = text(slice(tracks, name, n));
        name += n;
        require(name <= 8 + u32(tracks, 4), "Light names overlap curves");
        track.set = u32(tracks, p);
        track.type = u32(tracks, p + 4);
        auto flags = std::uint64_t(u32(tracks, p + 8)) | (std::uint64_t(u32(tracks, p + 12)) << 32);
        auto end = p + 24 + u32(tracks, p + 16);
        slice(tracks, p, end - p);
        p += 24;
        for (unsigned channel = 0; channel < 11; ++channel) {
            auto mode = (flags >> (channel * 3)) & 7;
            auto &curve = track.curves[channel];
            if (mode == 0)
                continue;
            if (mode <= 2) {
                curve.keys.push_back({0, float(mode - 1), 0});
                continue;
            }
            if (mode == 3) {
                curve.keys.push_back({0, number(tracks, p), 0});
                p += 4;
                continue;
            }
            require(mode == 4 || mode == 5, "Unsupported light curve encoding");
            auto keys = u32(tracks, p);
            p += 4;
            require(keys > 0 && keys <= 65536, "Invalid light curve key count");
            std::vector<float> frames;
            for (unsigned k = 0; k < keys; ++k) {
                auto f = out.frames <= 255 ? slice(tracks, p++, 1)[0] : u16(tracks, p);
                if (out.frames > 255)
                    p += 2;
                require(f <= out.frames && (frames.empty() || f > frames.back()),
                        "Invalid light key frame order");
                frames.push_back(float(f));
            }
            while (p % 4 && slice(tracks, p, 1)[0] == 255)
                ++p;
            float vs = 1, vo = 0, ss = 1, so = 0;
            if (mode == 4) {
                vs = number(tracks, p);
                vo = number(tracks, p + 4);
                ss = number(tracks, p + 8);
                so = number(tracks, p + 12);
                p += 16;
            }
            for (auto frame : frames) {
                float v = mode == 4 ? float(u16(tracks, p)) / 65535.f * vs + vo : number(tracks, p);
                float s = mode == 4 ? float(u16(tracks, p + 2)) / 65535.f * ss + so
                                    : number(tracks, p + 4);
                p += mode == 4 ? 4 : 8;
                curve.keys.push_back({frame, v, s});
            }
        }
        require(p == end, "Light curve payload mismatch");
        out.tracks.push_back(std::move(track));
    }
    return out;
}
std::vector<LightSet> evaluate_lights(const LightingContext &context, unsigned motion, float hour) {
    auto sets = context.sets;
    if (motion >= context.motions.size())
        return sets;
    auto &m = context.motions[motion];
    float frame = std::clamp(hour, 0.f, 24.f) * m.frames / 24.f;
    for (auto &track : m.tracks)
        for (auto &set : sets)
            if (set.index == track.set)
                for (auto &light : set.lights)
                    if (light.name == track.name && light.type == track.type)
                        for (unsigned k = 0; k < 3; ++k) {
                            light.position[k] = track.curves[k].sample(frame, light.position[k]);
                            light.direction[k] =
                                track.curves[k + 3].sample(frame, light.direction[k]);
                            light.color[k] = track.curves[k + 6].sample(frame, light.color[k]);
                        }
    return sets;
}
const WeatherProfile *weather_at_time(const LightingContext &context, float hour) {
    auto kind = context.schedule[weather_period(hour)];
    for (const auto &weather : context.weather)
        if (weather.kind == kind)
            return &weather;
    return nullptr;
}
std::vector<LightingContext> load_area_lighting(const std::filesystem::path &dump, std::size_t area,
                                                View environments) {
    Archive zone_archive(dump / TargetProfile::zone_archive),
        world_archive(dump / TargetProfile::world_archive),
        motion_archive(dump / TargetProfile::light_motion_archive);
    auto zones = zone_archive.decoded(0), worlds = zone_archive.decoded(1);
    require(zones.size() % 84 == 0 && worlds.size() == zones.size() / 84 * 2,
            "Unsupported zone lighting table layout");
    auto packs = Container::parse(environments, "AE");
    Bytes sky_positions;
    try {
        sky_positions = Archive(dump / TargetProfile::sky_position_archive).decoded(0);
        require(u32(sky_positions, 0) <= 4096, "Excessive sky positions");
        slice(sky_positions, 4, std::size_t(u32(sky_positions, 0)) * 8);
    } catch (const std::exception &) {
        sky_positions.clear();
    }
    Bytes weather_profiles, weather_types;
    try {
        weather_profiles = Archive(dump / TargetProfile::weather_profile_archive).decoded(0);
        weather_types = Archive(dump / TargetProfile::weather_type_archive).decoded(0);
    } catch (const std::exception &) {
    }
    std::map<unsigned, Bytes> headers;
    std::vector<LightingContext> out;
    for (unsigned zone = 0; zone < zones.size() / 84; ++zone) {
        auto z = slice(zones, zone * 84, 84);
        if (z[31] != 0)
            continue;
        auto world = u16(worlds, zone * 2);
        if (!headers.contains(world)) {
            auto wd = Container::parse(world_archive.decoded(world), "WD");
            headers[world] = wd.files.at(0);
        }
        auto &h = headers.at(world);
        auto begin = u32(h, 8), end = u32(h, 12);
        require(end >= begin && (end - begin) % 4 == 0, "Invalid world zone table");
        slice(h, begin, end - begin);
        bool matched = false;
        for (auto p = begin; p < end; p += 4)
            if (u16(h, p) == u16(z, 10) && u16(h, p + 2) == area)
                matched = true;
        if (!matched)
            continue;
        LightingContext c;
        c.zone = zone;
        for (unsigned period = 0; period < c.schedule.size(); ++period)
            c.schedule[period] = u32(z, 36 + period * 4);
        c.sky_enabled = (u32(z, 56) & (1u << 5)) != 0;
        if (!sky_positions.empty())
            for (unsigned i = 0; i < u32(sky_positions, 0); ++i)
                if (u32(sky_positions, 8 + i * 8) == world) {
                    c.sky_height = f32(sky_positions, 4 + i * 8);
                    require(std::isfinite(c.sky_height), "Invalid sky height");
                    break;
                }
        auto pack = Container::parse(packs.files.at(z[60]), "EP");
        if (pack.files.size() > 1 && !pack.files[1].empty())
            c.bloom_mask = std::make_shared<TextureImage>(decode_field_texture(pack.files[1]));
        c.sets = decode_light_sets(pack.files.at(0));
        c.effects = decode_environment_effects(pack.files.at(0));
        auto motion = Container::parse(motion_archive.decoded(z[34]), "WE");
        for (auto &b : motion.files)
            c.motions.push_back(b.empty() ? LightMotion{} : decode_light_motion(b));
        if (weather_types.size() == TargetProfile::weather_kinds * 8 &&
            weather_profiles.size() >= (std::size_t(z[34]) + 1) * TargetProfile::weather_kinds * 4)
            for (unsigned kind = 0; kind < TargetProfile::weather_kinds; ++kind) {
                auto index = u32(weather_profiles,
                                 (std::size_t(z[34]) * TargetProfile::weather_kinds + kind) * 4);
                if (index < c.motions.size() && c.motions[index].frames > 0)
                    c.weather.push_back({kind, u32(weather_types, kind * 8 + 4),
                                         u32(weather_types, kind * 8), index});
            }
        out.push_back(std::move(c));
    }
    require(!out.empty(), "No zone lighting context resolves to this field area");
    return out;
}
}
