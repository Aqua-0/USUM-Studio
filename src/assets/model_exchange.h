#pragma once
#include "formats/skinning.h"
#include <map>
namespace studio {
struct ExchangeVertex {
    std::array<std::array<float, 4>, 7> channels{};
    std::array<std::uint16_t, 4> joints{};
    std::array<float, 4> weights{};
    bool operator==(const ExchangeVertex &) const = default;
};
struct ExchangeMesh {
    std::string name, texture;
    unsigned influences = 0;
    std::size_t source_mesh = std::size_t(-1);
    std::array<std::array<unsigned, 2>, 7> formats{};
    std::vector<ExchangeVertex> vertices;
    std::vector<std::uint16_t> indices;
};
struct ModelExchange {
    std::string source;
    std::vector<Joint> joints;
    std::vector<ExchangeMesh> meshes;
    bool standalone = false;
};
using MeshUvEdits = std::map<std::size_t, std::map<std::size_t, std::array<float, 2>>>;
Bytes replace_model_uvs(View bytes, unsigned channel, const MeshUvEdits &edits);
ModelExchange decode_model_exchange(View bytes);
Bytes replace_model_exchange(View bytes, const ModelExchange &replacement, bool keep_bones = true);
ModelExchange bind_new_model(const ModelExchange &model, const ModelExchange &target,
                             const std::vector<std::size_t> &materials);
std::string serialize_model_exchange(const ModelExchange &exchange);
ModelExchange parse_model_exchange(const std::string &text);
}
