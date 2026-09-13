#pragma once
#include "assets/refresh_feeding.h"
namespace studio {
struct RefreshFeedingArea {
    std::array<float, 2> position{80, 140}, size{160, 100};
    bool contains(float x, float y) const {
        return x > position[0] && x < position[0] + size[0] && y > position[1] &&
               y < position[1] + size[1];
    }
};
enum class FeedingStage { Ready, Approach, Start, Bite, Finish, Complete, Dropped, Refused };
struct RefreshFeedingSession {
    FeedingStage stage = FeedingStage::Ready;
    double seconds = 0;
    unsigned bites = 0;
    int fullness = 0;
    static bool in_range(float x, float y) {
        return RefreshFeedingArea{}.contains(x, y);
    }
    unsigned motion_slot() const;
    const char *status() const;
    void reset(int initial_fullness = 0);
    void advance(double elapsed, bool held, bool inside, unsigned loops, double motion_duration);
};
}
