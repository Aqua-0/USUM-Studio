#pragma once
#include "formats/model.h"
#include <map>
#include <optional>
namespace studio {
using MaterialColor = std::array<float, 4>;
struct CombinerStage {
    std::array<float, 4> color_sources{}, alpha_sources{}, color_operands{}, alpha_operands{};
    std::array<float, 4> operation{0, 0, 1, 1};
    MaterialColor constant{};
    std::array<float, 4> buffer_write{};
};
struct MaterialCombiner {
    std::array<CombinerStage, 6> stages{};
    MaterialColor buffer{};
    bool present = false, lighting = false;
    std::string unsupported;
};
struct CombinerSettings {
    std::array<std::array<std::uint32_t, 4>, 6> stages{};
    std::array<unsigned, 6> assignments{};
    std::uint32_t buffer = 0, buffer_write = 0;
    bool operator==(const CombinerSettings &) const = default;
};
CombinerSettings combiner_settings(const std::map<unsigned, std::uint32_t> &registers,
                                   const std::array<unsigned, 6> &assignments);
std::map<unsigned, std::uint32_t> combiner_registers(const CombinerSettings &settings);
struct MaterialBump {
    unsigned mode = 0, texture = 0;
    bool reconstruct_z = false;
    std::string unsupported;
};
MaterialBump decode_bump(const std::map<unsigned, std::uint32_t> &registers, int texture);
struct MaterialShader {
    std::string name;
    MaterialCombiner combiner;
    std::map<unsigned, std::uint32_t> registers;
};
MaterialColor material_color(std::uint32_t rgba);
MaterialCombiner decode_combiner(const std::map<unsigned, std::uint32_t> &registers);
MaterialShader decode_material_shader(View bytes);
}
