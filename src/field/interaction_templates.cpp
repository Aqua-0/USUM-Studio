#include "field/interaction_templates.h"
#include "field/area.h"
#include <algorithm>
#include <functional>
#include <limits>
namespace studio {
const char *interaction_template_name(InteractionTemplate kind) {
    switch (kind) {
    case InteractionTemplate::ItemGift:
        return "One-time item gift";
    case InteractionTemplate::PokemonGift:
        return "One-time Pokemon gift";
    case InteractionTemplate::ItemTrade:
        return "Repeatable item trade";
    case InteractionTemplate::PokemonTrade:
        return "Repeatable Pokemon trade";
    case InteractionTemplate::TrainerChallenge:
        return "Trainer challenge";
    case InteractionTemplate::ConditionalDialogue:
        return "Conditional dialogue";
    case InteractionTemplate::DeliveryQuest:
        return "One-time delivery quest";
    }
    throw std::runtime_error("Unknown interaction template");
}
bool interaction_template_needs_flag(InteractionTemplate kind) {
    return kind == InteractionTemplate::ItemGift || kind == InteractionTemplate::PokemonGift ||
           kind == InteractionTemplate::ConditionalDialogue ||
           kind == InteractionTemplate::DeliveryQuest;
}
unsigned insert_interaction_template(ConversationDraft &draft, unsigned selected, bool otherwise,
                                     const InteractionTemplateOptions &options) {
    require(!draft.custom, "Restore generated Pawn before inserting a visual template");
    interaction_template_name(options.kind);
    if (interaction_template_needs_flag(options.kind))
        require(options.flag >= 0 && unsigned(options.flag) < TargetProfile::saved_event_flag_count,
                "Choose a saved event flag for this template");
    auto next = draft;
    unsigned id = 1;
    std::function<void(const std::vector<ConversationStep> &)> scan = [&](const auto &steps) {
        for (const auto &step : steps) {
            require(step.id < std::numeric_limits<unsigned>::max() - 32, "Step IDs are exhausted");
            id = std::max(id, step.id + 1);
            scan(step.children);
            scan(step.otherwise);
        }
    };
    scan(next.steps);
    auto make = [&](ConversationAction action) {
        ConversationStep step;
        step.id = id++;
        step.action = action;
        return step;
    };
    auto text = [&](unsigned step, const char *suffix, const char *value) {
        auto symbol = "MSG_TEMPLATE_" + std::to_string(step) + suffix;
        while (std::any_of(next.messages.begin(), next.messages.end(), [&](const auto &m) {
            return m.symbol == symbol;
        }))
            symbol += '_';
        next.messages.push_back({symbol, value});
        return symbol;
    };
    auto message = [&](const char *value) {
        auto step = make(ConversationAction::Message);
        step.message = text(step.id, "", value);
        return step;
    };
    auto condition = [&](int flag) {
        auto step = make(ConversationAction::IfFlag);
        step.state_id = flag;
        step.value = 1;
        return step;
    };
    auto completion = [&] {
        auto step = make(ConversationAction::SetFlag);
        step.state_id = options.flag;
        step.value = 1;
        return step;
    };
    auto choice = [&](const char *prompt) {
        auto step = make(ConversationAction::Choice);
        step.message = text(step.id, "", prompt);
        step.yes_message = text(step.id, "_YES", "Yes");
        step.no_message = text(step.id, "_NO", "No");
        step.otherwise = {message("Maybe another time.")};
        return step;
    };
    auto reward = [&](ConversationAction action) {
        auto step = options.reward;
        step.id = id++;
        step.action = action;
        step.children.clear();
        step.otherwise.clear();
        return step;
    };
    ConversationStep root;
    switch (options.kind) {
    case InteractionTemplate::ItemGift:
    case InteractionTemplate::PokemonGift: {
        root = condition(options.flag);
        root.children = {message("I hope you enjoy your gift!")};
        auto gift =
            reward(options.kind == InteractionTemplate::ItemGift ? ConversationAction::GiveItem
                                                                 : ConversationAction::GivePokemon);
        gift.children = {completion(), message("Here you go!")};
        gift.otherwise = {message("Please make room and come back.")};
        root.otherwise = {gift};
        break;
    }
    case InteractionTemplate::ItemTrade:
    case InteractionTemplate::PokemonTrade:
    case InteractionTemplate::DeliveryQuest: {
        auto offer = choice(options.kind == InteractionTemplate::DeliveryQuest
                                ? "Do you have the items I requested?"
                                : "Would you like to trade?");
        auto trade = reward(options.kind == InteractionTemplate::PokemonTrade
                                ? ConversationAction::TradePokemon
                                : ConversationAction::TradeItems);
        if (options.kind == InteractionTemplate::DeliveryQuest)
            trade.children.push_back(completion());
        trade.children.push_back(message("Thank you!"));
        trade.otherwise = {message("We couldn't complete the exchange. Please try again later.")};
        offer.children = {trade};
        if (options.kind == InteractionTemplate::DeliveryQuest) {
            root = condition(options.flag);
            root.children = {message("Thanks again for your help!")};
            root.otherwise = {offer};
        } else
            root = offer;
        break;
    }
    case InteractionTemplate::TrainerChallenge: {
        require(options.reward.trainer > 0 &&
                    options.reward.trainer < TargetProfile::saved_event_flag_count - 3036,
                "Trainer ID has no supported saved defeated flag");
        root = condition(int(3036 + options.reward.trainer));
        root.children = {message("That was a great battle!")};
        auto offer = choice("Would you like to battle?");
        auto battle = reward(ConversationAction::TrainerBattle);
        battle.children = {message("You won! Well done!")};
        offer.children = {battle};
        root.otherwise = {offer};
        break;
    }
    case InteractionTemplate::ConditionalDialogue:
        root = condition(options.flag);
        root.children = {message("Things have changed since we last spoke.")};
        root.otherwise = {message("Come back when you're ready.")};
        break;
    }
    unsigned inserted = root.id;
    bool found = false;
    std::function<void(std::vector<ConversationStep> &)> insert = [&](auto &steps) {
        for (std::size_t i = 0; i < steps.size() && !found; ++i) {
            auto &at = steps[i];
            if (at.id == selected) {
                if (at.action == ConversationAction::Menu)
                    at.otherwise.push_back(root);
                else if (conversation_branch(at.action))
                    (otherwise ? at.otherwise : at.children).push_back(root);
                else if (at.action == ConversationAction::Repeat ||
                         at.action == ConversationAction::MenuOption)
                    at.children.push_back(root);
                else
                    steps.insert(
                        steps.begin() +
                            std::ptrdiff_t(i + (at.action == ConversationAction::End ? 0 : 1)),
                        root);
                found = true;
                return;
            }
            insert(at.children);
            if (!found)
                insert(at.otherwise);
        }
    };
    insert(next.steps);
    require(found, "Select an insertion step before adding a template");
    AuthoredInteraction::validate(next);
    draft = std::move(next);
    return inserted;
}
}
