#include "scene/material.h"
namespace studio {
CombinerSettings combiner_settings(const std::map<unsigned, std::uint32_t> &registers,
                                   const std::array<unsigned, 6> &assignments) {
    CombinerSettings out;
    out.assignments = assignments;
    auto get = [&](unsigned reg, unsigned fallback = 0) {
        auto it = registers.find(reg);
        return it == registers.end() ? fallback : it->second;
    };
    for (unsigned i = 0; i < 6; ++i) {
        unsigned base = i < 4 ? 0xc0 + i * 8 : 0xf0 + (i - 4) * 8;
        out.stages[i] = {get(base, 0x000f000f) & 0x0fff0fff, get(base + 1) & 0x00777fff,
                         get(base + 2) & 0x000f000f, get(base + 4) & 0x00030003};
    }
    out.buffer = get(0xfd);
    out.buffer_write = get(0xe0) & 0xff00;
    return out;
}
std::map<unsigned, std::uint32_t> combiner_registers(const CombinerSettings &settings) {
    std::map<unsigned, std::uint32_t> out;
    for (unsigned i = 0; i < 6; ++i) {
        unsigned base = i < 4 ? 0xc0 + i * 8 : 0xf0 + (i - 4) * 8;
        for (unsigned j = 0; j < 4; ++j)
            out[base + (j == 3 ? 4 : j)] = settings.stages[i][j];
    }
    out[0xfd] = settings.buffer;
    out[0xe0] = settings.buffer_write;
    return out;
}
MaterialBump decode_bump(const std::map<unsigned, std::uint32_t> &registers, int texture) {
    MaterialBump out;
    if (auto it = registers.find(0x1c3); it != registers.end()) {
        out.mode = (it->second >> 28) & 3;
        out.texture = (it->second >> 22) & 3;
        out.reconstruct_z = (it->second & (1u << 30)) == 0;
    } else if (texture >= 0) {
        out.mode = 1;
        out.texture = unsigned(texture);
    }
    if (out.mode > 1)
        out.unsupported = "Tangent-map lighting is not yet supported";
    else if (out.mode && out.texture > 2)
        out.unsupported = "Invalid normal-map texture unit";
    return out;
}
MaterialColor material_color(std::uint32_t rgba) {
    MaterialColor c{};
    for (unsigned i = 0; i < 4; ++i)
        c[i] = float((rgba >> (8 * i)) & 255) / 255.f;
    return c;
}
MaterialCombiner decode_combiner(const std::map<unsigned, std::uint32_t> &registers) {
    MaterialCombiner out;
    auto get = [&](unsigned reg, std::uint32_t fallback = 0) {
        auto it = registers.find(reg);
        return it == registers.end() ? fallback : it->second;
    };
    out.buffer = material_color(get(0xfd));
    for (unsigned i = 0; i < 6; ++i) {
        unsigned base = i < 4 ? 0xc0 + i * 8 : 0xf0 + (i - 4) * 8;
        auto &s = out.stages[i];
        auto source = get(base, 0x000f000f), operand = get(base + 1), operation = get(base + 2),
             scale = get(base + 4);
        out.present |= registers.contains(base);
        s.operation = {float(operation & 15), float((operation >> 16) & 15),
                       float(1u << (scale & 3)), float(1u << ((scale >> 16) & 3))};
        s.constant = material_color(get(base + 3));
        if ((operation & 15) > 9 || ((operation >> 16) & 15) > 9 || (scale & 3) > 2 ||
            ((scale >> 16) & 3) > 2)
            out.unsupported = "Unknown texture-combiner operation";
        for (unsigned j = 0; j < 3; ++j) {
            unsigned rgb = (source >> (j * 4)) & 15, alpha = (source >> (16 + j * 4)) & 15;
            if (i == 0) {
                if (rgb == 15)
                    rgb = (source >> 8) & 15;
                if (alpha == 15)
                    alpha = (source >> 24) & 15;
            }
            s.color_sources[j] = float(rgb);
            s.alpha_sources[j] = float(alpha);
            auto cop = (operand >> (j * 4)) & 15;
            s.color_operands[j] = float(cop);
            s.alpha_operands[j] = float((operand >> (12 + j * 4)) & 7);
            unsigned nc = (operation & 15) == 0                            ? 1
                          : (operation & 15) == 4 || (operation & 15) >= 8 ? 3
                                                                           : 2;
            unsigned na = ((operation >> 16) & 15) == 0                                    ? 1
                          : ((operation >> 16) & 15) == 4 || ((operation >> 16) & 15) >= 8 ? 3
                                                                                           : 2;
            auto check = [&](unsigned src) {
                if (src == 1 || src == 2)
                    out.lighting = true;
                else if (src >= 6 && src <= 12)
                    out.unsupported = "Procedural or unknown texture input";
            };
            if (j < nc) {
                check(rgb);
                if (cop != 0 && cop != 1 && cop != 2 && cop != 3 && cop != 4 && cop != 5 &&
                    cop != 8 && cop != 9 && cop != 12 && cop != 13)
                    out.unsupported = "Unknown color operand";
            }
            if (j < na && (operation & 15) != 7)
                check(alpha);
        }
        if (i >= 1 && i <= 4) {
            s.buffer_write[0] = float((get(0xe0) >> (7 + i)) & 1);
            s.buffer_write[1] = float((get(0xe0) >> (11 + i)) & 1);
        }
    }
    return out;
}
MaterialShader decode_material_shader(View b) {
    require(text(slice(b, 16, 8)) == "shader", "Expected shader resource");
    auto length = u32(b, 112);
    auto stream = slice(b, 192, length);
    std::map<unsigned, std::uint32_t> registers;
    for (auto c : commands(stream)) {
        auto &value = registers[c.reg];
        for (unsigned lane = 0; lane < 4; ++lane)
            if (c.mask & (1u << lane)) {
                auto mask = 255u << (lane * 8);
                value = (value & ~mask) | (c.value & mask);
            }
    }
    return {text(slice(b, 32, 64)), decode_combiner(registers), std::move(registers)};
}
}
