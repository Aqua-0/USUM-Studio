#pragma once
#include "formats/model.h"
namespace studio {
Bytes copy_model_material(View model, std::size_t material, const std::string &name);
Bytes remove_model_material(View model, std::size_t material, const std::string &replacement);
Bytes change_model_texture(View model, const std::string &name, const std::string &replacement,
                           bool add = false);
Bytes change_pack_texture(View pack, const std::string &name, View added = {});
}
