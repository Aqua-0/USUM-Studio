#pragma once
#include "core/binary.h"
namespace studio {
struct TrainerPokemon {
    unsigned species = 0, form = 0, level = 0;
};
struct TrainerBattleEntry {
    unsigned id = 0, category = 0, mode = 0;
    std::string name, error;
    std::vector<TrainerPokemon> team;
};
TrainerBattleEntry decode_trainer_battle(unsigned id, View record, View team);
std::vector<TrainerBattleEntry> load_trainer_battles(const std::filesystem::path &source);
TrainerBattleEntry load_trainer_battle(const std::filesystem::path &source, unsigned id);
}
