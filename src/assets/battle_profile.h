#pragma once
#include <array>
namespace studio {
struct BattleCameraView {
    std::array<float, 3> eye, target;
    float fov = 30;
};
struct BattleProfile {
    static constexpr unsigned trainer_idle_slot = 4;
    static constexpr const char *effects_archive = "romfs/a/0/8/7";
    static constexpr const char *sequences_archive = "romfs/a/0/8/8";
    static constexpr unsigned single_sendout = 1074, single_sendout_alternate = 1077;
    static constexpr std::array<std::array<unsigned, 2>, 30> sendout_commands{
        {{4, 0},    {10, 8},   {20, 4},   {29, 4},   {41, 44}, {43, 72}, {45, 4},   {48, 4},
         {50, 20},  {54, 44},  {57, 16},  {70, 8},   {73, 44}, {76, 16}, {82, 84},  {88, 12},
         {103, 24}, {104, 24}, {110, 20}, {111, 16}, {119, 4}, {125, 4}, {126, 16}, {153, 8},
         {155, 8},  {159, 12}, {166, 8},  {168, 0},  {169, 4}, {191, 0}}};
    static BattleCameraView single_camera(unsigned near_size, unsigned far_size) {
        static constexpr std::array<std::array<std::array<float, 3>, 3>, 3> eyes{
            {{{{125, 50, 360}, {155, 56, 380}, {185, 62, 400}}},
             {{{236, 110, 460}, {256, 106, 465}, {276, 102, 470}}},
             {{{220, 150, 470}, {240, 140, 485}, {260, 130, 500}}}}};
        static constexpr std::array<std::array<std::array<float, 3>, 3>, 3> targets{
            {{{{-50, 50, -40}, {-45, 56, -40}, {-40, 62, -40}}},
             {{{-50, 60, -20}, {-45, 70, -20}, {-40, 80, -20}}},
             {{{0, 130, 90}, {0, 130, 90}, {0, 130, 90}}}}};
        return {eyes.at(near_size).at(far_size), targets.at(near_size).at(far_size), 30};
    }
};
}
