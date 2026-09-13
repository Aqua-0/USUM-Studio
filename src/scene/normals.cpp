#include "scene/environment.h"
#include <cmath>
namespace studio {
void complete_normals(SceneDraw &draw) {
    std::vector<std::array<float, 3>> sums(draw.vertices.size());
    require(draw.indices.size() % 3 == 0, "Normal generation requires triangles");
    for (std::size_t i = 0; i < draw.indices.size(); i += 3) {
        auto a = draw.indices[i], b = draw.indices[i + 1], c = draw.indices[i + 2];
        require(a < sums.size() && b < sums.size() && c < sums.size(),
                "Normal generation index out of range");
        auto &x = draw.vertices[a];
        auto &y = draw.vertices[b];
        auto &z = draw.vertices[c];
        float ux = y.x - x.x, uy = y.y - x.y, uz = y.z - x.z, vx = z.x - x.x, vy = z.y - x.y,
              vz = z.z - x.z;
        std::array<float, 3> n{uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
        for (auto j : {a, b, c})
            for (unsigned k = 0; k < 3; ++k)
                sums[j][k] += n[k];
    }
    for (std::size_t i = 0; i < draw.vertices.size(); ++i) {
        auto &v = draw.vertices[i];
        float length = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        if (length < .000001f) {
            v.nx = sums[i][0];
            v.ny = sums[i][1];
            v.nz = sums[i][2];
            length = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        }
        if (length < .000001f) {
            v.nx = 0;
            v.ny = 1;
            v.nz = 0;
        } else {
            v.nx /= length;
            v.ny /= length;
            v.nz /= length;
        }
    }
}
}
