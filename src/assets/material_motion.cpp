#include "assets/material_motion.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace studio {
MaterialMotion key_material_uv_transform(const MaterialMotion &motion, const std::string &material,
                                         unsigned unit, unsigned frame,
                                         const std::array<float, 5> &values,
                                         const std::array<float, 5> &base) {
    require(!material.empty() && unit < 3 && frame <= motion.frames,
            "Invalid UV animation target or frame");
    auto next = motion;
    auto track = std::find_if(next.tracks.begin(), next.tracks.end(), [&](auto &t) {
        return t.kind == MaterialTrack::Kind::TextureTransform && t.material == material &&
               t.slot == unit;
    });
    if (track == next.tracks.end()) {
        MaterialTrack created;
        created.material = material;
        created.slot = unit;
        next.tracks.push_back(created);
        track = next.tracks.end() - 1;
    }
    for (unsigned i = 0; i < 5; ++i) {
        require(std::isfinite(values[i]) && std::isfinite(base[i]),
                "Mapping values must be finite");
        auto &keys = track->curves[i].keys;
        std::erase_if(keys, [&](auto &key) {
            return key.frame == frame;
        });
        if (keys.empty() && frame > 0)
            keys.push_back({0, base[i], 0});
        keys.push_back({float(frame), values[i], 0});
        std::sort(keys.begin(), keys.end(), [](auto &a, auto &b) {
            return a.frame < b.frame;
        });
    }
    return next;
}
MaterialMotion make_atlas_flipbook(const MaterialMotion &motion, const std::string &material,
                                   unsigned slot, const AtlasFlipbook &atlas) {
    require(std::isfinite(motion.frames) && motion.frames >= 1 && motion.frames <= 65535 &&
                std::floor(motion.frames) == motion.frames,
            "Choose a motion with whole frames");
    const auto cells = std::uint64_t(atlas.columns) * atlas.rows;
    require(!material.empty() && slot < 3 && atlas.columns && atlas.rows && atlas.tile_count &&
                atlas.hold_frames && atlas.first_tile < cells &&
                std::uint64_t(atlas.first_tile) + atlas.tile_count <= cells,
            "Choose a valid grid, tile range and hold time");
    MaterialTrack track;
    track.material = material;
    track.slot = slot;
    track.curves[0].keys = {{0, atlas.tile_uvs ? 1.f : 1.f / atlas.columns, 0}};
    track.curves[1].keys = {{0, atlas.tile_uvs ? 1.f : 1.f / atlas.rows, 0}};
    track.curves[2].keys = {{0, 0, 0}};
    for (unsigned channel = 3; channel < 5; ++channel) {
        auto &keys = track.curves[channel].keys;
        for (unsigned frame = 0; frame <= unsigned(motion.frames); ++frame) {
            unsigned step = frame / atlas.hold_frames;
            unsigned tile =
                atlas.first_tile +
                (atlas.repeat ? step % atlas.tile_count : std::min(step, atlas.tile_count - 1));
            float value =
                channel == 3 ? -float(tile % atlas.columns) : -float(tile / atlas.columns);
            if (atlas.tile_uvs)
                value /= channel == 3 ? atlas.columns : atlas.rows;
            if (keys.empty() || value != keys.back().value) {
                if (!keys.empty() && keys.back().frame < frame - 1)
                    keys.push_back({float(frame - 1), keys.back().value, 0});
                keys.push_back({float(frame), value, 0});
            }
        }
    }
    auto next = motion;
    std::erase_if(next.tracks, [&](const MaterialTrack &existing) {
        return existing.material == material && existing.slot == slot &&
               existing.kind != MaterialTrack::Kind::ConstantColor;
    });
    next.tracks.push_back(std::move(track));
    return next;
}
Bytes encode_material_tracks(const MaterialMotion &motion, MaterialTrack::Kind kind) {
    std::map<std::string, std::vector<const MaterialTrack *>> groups;
    std::vector<std::string> textures;
    for (auto &t : motion.tracks)
        if (t.kind == kind) {
            groups[t.material].push_back(&t);
            for (auto &k : t.textures)
                if (std::find(textures.begin(), textures.end(), k.texture) == textures.end())
                    textures.push_back(k.texture);
        }
    Bytes names, texture_names;
    auto name = [&](Bytes &b, const std::string &s) {
        require(s.size() < 256, "Motion binding name too long");
        b.push_back(std::uint8_t(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    };
    for (auto &[n, tracks] : groups)
        name(names, n);
    for (auto &n : textures)
        name(texture_names, n);
    names.resize(aligned(names.size(), 4));
    texture_names.resize(aligned(texture_names.size(), 4));
    Bytes b;
    append32(b, narrow(groups.size()));
    append32(b, narrow(names.size()));
    if (kind == MaterialTrack::Kind::TexturePattern) {
        append32(b, narrow(textures.size()));
        append32(b, narrow(texture_names.size()));
    }
    for (auto &[n, tracks] : groups)
        append32(b, narrow(tracks.size()));
    append(b, names);
    append(b, texture_names);
    for (auto &[n, tracks] : groups)
        for (auto *t : tracks) {
            append32(b, t->slot);
            if (kind == MaterialTrack::Kind::TexturePattern) {
                append32(b, narrow(t->textures.size()));
                for (auto &k : t->textures) {
                    auto at = b.size();
                    b.resize(at + 4);
                    put16(b, at, std::uint16_t(k.frame));
                    put16(b, at + 2,
                          std::uint16_t(std::find(textures.begin(), textures.end(), k.texture) -
                                        textures.begin()));
                }
                continue;
            }
            auto header = b.size();
            append32(b, 0);
            append32(b, 0);
            std::uint32_t flags = 0;
            for (unsigned c = 0; c < (kind == MaterialTrack::Kind::TextureTransform ? 5u : 4u);
                 ++c) {
                auto &keys = t->curves[c].keys;
                if (keys.empty())
                    continue;
                if (keys.size() == 1) {
                    flags |= 3u << (c * 3);
                    auto at = b.size();
                    b.resize(at + 4);
                    put_float(b, at, keys[0].value);
                } else {
                    flags |= 5u << (c * 3);
                    append32(b, narrow(keys.size()));
                    for (auto &k : keys) {
                        if (motion.frames <= 255)
                            b.push_back(std::uint8_t(k.frame));
                        else {
                            auto at = b.size();
                            b.resize(at + 2);
                            put16(b, at, std::uint16_t(k.frame));
                        }
                    }
                    b.resize(aligned(b.size(), 4), 255);
                    for (auto &k : keys) {
                        auto at = b.size();
                        b.resize(at + 8);
                        put_float(b, at, k.value);
                        put_float(b, at + 4, k.slope);
                    }
                }
            }
            put32(b, header, flags);
            put32(b, header + 4, narrow(b.size() - header - 8));
        }
    return b;
}

Bytes replace_material_motion(View original, const MaterialMotion &motion) {
    auto before = decode_material_motion(original);
    if (motion == before)
        return Bytes(original.begin(), original.end());
    require(motion.frames == before.frames && motion.looping == before.looping,
            "Keep the source motion duration and loop flag; other animation channels share them");
    std::set<std::tuple<int, std::string, unsigned>> used;
    for (auto &t : motion.tracks) {
        require(!t.material.empty() && t.material.size() < 256 &&
                    t.slot < (t.kind == MaterialTrack::Kind::ConstantColor ? 6u : 3u),
                "Invalid material motion binding");
        require(used.emplace(int(t.kind), t.material, t.slot).second,
                "A track already exists for this material, property and slot");
        require(t.kind == MaterialTrack::Kind::TextureTransform ||
                    t.kind == MaterialTrack::Kind::ConstantColor ||
                    t.kind == MaterialTrack::Kind::TexturePattern,
                "Unsupported material track kind");
        for (auto &curve : t.curves) {
            float previous = -1;
            for (auto &k : curve.keys) {
                require(std::isfinite(k.frame) && k.frame >= 0 && k.frame <= motion.frames &&
                            std::floor(k.frame) == k.frame && k.frame > previous &&
                            std::isfinite(k.value) && std::isfinite(k.slope),
                        "Keys need ordered whole frames and finite values/slopes");
                previous = k.frame;
            }
            if (curve.keys.size() == 1)
                require(curve.keys.front().frame == 0,
                        "A constant channel's single key must be at frame zero");
        }
        if (t.kind == MaterialTrack::Kind::TexturePattern)
            require(
                !t.textures.empty() && t.textures.front().frame == 0,
                "Texture switches need a starting key at frame zero; remove the track to clear it");
        int previous = -1;
        for (auto &k : t.textures) {
            require(k.frame <= motion.frames && int(k.frame) > previous && !k.texture.empty() &&
                        k.texture.size() < 256,
                    "Invalid texture switch key");
            previous = int(k.frame);
        }
    }
    std::map<unsigned, Bytes> sections;
    for (unsigned i = 0; i < u32(original, 4); ++i) {
        auto p = 8 + i * 12;
        auto data = slice(original, u32(original, p + 8), u32(original, p + 4));
        sections[u32(original, p)] = {data.begin(), data.end()};
    }
    for (auto [type, kind] : {std::pair{3u, MaterialTrack::Kind::TextureTransform},
                              {4u, MaterialTrack::Kind::TexturePattern},
                              {5u, MaterialTrack::Kind::ConstantColor}}) {
        auto tracks = [&](const MaterialMotion &m) {
            std::vector<MaterialTrack> out;
            for (auto &t : m.tracks)
                if (t.kind == kind)
                    out.push_back(t);
            return out;
        };
        if (tracks(before) == tracks(motion))
            continue;
        bool present = std::any_of(motion.tracks.begin(), motion.tracks.end(), [&](auto &t) {
            return t.kind == kind;
        });
        if (present)
            sections[type] = encode_material_tracks(motion, kind);
        else
            sections.erase(type);
    }
    Bytes out(8 + sections.size() * 12);
    put32(out, 0, 0x60000);
    put32(out, 4, narrow(sections.size()));
    unsigned i = 0;
    for (auto &[type, data] : sections) {
        auto p = 8 + i++ * 12;
        put32(out, p, type);
        put32(out, p + 4, narrow(data.size()));
        put32(out, p + 8, narrow(out.size()));
        append(out, data);
    }
    decode_material_motion(out);
    return out;
}
}
