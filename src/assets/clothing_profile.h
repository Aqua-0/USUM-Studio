#pragma once
#include "field/area.h"
namespace studio {
struct ClothingProfile {
    static constexpr unsigned parts = 12;
    static constexpr std::array<const char *, parts> names{
        "Face", "Hair", "Eyewear", "Hair accessories", "Bag", "Bracelet", "Bottoms",
        "Hat",  "Legs", "Shoes",   "Special outfits",  "Top"};
    static constexpr std::array<std::array<const char *, parts>, 4> archives{
        {{{"romfs/a/2/0/2", "romfs/a/2/0/3", "romfs/a/2/0/4", "", "romfs/a/2/0/5", "romfs/a/2/0/6",
           "romfs/a/2/0/7", "romfs/a/2/0/8", "romfs/a/2/0/9", "romfs/a/2/1/0", "romfs/a/2/1/1",
           "romfs/a/2/1/2"}},
         {{"romfs/a/2/1/4", "romfs/a/2/1/5", "romfs/a/2/1/6", "romfs/a/2/1/7", "romfs/a/2/1/8",
           "romfs/a/2/1/9", "romfs/a/2/2/0", "romfs/a/2/2/1", "romfs/a/2/2/2", "romfs/a/2/2/3",
           "romfs/a/2/2/4", "romfs/a/2/2/5"}},
         {{"romfs/a/1/7/6", "romfs/a/1/7/7", "romfs/a/1/7/8", "", "romfs/a/1/7/9", "romfs/a/1/8/0",
           "romfs/a/1/8/1", "romfs/a/1/8/2", "romfs/a/1/8/3", "romfs/a/1/8/4", "romfs/a/1/8/5",
           "romfs/a/1/8/6"}},
         {{"romfs/a/1/8/8", "romfs/a/1/8/9", "romfs/a/1/9/0", "romfs/a/1/9/1", "romfs/a/1/9/2",
           "romfs/a/1/9/3", "romfs/a/1/9/4", "romfs/a/1/9/5", "romfs/a/1/9/6", "romfs/a/1/9/7",
           "romfs/a/1/9/8", "romfs/a/1/9/9"}}}};
    static constexpr std::array<const char *, 4> colors{"romfs/a/2/0/1", "romfs/a/2/1/3",
                                                        "romfs/a/1/7/5", "romfs/a/1/8/7"};
    static constexpr std::array<std::array<int, parts>, 2> defaults{
        {{{0, 6, -1, -1, 109, 0, 105, 58, 37, 95, -1, 256}},
         {{0, 25, -1, -1, 145, 0, 249, 58, 0, 167, -1, 394}}}};
    static constexpr unsigned variants(unsigned part) {
        return part == 0 || part == 1 || part == 8 ? 2 : 1;
    }
};
}
