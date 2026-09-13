#pragma once
#include <array>
#include <span>
#include <string>
#include <string_view>
namespace studio {
inline constexpr std::array<unsigned, 4> pokemon_main_motion_counts{18, 26, 13, 57};
inline std::string pokemon_motion_label(unsigned group, unsigned slot) {
    static constexpr std::array<std::string_view, 18> battle{"Idle A",
                                                             "Idle B",
                                                             "Idle C",
                                                             "Entrance: jump out",
                                                             "Entrance: falling loop",
                                                             "Entrance: landing",
                                                             "Entrance: alternate",
                                                             "Mega Evolution appeal",
                                                             "Physical attack 1",
                                                             "Physical attack 2",
                                                             "Physical attack 3",
                                                             "Physical attack 4",
                                                             "Special attack 1",
                                                             "Special attack 2",
                                                             "Special attack 3",
                                                             "Special attack 4",
                                                             "Taking damage",
                                                             "Fainting"};
    static constexpr std::array<std::string_view, 26> refresh{"Idle",
                                                              "Responding",
                                                              "Turning A",
                                                              "Turning B",
                                                              "Drowsing: start",
                                                              "Drowsing: loop",
                                                              "Drowsing: wake up",
                                                              "Sleeping: loop",
                                                              "Sleeping: wake up",
                                                              "Dislike",
                                                              "Confused reaction",
                                                              "Happy A",
                                                              "Happy B",
                                                              "Happy C",
                                                              "Mannerism A",
                                                              "Mannerism B",
                                                              "Mannerism C",
                                                              "Mannerism D",
                                                              "Lonely",
                                                              "Seeking attention A",
                                                              "Seeking attention B",
                                                              "Angry",
                                                              "Eating: start",
                                                              "Eating: loop",
                                                              "Eating: finish",
                                                              "Touch reaction"};
    static constexpr std::array<std::string_view, 13> field{
        "Idle A",       "Idle B (random variation)", "Walking",
        "Running",      "Idle / walk / idle cycle",  "Idle to walk",
        "Walk to idle", "Idle / run / idle cycle",   "Idle to run",
        "Run to idle",  "Walk / run / walk cycle",   "Walk to run",
        "Run to walk"};
    static constexpr std::array<std::string_view, 10> overlays{
        "Eye expression 1",   "Eye expression 2",   "Eye expression 3",  "Mouth expression 1",
        "Mouth expression 2", "Mouth expression 3", "Looping overlay 1", "Looping overlay 2",
        "Looping overlay 3",  "Looping overlay 4"};
    std::span<const std::string_view> roles;
    std::string prefix;
    unsigned index = slot, count = 0;
    if (group == 0)
        roles = battle;
    else if (group == 1)
        roles = refresh;
    else if (group == 2)
        roles = field;
    else if (group == 3) {
        count = unsigned(battle.size() + refresh.size() + field.size());
        if (index < battle.size()) {
            roles = battle;
            prefix = "Battle: ";
        } else if (index < battle.size() + refresh.size()) {
            index -= unsigned(battle.size());
            roles = refresh;
            prefix = "Refresh: ";
        } else {
            index -= unsigned(battle.size() + refresh.size());
            roles = field;
            prefix = "Field: ";
        }
    }
    if (group < 3)
        count = unsigned(roles.size());
    std::string label = "Unmapped motion";
    if (index < roles.size())
        label = prefix + std::string(roles[index]);
    else if (group < 4 && slot > count && slot - count - 1 < overlays.size())
        label = overlays[slot - count - 1];
    return label + " (slot " + std::to_string(slot) + ")";
}
}
