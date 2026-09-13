#pragma once
#include "compiler/attachment.h"
namespace studio::model_build {
std::uint32_t hash(const std::string &name);
void string(Bytes &bytes, const std::string &name);
void named(Bytes &bytes, const std::string &name);
void table(Bytes &bytes, const std::vector<std::string> &names);
Bytes section(const std::string &tag, View payload);
Bytes material(View old, const std::string &name, const std::string &texture);
Bytes mesh_names(View old, const std::string &name, const std::string &mat, std::uint8_t bone);
std::size_t skeleton_end(View tail);
Bytes metadata(const Model &m, const std::array<std::vector<std::string>, 4> &names, View tail);
Bytes assemble(View original, View meta, const std::vector<Bytes> &mats,
               const std::vector<Bytes> &meshes);
void bounds(Bytes &bytes, std::size_t pos, std::array<float, 3> low, std::array<float, 3> high,
            bool expand);
}
