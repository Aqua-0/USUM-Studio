#include "field/area.h"
#include <cmath>
namespace studio {
std::vector<Placement> read_placements(View b) {
    if (b.empty())
        return {};
    auto count = u32(b, 0);
    require(count < 4096, "Too many static placement records");
    auto end = 4 + std::size_t(count) * 56;
    slice(b, 0, end);
    std::vector<Placement> out;
    for (std::size_t i = 0; i < count; ++i) {
        auto off = 4 + i * 56;
        require(u32(b, off) == 4, "Unexpected static placement type");
        Placement p;
        p.version = u32(b, off + 32);
        p.flags = u32(b, off + 36);
        p.condition = u32(b, off + 40);
        p.event = u32(b, off + 44);
        p.model = u16(b, off + 48);
        p.alias = u16(b, off + 50);
        p.collision = u32(b, off + 52);
        for (std::size_t j = 0; j < 3; ++j) {
            p.position[j] = f32(b, off + 4 + 4 * j);
            require(std::isfinite(p.position[j]), "Non-finite placement position");
        }
        float norm = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            p.rotation[j] = f32(b, off + 16 + 4 * j);
            norm += p.rotation[j] * p.rotation[j];
        }
        require(std::isfinite(norm) && norm > 0.9f && norm < 1.1f, "Invalid placement quaternion");
        require(!p.collision || (p.collision >= end && p.collision <= b.size() - 4),
                "Invalid placement collision offset");
        out.push_back(p);
    }
    return out;
}
}
