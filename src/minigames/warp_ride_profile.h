#pragma once
#include <array>
#include <cstddef>
namespace studio {
struct WarpRideProfile {
    static constexpr const char *course_archive = "romfs/a/3/2/0";
    static constexpr const char *mount_archive = "romfs/a/2/0/0";
    static constexpr const char *ride_motions_archive = "romfs/a/3/1/9";
    struct Asset {
        const char *name;
        std::size_t model, loop;
    };
    static constexpr std::array<Asset, 19> assets{{{"Aura", 0, 1},
                                                   {"Curved course", 2, 3},
                                                   {"Gate aura", 5, 6},
                                                   {"Energy pickup", 7, 8},
                                                   {"Obstacle", 13, 14},
                                                   {"Wormhole tunnel", 11, 12},
                                                   {"Sky", 17, 18},
                                                   {"Straight course", 19, 20},
                                                   {"White wormhole", 22, 23},
                                                   {"Red wormhole", 26, 27},
                                                   {"Blue wormhole", 30, 31},
                                                   {"Yellow wormhole", 34, 35},
                                                   {"Green wormhole", 38, 39},
                                                   {"Solgaleo", 259, 72},
                                                   {"Lunala", 260, 90},
                                                   {"Boy on Solgaleo", 0, 0},
                                                   {"Girl on Solgaleo", 1, 36},
                                                   {"Boy on Lunala", 0, 18},
                                                   {"Girl on Lunala", 1, 54}}};
};
}
