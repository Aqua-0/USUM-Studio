#pragma once
#include "scene/environment.h"
#include "scene/texture_uv.h"
#include <imgui.h>
namespace studio {
inline void draw_texture_uvs(const Environment &scene, std::size_t material,
                             const SceneTexture &input, bool transformed, ImVec2 origin,
                             float size) {
    auto *painter = ImGui::GetWindowDrawList();
    painter->PushClipRect(origin, {origin.x + size, origin.y + size}, true);
    auto point = [&](const SceneVertex &v) {
        float u = input.source == 0   ? v.u
                  : input.source == 1 ? v.u1
                                      : v.u2,
              w = input.source == 0   ? v.v
                  : input.source == 1 ? v.v1
                                      : v.v2;
        if (transformed) {
            auto a = u;
            u = input.row_u[0] * u + input.row_u[1] * w + input.row_u[2];
            w = input.row_v[0] * a + input.row_v[1] * w + input.row_v[2];
        }
        return std::array<float, 2>{u, w};
    };
    for (unsigned i = 0; i < scene.draws.size(); ++i) {
        auto &draw = scene.draws[i];
        if (draw.material != std::size_t(material))
            continue;
        for (unsigned j = 0; j + 2 < draw.indices.size(); j += 3) {
            for (unsigned k = 0; k < 3; ++k)
                for (auto &edge :
                     texture_uv_segments(point(draw.vertices[draw.indices[j + k]]),
                                         point(draw.vertices[draw.indices[j + (k + 1) % 3]]),
                                         input.wrap_u, input.wrap_v)) {
                    ImVec2 a{origin.x + edge[0][0] * size, origin.y + edge[0][1] * size},
                        b{origin.x + edge[1][0] * size, origin.y + edge[1][1] * size};
                    painter->AddLine(a, b, IM_COL32(0, 0, 0, 210), 2);
                    painter->AddLine(a, b, IM_COL32(90, 240, 255, 230), 1);
                }
        }
    }
    painter->PopClipRect();
}
}
