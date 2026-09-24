#pragma once
#include "core/binary.h"
#include <map>
#include <array>
#include <optional>
#include <set>
#include "core/game_profile.h"
namespace studio {
enum class ConversationAction {
    Begin,
    Message,
    Choice,
    Sound,
    Wait,
    Repeat,
    End,
    FacePlayer,
    Rotate,
    PlayMotion,
    Move,
    WaitAction,
    WaitMotion,
    SetFlag,
    SetWork,
    IfFlag,
    IfWork,
    MoveTo,
    GiveItem,
    Encounter,
    IfBattleResult,
    TrainerBattle,
    Menu,
    MenuOption
};
enum class ConversationComparison { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
bool conversation_branch(ConversationAction action);
const char *conversation_comparison(ConversationComparison comparison);
bool conversation_compare(int actual, ConversationComparison comparison, int expected);
struct ConversationStep {
    unsigned id = 0;
    ConversationAction action = ConversationAction::Message;
    std::string message, yes_message, no_message;
    unsigned sound = 327694, frames = 20, repeat = 3;
    float volume = 1;
    int actor = -2, state_id = 0, value = 0;
    float angle = 0;
    std::array<float, 3> destination{};
    float turn_threshold_degrees = 20;
    unsigned item = 1, quantity = 1, encounter = 0, trainer = 1;
    bool wait_for_completion = false;
    ConversationComparison comparison = ConversationComparison::Equal;
    std::array<int, 3> arguments{0, 0, -1};
    std::vector<ConversationStep> children, otherwise;
    bool operator==(const ConversationStep &) const = default;
};
struct ConversationMessage {
    std::string symbol, text;
    bool formatted = false;
    std::map<unsigned, std::string> translations;
    const std::string &text_for(unsigned language) const {
        auto it = translations.find(language);
        return it == translations.end() || it->second.empty() ? text : it->second;
    }
    bool operator==(const ConversationMessage &) const = default;
};
enum class InteractionTargetKind { Npc, Scenery, PositionTrigger, Trainer };
struct ConversationSource {
    unsigned area = 0, local_zone = 0, event = 0, script = 0;
    InteractionTargetKind kind = InteractionTargetKind::Npc;
    bool operator==(const ConversationSource &) const = default;
};
struct ConversationDraft {
    std::vector<ConversationStep> steps;
    std::vector<ConversationMessage> messages;
    bool custom = false;
    std::string pawn;
    std::set<unsigned> languages{GameProfile::dialogue_fallback_language};
    bool operator==(const ConversationDraft &) const = default;
};
struct ConversationPawn {
    std::string source, definitions;
    std::map<unsigned, unsigned> line_steps;
};
class AuthoredInteraction {
  public:
    AuthoredInteraction(View source, ConversationSource target);
    const ConversationDraft &draft() const {
        return draft_;
    }
    const ConversationSource &target() const {
        return target_;
    }
    void update(ConversationDraft next);
    bool undo();
    bool redo();
    bool dirty() const {
        return draft_ != saved_;
    }
    void mark_saved() {
        saved_ = draft_;
    }
    std::string serialize() const;
    void restore(const std::string &record);
    ConversationPawn generate(const std::map<std::string, unsigned> &message_ids) const;
    static void validate(const ConversationDraft &draft);

  private:
    std::string fingerprint_;
    ConversationSource target_;
    ConversationDraft draft_, saved_;
    std::vector<ConversationDraft> undo_, redo_;
};
}
