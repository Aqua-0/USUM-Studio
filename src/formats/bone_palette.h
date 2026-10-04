#pragma once
#include "core/binary.h"
#include <array>
#include <map>
#include <numeric>
#include <set>
namespace studio {
inline constexpr unsigned max_draw_bones = 20;
inline constexpr unsigned bone_palette_capacity = 31;
struct BonePaletteDraw {
    std::vector<unsigned> vertices;
    std::vector<std::uint16_t> indices;
};
// Keep triangle order so splitting does not reorder transparent surfaces.
template <class Bones>
std::vector<BonePaletteDraw> partition_bone_palettes(
    std::size_t vertex_count, const std::vector<std::uint16_t> &indices, Bones bones) {
    require(vertex_count > 0 && vertex_count <= 65536 && !indices.empty() &&
                indices.size() % 3 == 0,
            "Bone palette splitting needs vertices and a triangle list");
    std::set<unsigned> all;
    for (unsigned v = 0; v < vertex_count; ++v)
        for (auto bone : bones(v)) all.insert(bone);
    for (auto index : indices)
        require(index < vertex_count, "Triangle references a missing vertex");
    if (all.size() <= max_draw_bones) {
        BonePaletteDraw draw;
        draw.vertices.resize(vertex_count);
        std::iota(draw.vertices.begin(), draw.vertices.end(), 0u);
        draw.indices = indices;
        return {std::move(draw)};
    }
    std::vector<BonePaletteDraw> draws(1);
    std::set<unsigned> palette;
    std::map<unsigned, std::uint16_t> remap;
    for (std::size_t t = 0; t < indices.size(); t += 3) {
        std::set<unsigned> triangle;
        for (unsigned k = 0; k < 3; ++k)
            for (auto bone : bones(indices[t + k])) triangle.insert(bone);
        require(triangle.size() <= max_draw_bones,
                "A triangle needs more than 20 bones; reduce its vertex influences");
        auto combined = palette;
        combined.insert(triangle.begin(), triangle.end());
        if (combined.size() > max_draw_bones) {
            draws.emplace_back();
            palette.clear();
            remap.clear();
        }
        palette.insert(triangle.begin(), triangle.end());
        auto &draw = draws.back();
        for (unsigned k = 0; k < 3; ++k) {
            auto vertex = indices[t + k];
            auto [it, added] = remap.emplace(vertex, std::uint16_t(draw.vertices.size()));
            if (added) draw.vertices.push_back(vertex);
            draw.indices.push_back(it->second);
        }
    }
    return draws;
}
}
