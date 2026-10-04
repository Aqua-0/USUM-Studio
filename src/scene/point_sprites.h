#pragma once
#include "scene/environment.h"
namespace studio {
inline std::vector<SceneVertex> point_sprite_vertices(const std::vector<SceneVertex> &source) {
    std::vector<SceneVertex> vertices;
    vertices.reserve(source.size() * 4);
    for (const auto &center : source)
        for (unsigned corner = 0; corner < 4; ++corner) {
            auto vertex = center;
            vertex.u = float(corner & 1);
            vertex.v = float(corner >> 1);
            vertices.push_back(vertex);
        }
    return vertices;
}
inline std::vector<std::uint32_t> point_sprite_indices(const std::vector<std::uint16_t> &source,
                                                       bool edges = false) {
    std::vector<std::uint32_t> indices;
    indices.reserve(source.size() * (edges ? 8 : 6));
    for (auto index : source) {
        auto base = std::uint32_t(index) * 4;
        if (edges)
            for (auto corner : {0u, 1u, 1u, 3u, 3u, 2u, 2u, 0u})
                indices.push_back(base + corner);
        else
            for (auto corner : {0u, 1u, 2u, 2u, 1u, 3u})
                indices.push_back(base + corner);
    }
    return indices;
}
}
