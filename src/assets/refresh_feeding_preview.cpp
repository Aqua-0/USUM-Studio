#include "assets/refresh_feeding_preview.h"
#include <algorithm>
#include <cmath>
namespace studio {
unsigned RefreshFeedingSession::motion_slot() const {
    switch (stage) {
    case FeedingStage::Start:
        return 22;
    case FeedingStage::Bite:
        return 23;
    case FeedingStage::Finish:
        return 24;
    default:
        return 0;
    }
}
const char *RefreshFeedingSession::status() const {
    switch (stage) {
    case FeedingStage::Ready:
        return "Pick up the bean";
    case FeedingStage::Approach:
        return "Hold inside the feeding area";
    case FeedingStage::Start:
        return "Eating: start";
    case FeedingStage::Bite:
        return "Eating: bite loop";
    case FeedingStage::Finish:
        return "Eating: finish";
    case FeedingStage::Complete:
        return "Bean eaten";
    case FeedingStage::Dropped:
        return "Bean dropped - try another";
    case FeedingStage::Refused:
        return "Full - feeding refused";
    }
    return "";
}
void RefreshFeedingSession::reset(int initial_fullness) {
    *this = {};
    fullness = std::clamp(initial_fullness, 0, 255);
}
void RefreshFeedingSession::advance(double elapsed, bool held, bool inside, unsigned loops,
                                    double duration) {
    require(loops >= 1 && loops <= 255, "Invalid feeding loop count");
    require(std::isfinite(elapsed) && elapsed >= 0 && std::isfinite(duration) && duration > 0,
            "Invalid feeding preview time");
    auto change = [&](FeedingStage next) {
        stage = next;
        seconds = 0;
    };
    if (stage == FeedingStage::Ready) {
        seconds += elapsed;
        if (held)
            change(FeedingStage::Approach);
        return;
    }
    if (stage == FeedingStage::Complete || stage == FeedingStage::Dropped ||
        stage == FeedingStage::Refused)
        return;
    if (stage != FeedingStage::Finish && !held) {
        change(FeedingStage::Dropped);
        return;
    }
    if (stage == FeedingStage::Approach) {
        seconds += elapsed;
        if (seconds >= 1) {
            if (fullness >= 255)
                change(FeedingStage::Refused);
            else if (inside)
                change(FeedingStage::Start);
        }
        return;
    }
    if (stage != FeedingStage::Finish && !inside) {
        change(FeedingStage::Approach);
        return;
    }
    seconds += elapsed;
    if (seconds < duration)
        return;
    if (stage == FeedingStage::Start)
        change(FeedingStage::Bite);
    else if (stage == FeedingStage::Bite) {
        ++bites;
        fullness = std::min(255, fullness + 102 / int(loops));
        change(bites >= loops ? FeedingStage::Finish : FeedingStage::Bite);
    } else if (stage == FeedingStage::Finish)
        change(FeedingStage::Complete);
}
}
