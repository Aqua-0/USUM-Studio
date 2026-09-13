#pragma once
#include "scene/skeleton.h"
#include "formats/texture.h"
#include <map>
namespace studio {
struct PlayerPart {
    std::string name, attachment;
    Bytes model;
    std::array<SkeletalMotion, 3> motions;
    std::map<std::string, TextureImage> textures;
};
struct PlayerAssets {
    std::array<SkeletalMotion, 3> motions;
    std::vector<PlayerPart> parts;
};
PlayerAssets load_player_assets(const std::filesystem::path &dump, unsigned appearance);
}
