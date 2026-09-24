#pragma once
#include "scene/environment.h"
#include <cstring>
#include <algorithm>
#include <limits>
namespace studio {
inline bool same_texture_pixels(const TextureImage &a, const TextureImage &b) {
    return a.width == b.width && a.height == b.height && a.rgba == b.rgba && a.mipmaps == b.mipmaps;
}
inline std::vector<std::size_t> reusable_scene_draws(const Environment &old_scene,
                                                     const Environment &next) {
    const auto hash = [](const SceneDraw &draw) {
        std::uint64_t result = 14695981039346656037ull;
        const auto add = [&](const void *data, std::size_t size) {
            const auto *bytes = static_cast<const unsigned char *>(data);
            for (std::size_t i = 0; i < size; ++i)
                result = (result ^ bytes[i]) * 1099511628211ull;
        };
        add(draw.vertices.data(), draw.vertices.size() * sizeof(SceneVertex));
        add(draw.indices.data(), draw.indices.size() * sizeof(std::uint16_t));
        return result;
    };
    const auto equal = [](const SceneDraw &a, const SceneDraw &b) {
        return a.indices == b.indices && a.vertices.size() == b.vertices.size() &&
               (a.vertices.empty() || std::memcmp(a.vertices.data(), b.vertices.data(),
                                                  a.vertices.size() * sizeof(SceneVertex)) == 0);
    };
    std::vector<bool> used(old_scene.draws.size());
    std::vector<std::size_t> result(next.draws.size(), std::numeric_limits<std::size_t>::max());
    for (std::size_t i = 0; i < std::min(old_scene.draws.size(), next.draws.size()); ++i)
        if (equal(old_scene.draws[i], next.draws[i])) {
            result[i] = i;
            used[i] = true;
        }
    std::multimap<std::uint64_t, std::size_t> candidates;
    for (std::size_t i = 0; i < old_scene.draws.size(); ++i)
        if (!used[i])
            candidates.emplace(hash(old_scene.draws[i]), i);
    for (std::size_t i = 0; i < next.draws.size(); ++i) {
        if (result[i] != std::numeric_limits<std::size_t>::max())
            continue;
        auto [begin, end] = candidates.equal_range(hash(next.draws[i]));
        for (auto it = begin; it != end; ++it)
            if (equal(next.draws[i], old_scene.draws[it->second])) {
                result[i] = it->second;
                candidates.erase(it);
                break;
            }
    }
    return result;
}
}
