#pragma once
#include "formats/amx.h"
#include <optional>
namespace studio {
struct InteractionLinkResult {
    Bytes program;
    unsigned handler = 0;
    std::vector<unsigned> added_natives;
};
struct TrainerInteractionTarget {
    unsigned zone, event;
};
InteractionLinkResult
link_authored_interaction(View original, View compiled, unsigned script,
                          std::optional<TrainerInteractionTarget> trainer = {});
}
