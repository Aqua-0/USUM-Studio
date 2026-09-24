#pragma once
#include "formats/amx.h"
#include <set>
#include <optional>
namespace studio {
struct NpcBattlePrompt {
    unsigned encounter, yes_message, no_message;
};
std::set<unsigned> zone_script_ids(View program);
Bytes append_npc_dialogue_script(View program, unsigned script, unsigned event,
                                 const std::vector<unsigned> &messages,
                                 std::optional<NpcBattlePrompt> battle = std::nullopt);
struct DialogueText;
Bytes append_dialogue_message(View messages, const DialogueText &text);
Bytes append_dialogue_message(View messages, const std::string &text, bool formatted = false);
}
