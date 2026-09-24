#include "field/authored_interaction.h"
#include "core/digest.h"
#include "field/dialogue_text.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
namespace studio {
namespace {
bool symbol(const std::string &s) {
    return s.starts_with("MSG_") && s.size() > 4 && s.size() <= 63 &&
           std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
           });
}
void write_steps(std::ostream &out, const std::vector<ConversationStep> &steps) {
    out << steps.size() << '\n';
    for (const auto &s : steps) {
        out << s.id << ' ' << int(s.action) << ' ' << std::quoted(s.message) << ' '
            << std::quoted(s.yes_message) << ' ' << std::quoted(s.no_message) << ' ' << s.sound
            << ' ' << s.frames << ' ' << s.repeat << ' ' << std::setprecision(9) << s.volume << ' '
            << s.actor << ' ' << s.state_id << ' ' << s.value << ' ' << s.angle << ' '
            << s.wait_for_completion << ' ' << int(s.comparison);
        for (auto argument : s.arguments)
            out << ' ' << argument;
        for (auto coordinate : s.destination)
            out << ' ' << coordinate;
        out << ' ' << s.turn_threshold_degrees << ' ' << s.item << ' ' << s.quantity << ' '
            << s.encounter << ' ' << s.trainer;
        out << '\n';
        write_steps(out, s.children);
        write_steps(out, s.otherwise);
    }
}
std::vector<ConversationStep> read_steps(std::istream &in, unsigned depth, unsigned &total,
                                         unsigned version) {
    unsigned count = 0;
    require(depth <= 32 && bool(in >> count) && count <= 4096 - total,
            "Invalid conversation step count or nesting");
    total += count;
    std::vector<ConversationStep> steps(count);
    for (auto &s : steps) {
        int kind = 0;
        require(bool(in >> s.id >> kind >> std::quoted(s.message) >> std::quoted(s.yes_message) >>
                     std::quoted(s.no_message) >> s.sound >> s.frames >> s.repeat >> s.volume) &&
                    kind >= 0 &&
                    kind <= int(version == 1   ? ConversationAction::End
                                : version == 2 ? ConversationAction::IfWork
                                : version == 3 ? ConversationAction::IfBattleResult
                                : version == 4 ? ConversationAction::TrainerBattle
                                               : ConversationAction::MenuOption),
                "Invalid conversation step");
        if (version >= 2) {
            int comparison;
            require(bool(in >> s.actor >> s.state_id >> s.value >> s.angle >>
                         s.wait_for_completion >> comparison) &&
                        comparison >= 0 && comparison <= int(ConversationComparison::GreaterEqual),
                    "Invalid action parameters");
            s.comparison = ConversationComparison(comparison);
            for (auto &argument : s.arguments)
                require(bool(in >> argument), "Missing action argument");
        }
        if (version >= 3) {
            for (auto &coordinate : s.destination)
                require(bool(in >> coordinate), "Missing destination coordinate");
            require(bool(in >> s.turn_threshold_degrees >> s.item >> s.quantity >> s.encounter),
                    "Missing reward or encounter parameters");
        }
        if (version >= 4)
            require(bool(in >> s.trainer), "Missing trainer battle ID");
        s.action = ConversationAction(kind);
        s.children = read_steps(in, depth + 1, total, version);
        s.otherwise = read_steps(in, depth + 1, total, version);
    }
    return steps;
}
}
bool conversation_branch(ConversationAction action) {
    return action == ConversationAction::Choice || action == ConversationAction::IfFlag ||
           action == ConversationAction::IfWork || action == ConversationAction::GiveItem ||
           action == ConversationAction::Encounter ||
           action == ConversationAction::IfBattleResult ||
           action == ConversationAction::TrainerBattle || action == ConversationAction::Menu;
}
const char *conversation_comparison(ConversationComparison comparison) {
    static const char *operators[] = {"==", "!=", "<", "<=", ">", ">="};
    require(int(comparison) >= 0 && int(comparison) < 6, "Invalid comparison");
    return operators[int(comparison)];
}
bool conversation_compare(int actual, ConversationComparison comparison, int expected) {
    switch (comparison) {
    case ConversationComparison::Equal:
        return actual == expected;
    case ConversationComparison::NotEqual:
        return actual != expected;
    case ConversationComparison::Less:
        return actual < expected;
    case ConversationComparison::LessEqual:
        return actual <= expected;
    case ConversationComparison::Greater:
        return actual > expected;
    case ConversationComparison::GreaterEqual:
        return actual >= expected;
    }
    throw std::runtime_error("Invalid comparison");
}
AuthoredInteraction::AuthoredInteraction(View source, ConversationSource target)
    : fingerprint_(sha256(source)), target_(target) {
    require(target.event <= 65535 &&
                (target.script > 0 || target.kind == InteractionTargetKind::PositionTrigger) &&
                target.script <= 65535,
            "Choose a valid interaction placement");
    ConversationStep begin, message, end;
    begin.id = 1;
    begin.action = ConversationAction::Begin;
    message.id = 2;
    message.message = "MSG_GREETING";
    end.id = 3;
    end.action = ConversationAction::End;
    draft_.steps = {begin, message, end};
    draft_.messages = {{"MSG_GREETING", "Alola!"}};
    if (target.kind == InteractionTargetKind::Trainer) {
        require(target.script > 1000 && target.script < 3000, "Unsupported trainer selector");
        ConversationStep branch, defeated, challenge, battle, returned;
        branch.id = 4;
        branch.action = ConversationAction::IfFlag;
        branch.state_id = int(3036 + target.script - 1000);
        branch.value = 1;
        defeated.id = 5;
        defeated.message = "MSG_DEFEATED";
        challenge.id = 6;
        challenge.message = "MSG_CHALLENGE";
        battle.id = 7;
        battle.action = ConversationAction::TrainerBattle;
        battle.trainer = target.script - 1000;
        returned.id = 8;
        returned.message = "MSG_RETURNED";
        battle.children = {returned};
        branch.children = {defeated};
        branch.otherwise = {challenge, battle};
        draft_.steps = {begin, branch, end};
        draft_.messages = {{"MSG_DEFEATED", "That was a great battle!"},
                           {"MSG_CHALLENGE", "Let's battle!"},
                           {"MSG_RETURNED", "Thanks for the battle!"}};
    }
    saved_ = draft_;
}
void AuthoredInteraction::validate(const ConversationDraft &draft) {
    require(draft.pawn.size() <= 1024 * 1024 && draft.pawn.find('\0') == std::string::npos,
            "Invalid Pawn source text");
    require(draft.messages.size() <= 65535, "Too many conversation messages");
    require(draft.languages.contains(GameProfile::dialogue_fallback_language),
            "English fallback must be enabled");
    for (auto language : draft.languages)
        require(language < GameProfile::dialogue_languages.size(), "Unknown dialogue language");
    std::set<std::string> messages;
    for (const auto &m : draft.messages) {
        require(symbol(m.symbol) && messages.insert(m.symbol).second,
                "Message symbols must be unique MSG_ names using capital letters, digits and "
                "underscores");
        require(m.text.size() <= 2048 && m.text.find('\0') == std::string::npos,
                "Enter message text up to 2048 UTF-8 bytes");
        for (const auto &[language, translation] : m.translations) {
            require(language < GameProfile::dialogue_languages.size() &&
                        language != GameProfile::dialogue_fallback_language,
                    "Unknown translation language");
            require(translation.size() <= 2048 && translation.find('\0') == std::string::npos,
                    "Enter translation text up to 2048 UTF-8 bytes");
        }
    }
    require(draft.steps.size() >= 2 && draft.steps.front().action == ConversationAction::Begin &&
                draft.steps.back().action == ConversationAction::End,
            "A visual conversation needs Begin first and End last");
    std::set<unsigned> ids;
    unsigned total = 0;
    std::function<void(const std::vector<ConversationStep> &, unsigned, bool)> visit;
    visit = [&](const auto &steps, unsigned depth, bool options) {
        require(depth <= 32, "Conversation nesting is too deep");
        for (const auto &s : steps) {
            require((s.action == ConversationAction::MenuOption) == options,
                    "Option branches belong directly inside a menu");
            require(++total <= 4096 && s.id && ids.insert(s.id).second,
                    "Conversation steps need unique IDs and at most 4096 steps");
            require(int(s.action) >= 0 && int(s.action) <= int(ConversationAction::MenuOption),
                    "Unknown conversation action");
            if (s.action == ConversationAction::Begin || s.action == ConversationAction::End)
                require(depth == 0 && (&s == &draft.steps.front() || &s == &draft.steps.back()),
                        "Begin and End belong at the conversation boundaries");
            if (s.action == ConversationAction::Message || s.action == ConversationAction::Choice ||
                s.action == ConversationAction::Menu || s.action == ConversationAction::MenuOption)
                require(messages.contains(s.message),
                        "Step references a missing message: " + s.message);
            if (s.action == ConversationAction::Menu) {
                require(s.children.size() >= 2, "A menu needs at least two options");
                for (const auto &option : s.children)
                    require(option.action == ConversationAction::MenuOption,
                            "Menu options must be option branches");
            }
            auto plain_label = [&](const std::string &label, bool single_line) {
                auto it =
                    std::find_if(draft.messages.begin(), draft.messages.end(), [&](const auto &m) {
                        return m.symbol == label;
                    });
                require(it != draft.messages.end() && !it->formatted &&
                            (!single_line || it->text.find('\n') == std::string::npos),
                        "Menu labels must be plain single-line text");
                if (single_line)
                    for (auto language : draft.languages)
                        require(it->text_for(language).find('\n') == std::string::npos,
                                "Translated menu labels must be single-line text");
            };
            if (s.action == ConversationAction::MenuOption)
                plain_label(s.message, true);
            if (s.action == ConversationAction::Choice) {
                plain_label(s.yes_message, false);
                plain_label(s.no_message, false);
            }
            if (s.action == ConversationAction::Choice)
                require(messages.contains(s.yes_message) && messages.contains(s.no_message),
                        "Choose messages for both choice labels");
            if (s.action == ConversationAction::Sound)
                require(s.sound <= 2147483647 && std::isfinite(s.volume) && s.volume >= 0 &&
                            s.volume <= 1,
                        "Sound volume must be between zero and one");
            if (s.action == ConversationAction::Wait)
                require(s.frames > 0 && s.frames <= 2147483647,
                        "Wait needs a positive frame count");
            if (s.action == ConversationAction::Repeat)
                require(s.repeat > 0 && s.repeat <= 2147483647, "Repeat needs a positive count");
            require(s.actor >= -2 && s.actor <= 65535 && s.state_id >= 0 && s.state_id <= 65535,
                    "Use an actor event ID or player, and a state ID from 0 to 65535");
            require(std::isfinite(s.angle), "Rotation must be finite");
            if (s.action == ConversationAction::MoveTo) {
                require(std::all_of(s.destination.begin(), s.destination.end(),
                                    [](float value) {
                                        return std::isfinite(value) && std::abs(value) < 1e8f;
                                    }),
                        "Destination coordinates must be finite world positions");
                require(std::isfinite(s.turn_threshold_degrees) && s.turn_threshold_degrees >= 0,
                        "Turn animation threshold must be nonnegative and finite");
            }
            if (s.action == ConversationAction::GiveItem)
                require(s.item > 0 && s.item <= 65535 && s.quantity > 0 && s.quantity <= 999,
                        "Choose an item and a quantity from 1 to 999");
            if (s.action == ConversationAction::TrainerBattle)
                require(s.trainer > 0 && s.trainer <= 65535, "Choose an existing trainer ID");
            if (s.action == ConversationAction::Encounter ||
                s.action == ConversationAction::TrainerBattle) {
                require(s.encounter <= 65535, "Invalid encounter row");
                for (const auto &loss : s.otherwise)
                    require(loss.action == ConversationAction::SetFlag ||
                                loss.action == ConversationAction::SetWork,
                            "Before defeat recovery, only flag and work updates are supported");
            }
            if (s.action == ConversationAction::IfBattleResult)
                require(s.value >= 1 && s.value <= 4,
                        "Choose a returned battle result from 1 to 4");
            require(int(s.comparison) >= 0 &&
                        int(s.comparison) <= int(ConversationComparison::GreaterEqual),
                    "Invalid comparison");
            if (s.action == ConversationAction::SetFlag || s.action == ConversationAction::IfFlag)
                require(s.value == 0 || s.value == 1, "Flag value must be off or on");
            require(s.children.empty() || conversation_branch(s.action) ||
                        s.action == ConversationAction::Repeat ||
                        s.action == ConversationAction::MenuOption,
                    "Only branches and repeats contain steps");
            require(s.otherwise.empty() || conversation_branch(s.action),
                    "Only branches have a second branch");
            visit(s.children, depth + 1, s.action == ConversationAction::Menu);
            visit(s.otherwise, depth + 1, false);
        }
    };
    visit(draft.steps, 0, false);
}
void AuthoredInteraction::update(ConversationDraft next) {
    validate(next);
    if (next == draft_)
        return;
    undo_.push_back(draft_);
    if (undo_.size() > 80)
        undo_.erase(undo_.begin());
    redo_.clear();
    draft_ = std::move(next);
}
bool AuthoredInteraction::undo() {
    if (undo_.empty())
        return false;
    redo_.push_back(draft_);
    draft_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}
bool AuthoredInteraction::redo() {
    if (redo_.empty())
        return false;
    undo_.push_back(draft_);
    draft_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}
std::string AuthoredInteraction::serialize() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "authored_interaction 7 " << fingerprint_ << ' ' << target_.area << ' '
        << target_.local_zone << ' ' << target_.event << ' ' << target_.script << ' '
        << int(target_.kind) << '\n';
    out << draft_.custom << ' ' << std::quoted(draft_.pawn) << '\n'
        << draft_.messages.size() << '\n';
    for (const auto &m : draft_.messages) {
        out << std::quoted(m.symbol) << ' ' << std::quoted(m.text) << ' ' << m.formatted << ' '
            << m.translations.size() << '\n';
        for (const auto &[language, translation] : m.translations)
            out << language << ' ' << std::quoted(translation) << '\n';
    }
    out << draft_.languages.size();
    for (auto language : draft_.languages)
        out << ' ' << language;
    out << '\n';
    write_steps(out, draft_.steps);
    return out.str();
}
void AuthoredInteraction::restore(const std::string &record) {
    require(record.size() <= 8 * 1024 * 1024, "Conversation record is too large");
    std::istringstream in(record);
    in.imbue(std::locale::classic());
    std::string magic, hash;
    unsigned version = 0, count = 0, total = 0;
    ConversationSource target;
    ConversationDraft next;
    require(bool(in >> magic >> version >> hash >> target.area >> target.local_zone >>
                 target.event >> target.script) &&
                magic == "authored_interaction" && (version >= 1 && version <= 7) &&
                hash == fingerprint_,
            "Conversation source or target changed; reopen against the matching project source");
    if (version >= 6) {
        int kind;
        require(bool(in >> kind) && kind >= 0 && kind <= int(InteractionTargetKind::Trainer),
                "Invalid interaction target kind");
        target.kind = InteractionTargetKind(kind);
    }
    require(target == target_, "Interaction target changed");
    require(bool(in >> next.custom >> std::quoted(next.pawn) >> count) && count <= 65535,
            "Malformed conversation messages");
    next.messages.resize(count);
    for (auto &m : next.messages) {
        require(bool(in >> std::quoted(m.symbol) >> std::quoted(m.text)),
                "Malformed conversation message");
        if (version >= 5)
            require(bool(in >> m.formatted), "Missing dialogue formatting setting");
        if (version >= 7) {
            unsigned translations;
            require(bool(in >> translations) && translations < GameProfile::dialogue_languages.size(),
                    "Invalid translation count");
            for (unsigned i = 0; i < translations; ++i) {
                unsigned language;
                std::string translated;
                require(bool(in >> language >> std::quoted(translated)) &&
                            m.translations.emplace(language, std::move(translated)).second,
                        "Malformed or duplicate translation");
            }
        }
    }
    if (version >= 7) {
        unsigned languages;
        require(bool(in >> languages) && languages <= GameProfile::dialogue_languages.size(),
                "Invalid dialogue language count");
        next.languages.clear();
        for (unsigned i = 0; i < languages; ++i) {
            unsigned language;
            require(bool(in >> language) && next.languages.insert(language).second,
                    "Malformed or duplicate dialogue language");
        }
    }
    next.steps = read_steps(in, 0, total, version);
    in >> std::ws;
    require(in.eof(), "Unexpected conversation data");
    validate(next);
    draft_ = std::move(next);
    saved_ = draft_;
    undo_.clear();
    redo_.clear();
}
ConversationPawn
AuthoredInteraction::generate(const std::map<std::string, unsigned> &message_ids) const {
    validate(draft_);
    ConversationPawn result;
    std::ostringstream definitions, source;
    definitions << "#define ACTOR_SELF "
                << (target_.kind == InteractionTargetKind::PositionTrigger ? -1
                                                                           : int(target_.event))
                << '\n';
    if (target_.kind == InteractionTargetKind::PositionTrigger)
        definitions << "#define POSITION_TRIGGER 1\n";
    if (target_.kind == InteractionTargetKind::Scenery ||
        target_.kind == InteractionTargetKind::PositionTrigger)
        definitions << "#define SCENERY_INTERACTION 1\n";
    std::set<unsigned> allocated;
    for (const auto &m : draft_.messages) {
        auto it = message_ids.find(m.symbol);
        require(it != message_ids.end() && it->second < 65535 &&
                    allocated.insert(it->second).second,
                "Missing or duplicate allocated message: " + m.symbol);
        definitions << "#define " << m.symbol << ' ' << it->second << '\n';
    }
    definitions << "\nstock PrepareMessage(message)\n{\n    #pragma unused message\n";
    for (const auto &m : draft_.messages) {
        require(!m.text.empty(), "Enter text for " + m.symbol);
        auto text = encode_dialogue_text(m.text, m.formatted);
        for (auto language : draft_.languages)
            try {
                encode_dialogue_translation(m.text, m.text_for(language), m.formatted);
            } catch (const std::exception &e) {
                throw std::runtime_error(m.symbol + " / " +
                                         GameProfile::dialogue_languages[language].name + ": " + e.what());
            }
        if (text.setup.empty())
            continue;
        definitions << "    if (message == " << m.symbol << ") {\n";
        for (const auto &call : text.setup)
            definitions << "        " << call << '\n';
        definitions << "    }\n";
    }
    definitions << "}\n";
    result.definitions = definitions.str();
    if (draft_.custom) {
        result.source = draft_.pawn;
        return result;
    }
    unsigned line = 0;
    auto emit = [&](const std::string &s, unsigned step = 0) {
        source << s << '\n';
        ++line;
        if (step)
            result.line_steps[line] = step;
    };
    emit("#include \"messages.inc\"");
    emit("#include <usum>");
    emit("");
    emit("main()");
    emit("{");
    emit("    new battle_result = -1;");
    emit("    #pragma unused battle_result");
    std::function<void(const std::vector<ConversationStep> &, unsigned)> write;
    write = [&](const auto &steps, unsigned depth) {
        for (const auto &s : steps) {
            auto pad = std::string(depth * 4, ' ');
            auto call = [&](const std::string &code) {
                emit(pad + code, s.id);
            };
            bool character_action = (s.action >= ConversationAction::FacePlayer &&
                                     s.action <= ConversationAction::WaitMotion) ||
                                    s.action == ConversationAction::MoveTo;
            require(target_.kind == InteractionTargetKind::Npc ||
                        target_.kind == InteractionTargetKind::Trainer || !character_action ||
                        s.actor != -2,
                    "This interaction has no NPC body; choose Player or an explicit character for "
                    "character "
                    "actions");
            auto actor = s.actor == -2 ? std::string("ACTOR_SELF") : std::to_string(s.actor);
            switch (s.action) {
            case ConversationAction::MoveTo:
                call("MoveCharacterTo(" + actor + ", " +
                     std::to_string(std::bit_cast<std::int32_t>(s.destination[0])) + ", " +
                     std::to_string(std::bit_cast<std::int32_t>(s.destination[1])) + ", " +
                     std::to_string(std::bit_cast<std::int32_t>(s.destination[2])) + ", " +
                     std::to_string(std::bit_cast<std::int32_t>(s.angle)) + ", " +
                     std::to_string(std::bit_cast<std::int32_t>(s.turn_threshold_degrees)) + ");");
                if (s.wait_for_completion)
                    call("WaitForAction(" + actor + ");");
                break;
            case ConversationAction::GiveItem:
                call("if (GiveItem(" + std::to_string(s.item) + ", " + std::to_string(s.quantity) +
                     ")) {");
                write(s.children, depth + 1);
                call("} else {");
                write(s.otherwise, depth + 1);
                call("}");
                break;
            case ConversationAction::TrainerBattle:
            case ConversationAction::Encounter:
                call(s.action == ConversationAction::TrainerBattle
                         ? "battle_result = StartTrainerBattle(" + std::to_string(s.trainer) + ");"
                         : "battle_result = StartEncounter(" + std::to_string(s.encounter) + ");");
                call("if (battle_result == 0) {");
                write(s.otherwise, depth + 1);
                emit(pad + (s.action == ConversationAction::TrainerBattle
                                ? "    RecoverFromTrainerDefeat();"
                                : "    RecoverFromDefeat();"),
                     s.id);
                emit(pad + "    return 0;", s.id);
                call("}");
                write(s.children, depth);
                break;
            case ConversationAction::IfBattleResult:
                call("if (battle_result == " + std::to_string(s.value) + ") {");
                write(s.children, depth + 1);
                call("} else {");
                write(s.otherwise, depth + 1);
                call("}");
                break;
            case ConversationAction::FacePlayer:
                call("FacePlayer(" + actor + ");");
                if (s.wait_for_completion)
                    call("WaitForAction(" + actor + ");");
                break;
            case ConversationAction::Rotate:
                call("RotateCharacter(" + actor + ", " +
                     std::to_string(std::bit_cast<std::int32_t>(s.angle)) + ");");
                if (s.wait_for_completion)
                    call("WaitForAction(" + actor + ");");
                break;
            case ConversationAction::PlayMotion:
                call("ChrMotionPlayFrame_(" + actor + ", " + std::to_string(s.arguments[0]) + ", " +
                     std::to_string(s.arguments[1]) + ", " + std::to_string(s.arguments[2]) + ");");
                if (s.wait_for_completion)
                    call("WaitForMotion(" + actor + ");");
                break;
            case ConversationAction::Move:
                call("ChrMove_(" + actor + ", " + std::to_string(s.arguments[0]) + ", " +
                     std::to_string(s.arguments[1]) + ");");
                if (s.wait_for_completion)
                    call("WaitForAction(" + actor + ");");
                break;
            case ConversationAction::WaitAction:
                call("WaitForAction(" + actor + ");");
                break;
            case ConversationAction::WaitMotion:
                call("WaitForMotion(" + actor + ");");
                break;
            case ConversationAction::SetFlag:
                call(std::string(s.value ? "FlagSet(" : "FlagReset(") + std::to_string(s.state_id) +
                     ");");
                break;
            case ConversationAction::SetWork:
                call("WorkSet(" + std::to_string(s.state_id) + ", " + std::to_string(s.value) +
                     ");");
                break;
            case ConversationAction::IfFlag:
            case ConversationAction::IfWork:
                call(std::string("if (") +
                     (s.action == ConversationAction::IfFlag ? "FlagGet(" : "WorkGet(") +
                     std::to_string(s.state_id) + ") " +
                     (s.action == ConversationAction::IfFlag
                          ? "=="
                          : conversation_comparison(s.comparison)) +
                     " " + std::to_string(s.value) + ") {");
                write(s.children, depth + 1);
                call("} else {");
                write(s.otherwise, depth + 1);
                call("}");
                break;
            case ConversationAction::Begin:
                call("BeginConversation();");
                break;
            case ConversationAction::End:
                call("EndConversation();");
                break;
            case ConversationAction::Menu: {
                call("PrepareMessage(" + s.message + ");");
                call("OpenMessage(" + s.message + ");");
                call("BeginChoiceMenu();");
                for (unsigned i = 0; i < s.children.size(); ++i)
                    call("AddChoiceOption(" + s.children[i].message + ", " + std::to_string(i) +
                         ");");
                auto var = "menu_" + std::to_string(s.id);
                call("new " + var + " = FinishChoiceMenu();");
                for (unsigned i = 0; i < s.children.size(); ++i) {
                    call(std::string(i ? "} else if (" : "if (") + var +
                         " == " + std::to_string(i) + ") {");
                    write(s.children[i].children, depth + 1);
                }
                call("} else {");
                write(s.otherwise, depth + 1);
                call("}");
                break;
            }
            case ConversationAction::MenuOption:
                throw std::runtime_error("Option branch outside its menu");
            case ConversationAction::Message:
                call("PrepareMessage(" + s.message + ");");
                call("ShowMessage(" + s.message + ");");
                break;
            case ConversationAction::Sound:
                call("SEPlay(" + std::to_string(s.sound) + ", " +
                     std::to_string(std::bit_cast<std::uint32_t>(s.volume)) +
                     ", 0, FLOAT_ONE_BITS);");
                break;
            case ConversationAction::Wait:
                call("WaitFrames(" + std::to_string(s.frames) + ");");
                break;
            case ConversationAction::Choice:
                call("PrepareMessage(" + s.message + ");");
                call("OpenMessage(" + s.message + ");");
                call("if (AskYesNo(" + s.yes_message + ", " + s.no_message + ")) {");
                write(s.children, depth + 1);
                call("} else {");
                write(s.otherwise, depth + 1);
                call("}");
                break;
            case ConversationAction::Repeat: {
                auto var = "repeat_" + std::to_string(s.id);
                call("for (new " + var + " = 0; " + var + " < " + std::to_string(s.repeat) + "; " +
                     var + "++) {");
                write(s.children, depth + 1);
                call("}");
                break;
            }
            }
        }
    };
    write(draft_.steps, 1);
    emit("    return 0;");
    emit("}");
    result.source = source.str();
    return result;
}
}
