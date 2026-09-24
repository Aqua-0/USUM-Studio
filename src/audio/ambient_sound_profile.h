#pragma once
namespace studio {
struct AmbientSoundRange {
    unsigned group, first, count, sound;
};
inline constexpr AmbientSoundRange ambient_sound_ranges[] = {
    {9, 0, 144, 1519}, {9, 144, 476, 1664}, {9, 620, 1, 1663}, {9, 621, 144, 2140},
    {15, 0, 1, 280},   {15, 1, 200, 2284},  {15, 201, 1, 281}, {15, 202, 225, 2484}};
}
