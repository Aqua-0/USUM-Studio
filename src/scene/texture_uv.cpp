#include "scene/texture_uv.h"
#include <algorithm>
#include <cmath>
namespace studio {
std::vector<TextureUvSegment> texture_uv_segments(std::array<float, 2> start,
                                                  std::array<float, 2> end, unsigned wrap_u,
                                                  unsigned wrap_v) {
    std::array<unsigned, 2> wrap{wrap_u, wrap_v};
    std::vector<double> cuts{0, 1};
    for (unsigned axis = 0; axis < 2; ++axis) {
        if (!std::isfinite(start[axis]) || !std::isfinite(end[axis]))
            return {};
        double low = std::min(start[axis], end[axis]), high = std::max(start[axis], end[axis]);
        if (wrap[axis] >= 2) {
            if (high - low > 8192)
                return {};
            for (double boundary = std::floor(low) + 1; boundary < high; boundary += 1) {
                cuts.push_back((boundary - start[axis]) / (double(end[axis]) - start[axis]));
                if (boundary + 1 == boundary)
                    return {};
            }
        } else
            for (double boundary : {0., 1.})
                if (boundary > low && boundary < high)
                    cuts.push_back((boundary - start[axis]) / (double(end[axis]) - start[axis]));
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    std::vector<TextureUvSegment> result;
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        TextureUvSegment edge;
        bool visible = true;
        for (unsigned axis = 0; axis < 2; ++axis) {
            double delta = double(end[axis]) - start[axis],
                   mid = start[axis] + delta * (cuts[i - 1] + cuts[i]) * .5, tile = std::floor(mid);
            if (wrap[axis] == 1 && (mid < 0 || mid > 1))
                visible = false;
            for (unsigned p = 0; p < 2; ++p) {
                double value = start[axis] + delta * cuts[i - 1 + p];
                if (wrap[axis] >= 2) {
                    value -= tile;
                    if (wrap[axis] == 3 && std::fmod(std::abs(tile), 2.) == 1)
                        value = 1 - value;
                } else
                    value = std::clamp(value, 0., 1.);
                edge[p][axis] = float(value);
            }
        }
        if (visible && edge[0] != edge[1])
            result.push_back(edge);
    }
    return result;
}
}
