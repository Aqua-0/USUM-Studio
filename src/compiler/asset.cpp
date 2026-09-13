#include "compiler/asset.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace studio {
AssetDocument AssetDocument::read(View bytes) {
    require(text(slice(bytes, 0, 8)) == "USUMAST" && u32(bytes, 8) == 1,
            "Unsupported asset document");
    std::size_t pos = 12;
    auto integer = [&]() {
        auto v = u32(bytes, pos);
        pos += 4;
        return v;
    };
    auto count = [&](unsigned maximum) {
        auto n = integer();
        require(n <= maximum, "Asset count exceeds supported limit");
        return n;
    };
    auto string = [&]() {
        auto n = count(63);
        auto v = text(slice(bytes, pos, n));
        pos += n;
        require(v.size() == n && n > 0 &&
                    std::all_of(v.begin(), v.end(),
                                [](unsigned char c) {
                                    return c >= 32 && c < 127;
                                }),
                "Asset names must be printable ASCII");
        return v;
    };
    auto number = [&]() {
        float v = f32(bytes, pos);
        pos += 4;
        require(std::isfinite(v), "Non-finite asset value");
        return v;
    };
    AssetDocument out;
    out.name = string();
    auto joints = count(255);
    require(joints > 0, "Asset has no rig");
    std::set<std::string> names;
    for (unsigned i = 0; i < joints; ++i) {
        Joint j;
        j.name = string();
        require(names.insert(j.name).second, "Repeated joint name");
        auto parent = integer();
        require(parent == 0xffffffff || parent < i, "Asset joints must be ordered parent first");
        j.parent = parent == 0xffffffff ? -1 : int(parent);
        j.parent_name = j.parent < 0 ? "" : out.joints[std::size_t(j.parent)].name;
        auto flags = integer();
        require(flags <= 3, "Unsupported joint flags");
        j.flags = static_cast<std::uint8_t>(flags);
        for (auto *values : {&j.scale, &j.rotation, &j.translation})
            for (auto &v : *values)
                v = number();
        for (auto v : j.scale)
            require(std::abs(v - 1) < 1e-5f, "Asset profile requires unit bind scales");
        out.joints.push_back(j);
    }
    auto materials = count(64);
    require(materials > 0, "Asset has no materials");
    names.clear();
    for (unsigned i = 0; i < materials; ++i) {
        AssetMaterial m;
        m.name = string();
        require(m.name.size() <= 50 && names.insert(m.name).second,
                "Invalid or repeated material name");
        auto size = count(64 * 1024 * 1024);
        m.image = read_tga(slice(bytes, pos, size));
        pos += size;
        out.materials.push_back(std::move(m));
    }
    auto meshes = count(256);
    require(meshes > 0, "Asset has no meshes");
    names.clear();
    for (unsigned i = 0; i < meshes; ++i) {
        AssetMesh m;
        m.name = string();
        require(names.insert(m.name).second, "Repeated mesh name");
        m.material = integer();
        require(m.material < materials, "Mesh material is missing");
        auto palette = count(20);
        require(palette > 0, "Empty mesh palette");
        std::set<unsigned> used;
        for (unsigned k = 0; k < palette; ++k) {
            auto index = integer();
            require(index < joints && used.insert(index).second, "Invalid palette joint");
            m.palette.push_back(static_cast<std::uint8_t>(index));
        }
        auto vertices = count(65535), indices = count(1000000);
        require(vertices > 0 && indices > 0 && indices % 3 == 0, "Invalid mesh counts");
        for (unsigned k = 0; k < vertices; ++k) {
            AssetVertex v;
            for (auto &x : v.position)
                x = number();
            for (auto &x : v.normal)
                x = number();
            for (auto &x : v.uv)
                x = number();
            auto raw = slice(bytes, pos, 8);
            pos += 8;
            std::copy_n(raw.begin(), 4, v.joints.begin());
            std::copy_n(raw.begin() + 4, 4, v.weights.begin());
            unsigned sum = 0;
            for (unsigned a = 0; a < 4; ++a) {
                sum += v.weights[a];
                require(v.joints[a] < palette, "Vertex joint is outside palette");
            }
            require(sum == 255, "Vertex weights must sum to 255");
            m.vertices.push_back(v);
        }
        for (unsigned k = 0; k < indices; ++k) {
            auto index = u16(bytes, pos);
            pos += 2;
            require(index < vertices, "Index exceeds vertex count");
            m.indices.push_back(index);
        }
        out.meshes.push_back(std::move(m));
    }
    auto tracks = count(255);
    require(tracks > 0, "Asset has no motion");
    names.clear();
    for (unsigned i = 0; i < tracks; ++i) {
        JointMotion track;
        track.joint = string();
        require(names.insert(track.joint).second &&
                    std::any_of(out.joints.begin(), out.joints.end(),
                                [&](auto &j) {
                                    return j.name == track.joint;
                                }),
                "Invalid motion joint");
        auto frames = count(65535);
        require(frames > 0, "Empty motion duration");
        track.frames = static_cast<std::uint16_t>(frames);
        for (unsigned c = 0; c < 9; ++c) {
            auto keys = count(frames + 1);
            require(keys > 0, "Missing motion channel");
            for (unsigned k = 0; k < keys; ++k) {
                auto frame = count(frames);
                auto value = number(), slope = number();
                if (c < 3)
                    require(std::abs(value - 1) < 1e-5f && std::abs(slope) < 1e-5f,
                            "Asset motion profile requires unit scales");
                track.channels[c].push_back({static_cast<std::uint16_t>(frame), value, slope});
            }
        }
        track.write(false);
        out.tracks.push_back(std::move(track));
    }
    require(pos == bytes.size(), "Unaccounted asset bytes");
    return out;
}
Bytes SkeletonMotion::write() const {
    require(frames > 0 && !tracks.empty() && tracks.size() <= 255, "Invalid skeleton motion");
    Bytes skeleton(8);
    put32(skeleton, 0, narrow(tracks.size()));
    std::set<std::string> names;
    for (auto &track : tracks) {
        require(track.frames == frames && names.insert(track.joint).second,
                "Motion duration or duplicate track mismatch");
        skeleton.push_back(static_cast<std::uint8_t>(track.joint.size()));
        append(skeleton, View(reinterpret_cast<const std::uint8_t *>(track.joint.data()),
                              track.joint.size()));
    }
    skeleton.resize(aligned(skeleton.size(), 4));
    put32(skeleton, 4, narrow(skeleton.size() - 8));
    for (auto &track : tracks) {
        auto b = track.write(false);
        auto start = u32(b, 28);
        auto at = start + 8 + u32(b, start + 4);
        append(skeleton, slice(b, at, 8 + std::size_t(u32(b, at + 4))));
    }
    auto out = tracks.front().write(false);
    out.resize(68);
    put32(out, 24, narrow(skeleton.size()));
    append(out, skeleton);
    out.resize(aligned(out.size(), 128));
    return out;
}
SkeletonMotion SkeletonMotion::read(View bytes) {
    require(u32(bytes, 0) == 0x60000 && u32(bytes, 4) == 2 && u32(bytes, 8) == 0 &&
                u32(bytes, 20) == 1,
            "Unsupported skeletal motion sections");
    auto top = slice(bytes, u32(bytes, 16), u32(bytes, 12));
    require(top.size() == 36, "Unsupported motion metadata");
    auto skeleton = slice(bytes, u32(bytes, 28), u32(bytes, 24));
    auto count = u32(skeleton, 0);
    require(count > 0 && count <= 255, "Invalid motion track count");
    std::size_t pos = 8;
    std::vector<std::string> names;
    for (unsigned i = 0; i < count; ++i) {
        auto n = slice(skeleton, pos++, 1)[0];
        auto s = text(slice(skeleton, pos, n));
        require(n > 0 && n == s.size(), "Invalid joint name");
        pos += n;
        names.push_back(s);
    }
    auto curves = 8 + std::size_t(u32(skeleton, 4));
    require(pos <= curves, "Motion name table overlaps curves");
    SkeletonMotion result;
    for (auto &name : names) {
        auto length = 8 + std::size_t(u32(skeleton, curves + 4));
        auto track = slice(skeleton, curves, length);
        curves += length;
        Bytes single(8);
        put32(single, 0, 1);
        single.push_back(static_cast<std::uint8_t>(name.size()));
        append(single, View(reinterpret_cast<const std::uint8_t *>(name.data()), name.size()));
        single.resize(aligned(single.size(), 4));
        put32(single, 4, narrow(single.size() - 8));
        append(single, track);
        Bytes file(32);
        put32(file, 0, 0x60000);
        put32(file, 4, 2);
        put32(file, 8, 0);
        put32(file, 12, 36);
        put32(file, 16, 32);
        put32(file, 20, 1);
        put32(file, 24, narrow(single.size()));
        put32(file, 28, 68);
        append(file, top);
        append(file, single);
        auto decoded = JointMotion::read(file);
        result.frames = decoded.frames;
        result.tracks.push_back(decoded);
    }
    require(curves == skeleton.size(), "Unaccounted skeletal motion bytes");
    result.write();
    return result;
}
}
