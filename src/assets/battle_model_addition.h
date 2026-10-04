#pragma once
#include "assets/model_library.h"
namespace studio {
struct BattleModelAddition {
    ModelCategory category = ModelCategory::BattleCharacters;
    unsigned donor = 0, member = 0;
    std::string source_identity, name;
    std::string serialize() const;
    static BattleModelAddition parse(const std::string &text);
    bool operator==(const BattleModelAddition &) const = default;
};
BattleModelAddition plan_battle_model_addition(const Archive &archive, unsigned donor,
                                               ModelCategory category);
void export_battle_model_addition(const Archive &archive, const BattleModelAddition &plan,
                                  const std::filesystem::path &output);
}
