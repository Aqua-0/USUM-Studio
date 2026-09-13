#pragma once
#include "field/area.h"
#include "compiler/skinned.h"
namespace studio {
struct CharacterAsset {
    std::size_t member, actor, model_resource;
    Container area, character, models;
    ModelPack pack;
    SkinnedModel skin;
    static CharacterAsset read(const Archive &archive, std::size_t area, const std::string &asset);
    Bytes replace_pack(View replacement) const;
    Bytes rebuild(const std::string &joint, std::array<float, 3> offset,
                  bool descendants = true) const;
};
}
