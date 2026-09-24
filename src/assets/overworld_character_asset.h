#pragma once
#include "assets/asset_package.h"
namespace studio {
struct OverworldMotionSelection {
    int idle = -1, walk = -1, run = -1;
};
bool can_convert_overworld_character(const ModelDocument &model);
OverworldMotionSelection default_overworld_motions(const ModelDocument &model);
Bytes convert_overworld_character(const MaterialDocument &document, View behavior_donor,
                                const OverworldMotionSelection &motions, unsigned scale_percent);
ModelDocument open_independent_asset(const AssetPackage &package,
                                     const std::filesystem::path &dump);
}
