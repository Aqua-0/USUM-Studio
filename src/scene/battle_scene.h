#pragma once
#include "scene/environment.h"
#include "assets/pokemon_settings.h"
namespace studio {
struct BattleAsset {
    unsigned member = 0;
    std::string name;
};
std::vector<BattleAsset> load_battle_catalog(const std::filesystem::path &dump,
                                             bool trainers = false);
Environment load_battle_stage(const std::filesystem::path &dump,
                              const std::vector<unsigned> &arenas, int trainer);
struct BattleModelRange {
    std::size_t draws = 0, materials = 0, skeletons = 0, tables = 0, material_motions = 0,
                visibility = 0, shadow_draws = 0, shadow_count = 0, shadow_materials = 0;
};
BattleModelRange append_battle_pokemon(Environment &stage, const Environment &pokemon,
                                       const Environment *shadow = nullptr);
void update_battle_pokemon(Environment &stage, const Environment &pokemon,
                           const BattleModelRange &range, const PokemonSettings &settings,
                           bool shiny, bool far_side = false);
void update_battle_shadow(Environment &stage, const BattleModelRange &range, bool far_side,
                          float elevation = 50, float azimuth = 40);
}
