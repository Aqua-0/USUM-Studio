#pragma once
#include "formats/model.h"
namespace studio {
using Matrix = std::array<float, 16>;
struct Joint {
    std::string name, parent_name;
    int parent = -1;
    std::uint8_t flags = 0;
    std::array<float, 3> scale{}, rotation{}, translation{};
    Matrix bind{}, inverse_bind{};
    std::size_t transform_offset = 0;
};
struct SkinVertex {
    std::array<float, 3> position{}, bind_position{}, normal{}, tangent{};
    std::array<float, 2> uv{};
    std::array<std::uint16_t, 4> joints{};
    std::array<float, 4> weights{};
    std::size_t position_offset = 0, normal_offset = 0, tangent_offset = 0, index_offset = 0,
                weight_offset = 0;
    unsigned normal_format = 0, normal_elements = 0, tangent_format = 0, tangent_elements = 0,
             index_elements = 0, weight_elements = 0;
    bool rigid = false;
};
struct SkinMesh {
    std::string name;
    std::vector<std::uint8_t> palette;
    std::vector<SkinVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::size_t section_offset = 0, palette_offset = 0;
    unsigned influences = 0;
    std::array<std::size_t, 3> fixed_indices{}, fixed_weights{};
};
struct SkinnedModel {
    Model model;
    std::vector<Joint> joints;
    std::vector<SkinMesh> meshes;
    static SkinnedModel parse(View bytes);
    Bytes rebuild(const std::string &joint = "", std::array<float, 3> offset = {},
                  bool descendants = true) const;
    std::string report() const;
    std::string json() const;
};
}
