#include "scene/particles.h"
#include "scene/environment.h"
#include "field/area.h"
#include <algorithm>
#include <cmath>
namespace studio {
std::vector<ParticleEmitter> decode_particle_emitters(View b,
                                                      std::vector<std::string> &diagnostics) {
    require(u32(b, 0) == 0x44425053 && u32(b, 4) == 51, "Unsupported particle resource version");
    auto sets = u32(b, 8);
    require(sets <= 256, "Excessive emitter sets");
    slice(b, 72, std::size_t(sets) * 28);
    std::vector<ParticleEmitter> out;
    for (unsigned set = 0; set < sets; ++set) {
        auto at = 72 + set * 28, count = u32(b, at + 16), table = u32(b, at + 20);
        require(count <= 256, "Excessive particle emitters");
        slice(b, table, std::size_t(count) * 8);
        for (unsigned i = 0; i < count; ++i)
            try {
                auto offset = u32(b, table + i * 8);
                auto e = slice(b, offset, 1352);
                require(u32(e, 0) <= 1 && u32(e, 804) == 0 && u32(e, 808) <= 4,
                        "Unsupported particle mesh or emitter type");
                ParticleEmitter p;
                auto name = std::size_t(u32(b, 16)) + u32(e, 56);
                require(name < b.size(), "Particle name out of bounds");
                p.name =
                    text(slice(b, u32(b, 16), std::min(std::size_t(128), b.size() - u32(b, 16)))) +
                    " / " + text(slice(b, name, std::min(std::size_t(128), b.size() - name)));
                auto read = [&](unsigned o) {
                    auto f = f32(e, o);
                    require(std::isfinite(f) && std::abs(f) <= 1e7f, "Invalid particle value");
                    return f;
                };
                p.addressing = u32(e, 868);
                require(p.addressing <= 3, "Unsupported particle addressing");
                p.uv_scale = {read(872), read(876)};
                p.animate_pattern = e[824] != 0;
                p.random_pattern = e[825] != 0;
                p.tiles_x = std::max(1u, unsigned(e[827]));
                p.tiles_y = std::max(1u, unsigned(e[828]));
                p.pattern_count = std::clamp(unsigned(u16(e, 834)), 1u, 32u);
                p.pattern_frequency = std::max(1u, unsigned(u16(e, 832)));
                for (unsigned k = 0; k < 32; ++k)
                    p.pattern[k] = e[836 + k];
                p.blend = u32(e, 580);
                p.billboard = u32(e, 808);
                p.volume = u32(e, 592);
                p.rate = read(708);
                p.life = float(std::int32_t(u32(e, 796)));
                p.life_random = float(std::int32_t(u32(e, 800)));
                p.start = float(std::int32_t(u32(e, 712)));
                p.end = float(std::int32_t(u32(e, 716)));
                p.velocity = read(732);
                p.velocity_random = read(736);
                p.alpha = read(1212);
                p.alpha_middle = p.alpha + read(1216);
                p.alpha_end = p.alpha_middle + read(1220);
                p.alpha_split = float(u32(e, 1224)) * .01f;
                p.scale_split = float(u32(e, 1240)) * .01f;
                p.scale_random = read(1248);
                require(p.life > 0 && p.life <= 65535 && p.rate >= 0 && p.rate <= 10000,
                        "Invalid particle lifetime or rate");
                for (unsigned k = 0; k < 3; ++k) {
                    p.translation[k] = read(544 + k * 4);
                    p.radius[k] = read(596 + k * 4);
                    p.direction[k] = read(740 + k * 4);
                    p.gravity[k] = read(772 + k * 4);
                    p.color[k] = read(1072 + k * 4) * read(656 + k * 4) * read(1208);
                }
                p.color[3] = read(688);
                for (unsigned k = 0; k < 2; ++k) {
                    p.size[k] = read(1252 + k * 4);
                    p.scale[k] = read(1260 + k * 4);
                    p.scale_middle[k] = p.scale[k] + read(1268 + k * 4);
                    p.scale_end[k] = p.scale_middle[k] + read(1276 + k * 4);
                }
                auto format = u32(e, 172);
                static constexpr unsigned formats[] = {0x2b, 0x2a, 0,    0x28, 0x29, 0x25, 0x26,
                                                       0x27, 0x23, 0x17, 0x16, 4,    3,    2};
                require(format >= 18 && format <= 31 && format != 20,
                        "Unsupported particle texture format");
                auto texture = slice(b, std::size_t(u32(b, 20)) + u32(e, 180), u32(e, 176));
                Bytes wrapped(128);
                put32(wrapped, 0, 0x15041213);
                put32(wrapped, 24, narrow(texture.size()));
                put16(wrapped, 104, u16(e, 64));
                put16(wrapped, 106, u16(e, 66));
                put16(wrapped, 108, std::uint16_t(formats[format - 18]));
                put16(wrapped, 110, 1);
                append(wrapped, texture);
                p.texture = decode_field_texture(wrapped);
                if (u32(e, 0) == 1) {
                    std::string note = "Complex particles use the base emitter; child particles, "
                                       "force fields and trails are not yet evaluated.";
                    if (std::find(diagnostics.begin(), diagnostics.end(), note) ==
                        diagnostics.end())
                        diagnostics.push_back(note);
                }
                out.push_back(std::move(p));
            } catch (const std::exception &e) {
                diagnostics.push_back("Particle emitter " + std::to_string(i) + ": " + e.what());
            }
    }
    return out;
}
std::vector<WeatherParticles> load_weather_particles(const std::filesystem::path &dump,
                                                     std::vector<std::string> &diagnostics) {
    auto resident =
        Container::parse(Archive(dump / TargetProfile::resident_archive).decoded(0), "FR");
    auto effects = Container::parse(resident.files.at(TargetProfile::resident_effects), "SB");
    std::vector<WeatherParticles> out;
    for (auto binding : TargetProfile::weather_particles) {
        auto pack = Container::parse(effects.files.at(binding[1]), "SB");
        out.push_back(
            {binding[0], decode_particle_emitters(pack.files.at(binding[2]), diagnostics)});
    }
    return out;
}
std::vector<ParticleVertex> particle_vertices(const ParticleEmitter &e, double seconds,
                                              const std::array<float, 3> &origin,
                                              const std::array<float, 3> &camera_right,
                                              const std::array<float, 3> &camera_up, float light) {
    std::vector<ParticleVertex> out;
    if (e.rate <= 0 || seconds < 0 || !std::isfinite(seconds))
        return out;
    double frame = std::min(seconds, 1e8) * 30;
    auto last = std::floor((std::min(frame, double(e.end)) - e.start) * e.rate);
    if (last < 0)
        return out;
    auto first = std::max(0., std::ceil((frame - e.life - e.start) * e.rate));
    first = std::max(first, last - 4095);
    out.reserve(std::size_t(std::max(0., last - first + 1)) * 6);
    auto random = [](std::uint32_t n) {
        n ^= n >> 16;
        n *= 0x7feb352du;
        n ^= n >> 15;
        n *= 0x846ca68bu;
        n ^= n >> 16;
        return float(n & 0xffffff) / 16777216.f;
    };
    auto curve = [](float a, float b, float c, float split, float t) {
        split = std::clamp(split, .001f, .999f);
        return t < split ? a + (b - a) * t / split : b + (c - b) * (t - split) / (1 - split);
    };
    for (double index = first; index <= last; ++index) {
        auto seed = std::uint32_t(std::fmod(index, 4294967295.));
        float age = float(frame - e.start - index / e.rate),
              life = std::max(1.f, e.life - random(seed * 17 + 1) * e.life_random);
        if (age < 0 || age >= life)
            continue;
        float t = age / life, angle = random(seed * 17 + 2) * 6.2831853f,
              radius = std::sqrt(random(seed * 17 + 3));
        std::array<float, 3> p = origin, velocity{}, right = camera_right, up = camera_up;
        for (unsigned k = 0; k < 3; ++k) {
            float offset = e.volume == 0   ? 0
                           : e.volume == 3 ? (k == 0   ? std::cos(angle) * radius
                                              : k == 2 ? std::sin(angle) * radius
                                                       : 0)
                                           : (random(seed * 17 + 4 + k) * 2 - 1);
            velocity[k] = e.direction[k] * e.velocity +
                          (random(seed * 17 + 7 + k) * 2 - 1) * e.velocity_random;
            p[k] += e.translation[k] + offset * e.radius[k] + velocity[k] * age +
                    .5f * e.gravity[k] * age * age;
        }
        if (e.billboard == 1) {
            right = {1, 0, 0};
            up = {0, 1, 0};
        }
        if (e.billboard == 2) {
            right = {1, 0, 0};
            up = {0, 0, 1};
        }
        if (e.billboard == 3 || e.billboard == 4) {
            float x = 0, y = 0;
            for (unsigned k = 0; k < 3; ++k) {
                x += velocity[k] * camera_right[k];
                y += velocity[k] * camera_up[k];
            }
            float length = std::sqrt(x * x + y * y);
            if (length > 1e-5f)
                for (unsigned k = 0; k < 3; ++k) {
                    up[k] = (camera_right[k] * x + camera_up[k] * y) / length;
                    right[k] = (camera_right[k] * y - camera_up[k] * x) / length;
                }
        }
        float size[2];
        for (unsigned k = 0; k < 2; ++k)
            size[k] = e.size[k] *
                      curve(e.scale[k], e.scale_middle[k], e.scale_end[k], e.scale_split, t) *
                      (1 - e.scale_random * random(seed * 17 + 12));
        auto alpha = std::clamp(
            curve(e.alpha, e.alpha_middle, e.alpha_end, e.alpha_split, t) * e.color[3], 0.f, 1.f);
        std::uint32_t color = std::uint32_t(alpha * 255) << 24;
        for (unsigned k = 0; k < 3; ++k)
            color |= std::uint32_t(std::clamp(e.color[k] * light, 0.f, 1.f) * 255) << (k * 8);
        unsigned tile_index =
            e.random_pattern ? unsigned(random(seed * 17 + 13) * e.pattern_count) : 0;
        if (e.animate_pattern)
            tile_index += unsigned(age) / e.pattern_frequency;
        auto tile = e.pattern[tile_index % e.pattern_count] % (e.tiles_x * e.tiles_y);
        for (auto corner : {0, 1, 2, 0, 2, 3}) {
            float u = (corner == 1 || corner == 2) ? 1.f : 0.f, v = corner >= 2 ? 1.f : 0.f;
            auto q = p;
            for (unsigned k = 0; k < 3; ++k)
                q[k] += right[k] * (u * 2 - 1) * size[0] + up[k] * (v * 2 - 1) * size[1];
            out.push_back({q[0], q[1], q[2], (u + float(tile % e.tiles_x)) * e.uv_scale[0],
                           1 - (v + float(tile / e.tiles_x)) * e.uv_scale[1], color});
        }
    }
    return out;
}
}
