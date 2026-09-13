#pragma once
#include "compiler/skinned.h"
#include "compiler/box.h"
#include "formats/texture.h"
namespace studio {
struct GeneratedSkin {
    Bytes model, pack;
    std::string preview_obj;
};
Bytes pack_resources(const std::vector<ModelResource> &resources);
}
