#pragma once
#include "field/authored_interaction.h"
namespace studio {
enum class InteractionTemplate {
    ItemGift,
    PokemonGift,
    ItemTrade,
    PokemonTrade,
    TrainerChallenge,
    ConditionalDialogue,
    DeliveryQuest
};
struct InteractionTemplateOptions {
    InteractionTemplate kind = InteractionTemplate::ItemGift;
    int flag = -1;
    ConversationStep reward;
};
const char *interaction_template_name(InteractionTemplate kind);
bool interaction_template_needs_flag(InteractionTemplate kind);
unsigned insert_interaction_template(ConversationDraft &draft, unsigned selected, bool otherwise,
                                     const InteractionTemplateOptions &options);
}
