#pragma once
#include "core/binary.h"
#include <array>
namespace studio {
struct ModelResource {
    std::size_t category, index;
    std::string name;
    Bytes bytes;
    std::size_t address_field;
};
struct ModelPack {
    Bytes original;
    std::vector<ModelResource> resources;
    static ModelPack parse(View bytes);
    Bytes replace(std::size_t resource, View replacement, std::size_t alignment = 16) const;
};
struct ModelSection {
    std::string kind;
    std::size_t offset, size;
};
struct Model {
    Bytes original;
    std::vector<ModelSection> sections;
    std::array<std::vector<std::string>, 4> names;
    std::size_t bounds_offset = 0;
    std::uint32_t bones = 0;
    static Model parse(View bytes);
};
struct CommandWrite {
    std::uint16_t reg;
    std::uint8_t mask;
    std::uint32_t value;
    std::size_t offset;
};
std::vector<CommandWrite> commands(View bytes);
struct VertexAttribute {
    unsigned semantic, format, elements;
    std::size_t offset;
};
struct VertexLayout {
    std::size_t stride;
    std::vector<VertexAttribute> attributes;
};
VertexLayout vertex_layout(View enable);
}
