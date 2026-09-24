#include "field/interaction.h"
#include "field/interaction_source.h"
#include "field/area.h"
#include "field/map_catalog.h"
#include "formats/archive.h"
#include "formats/container.h"
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
namespace studio {
namespace {
struct Value {
    std::optional<std::int32_t> value;
    bool local = false;
    unsigned owner = 0;
    Value() = default;
    Value(std::int32_t v) : value(v) {
    }
    explicit operator bool() const {
        return value.has_value();
    }
    std::int32_t operator*() const {
        return *value;
    }
};
Value sum(Value a, Value b) {
    if (!a || !b || (a.local && b.local))
        return {};
    Value result(std::int32_t(unsigned(*a) + unsigned(*b)));
    result.local = a.local || b.local;
    result.owner = a.local ? a.owner : b.owner;
    return result;
}
std::string value_text(Value v) {
    return v ? (v.local ? "local address" : std::to_string(*v)) : "runtime value";
}
std::string native_name(unsigned hash) {
    static const char *names[] = {"TrainerIDGet",
                                  "TrainerGetScrID",
                                  "TrainerGetEventID",
                                  "CallTrainerBattleCore",
                                  "BattleGetResult_",
                                  "FlagGet",
                                  "FlagSet",
                                  "FlagReset",
                                  "ZigarudeCellStatusSet",
                                  "RecordAdd",
                                  "MEPlay",
                                  "WordSetNumber",
                                  "VoicePlay_",
                                  "ExtraMsgInstant_",
                                  "ChrMotionCommandInit_",
                                  "ChrMotionCommandEntry_",
                                  "ChrMotionCommandPlay_",
                                  "CharKindCheck",
                                  "SEPlay",
                                  "WorkGet",
                                  "ChrCheckSittingTalk",
                                  "TalkMdlStartInit_",
                                  "ChrMotionEndWatchStart",
                                  "WorkSet",
                                  "TalkMdlEndInit_",
                                  "ChrMotionEndWatchClear",
                                  "TalkMdlGetRepairAngle_",
                                  "TalkMsg_Seq",
                                  "TalkMsg_SeqWait",
                                  "GetMonsNo",
                                  "TalkMsg_VisubleCursor",
                                  "ABKeyWait_",
                                  "MsgWinCloseNo",
                                  "PG_WordSetRegister",
                                  "ChrActionCommandIsPlaying_",
                                  "ChrMotionIsPlaying_",
                                  "ChrMotionPlayFrame_",
                                  "ChrSetDefaultIdle",
                                  "ChrRot_",
                                  "ChrRotTarget_",
                                  "ChrFastRotTarget_",
                                  "ChrMotionGetPlayNo",
                                  "VoiceIsPlaying",
                                  "PlayerGetForm",
                                  "ChrWaitLookAt_",
                                  "ChrLookAtFieldActor_",
                                  "ChrLookAtChrEye_",
                                  "ChrResetLookAt_",
                                  "GetTargetActorEventId",
                                  "LastKeyWait_",
                                  "GlobalCall",
                                  "_Suspend"};
    for (auto name : names)
        if (pawn_name_hash(name) == hash)
            return name;
    std::ostringstream text;
    text << "Unknown native 0x" << std::hex << hash;
    return text.str();
}
unsigned relative_target(unsigned address, std::int32_t displacement) {
    auto result = std::int64_t(address) + displacement;
    require(result >= 0 && result <= 0xffffffffll, "Invalid AMX branch target");
    return unsigned(result);
}
unsigned switch_target(const AmxProgram &p, unsigned address, std::int32_t value,
                       bool require_case = false) {
    auto it = p.instructions.find(address);
    require(it != p.instructions.end() && it->second.name == "casetbl", "Invalid AMX switch table");
    auto &args = it->second.operands;
    for (unsigned k = 0; k < unsigned(args[0]); ++k)
        if (args[2 + 2 * k] == value)
            return relative_target(address + (3 + 2 * k) * 4, args[3 + 2 * k]);
    require(!require_case, "This script ID has no dispatcher case in the resolved program.");
    return relative_target(address + 4, args[1]);
}
}
std::string interaction_native_name(unsigned hash) {
    return native_name(hash);
}
InteractionInspection
trace_interaction(const AmxProgram &p, unsigned script, unsigned event,
                  const std::vector<std::string> &messages,
                  const std::function<std::vector<std::string>(bool, unsigned)> &load_messages,
                  const std::string &message_source) {
    InteractionInspection out;
    out.listing = p.listing();
    auto variable = p.variables.find(pawn_name_hash("g_mode"));
    require(variable != p.variables.end(), "Zone script has no resolved interaction selector");
    auto it = p.instructions.find(p.entry);
    require(it != p.instructions.end() && it->second.name == "proc",
            "Unrecognized zone script entry");
    it = p.instructions.find(it->second.next);
    require(it != p.instructions.end() &&
                (it->second.name == "load.p.pri" || it->second.name == "load.pri") &&
                unsigned(it->second.operands[0]) == variable->second,
            "Unrecognized interaction dispatcher load");
    it = p.instructions.find(it->second.next);
    require(it != p.instructions.end(), "Missing interaction dispatcher");
    if (it->second.name == "switch")
        out.entry = switch_target(p, relative_target(it->first, it->second.operands[0]),
                                  std::int32_t(script), true);
    else if (it->second.name == "eq.c.pri" || it->second.name == "eq.p.c.pri") {
        const bool equal = it->second.operands.at(0) == std::int32_t(script);
        it = p.instructions.find(it->second.next);
        require(it != p.instructions.end() &&
                    (it->second.name == "jzer" || it->second.name == "jnz"),
                "Unrecognized interaction comparison branch");
        const bool taken = it->second.name == "jzer" ? !equal : equal;
        out.entry = taken ? relative_target(it->first, it->second.operands.at(0)) : it->second.next;
    } else
        throw std::runtime_error("Unrecognized interaction dispatcher");
    std::set<unsigned> active;
    unsigned steps = 0;
    bool partial = false, message_context = true, context_uncertain = false;
    auto current_messages = messages;
    std::string current_message_source = message_source;
    auto add = [&](unsigned address, std::string title, std::string detail,
                   bool recognized = false) {
        require(out.actions.size() < 512, "Interaction exceeds inspection action limit");
        out.actions.push_back({address, std::move(title), std::move(detail), recognized});
    };
    std::function<Value(unsigned, const std::vector<Value> &)> trace;
    trace = [&](unsigned entry, const std::vector<Value> &args) -> Value {
        if (active.size() >= 12 || active.contains(entry)) {
            partial = true;
            context_uncertain = true;
            add(entry, "Unresolved helper", "Recursive or deep call; this helper is not expanded.");
            return {};
        }
        active.insert(entry);
        struct Exit {
            std::set<unsigned> &active;
            unsigned address;
            ~Exit() {
                active.erase(address);
            }
        } exit{active, entry};
        std::map<int, Value> frame;
        for (unsigned k = 0; k < args.size(); ++k)
            frame[12 + 4 * int(k)] = args[k];
        int sp = 0;
        Value pri, alt;
        std::set<unsigned> visited;
        auto get = [&](int offset) -> Value {
            auto v = frame.find(offset);
            return v == frame.end() ? Value{} : v->second;
        };
        auto push = [&](Value v) {
            require(sp > -4096, "Interaction stack limit exceeded");
            sp -= 4;
            frame[sp] = v;
        };
        auto pop = [&]() {
            require(sp < 0, "Invalid interaction argument stack");
            auto v = get(sp);
            frame.erase(sp);
            sp += 4;
            return v;
        };
        for (unsigned pc = entry;;) {
            require(++steps <= 16384, "Interaction exceeds analysis step limit");
            auto found = p.instructions.find(pc);
            require(found != p.instructions.end(),
                    "Interaction points outside decoded instructions");
            auto &i = found->second;
            if (!visited.insert(pc).second) {
                partial = true;
                context_uncertain = true;
                add(pc, "Runtime loop", "Repeated control flow is not executed by the inspector.");
                return {};
            }
            auto op = i.name;
            auto a = i.operands;
            auto next = i.next;
            auto operand = [&](unsigned k = 0) {
                require(k < a.size(), "Missing AMX operand");
                return a[k];
            };
            auto stop = [&](const std::string &why) {
                partial = true;
                context_uncertain = true;
                add(pc, "Unresolved control flow",
                    why + " Later caller steps require this helper to return.");
            };
            if (op == "proc" || op == "nop" || op == "break") {
            } else if (op == "retn" || op == "ret")
                return pri;
            else if (op == "const.pri" || op == "const.p.pri")
                pri = operand();
            else if (op == "const.alt" || op == "const.p.alt")
                alt = operand();
            else if (op == "addr.pri" || op == "addr.p.pri" || op == "addr.alt" ||
                     op == "addr.p.alt") {
                Value address(operand());
                address.local = true;
                address.owner = entry;
                if (op == "addr.pri" || op == "addr.p.pri")
                    pri = address;
                else
                    alt = address;
            } else if (op == "movs") {
                auto count = operand();
                if (!pri || pri.local || !alt || !alt.local || alt.owner != entry || *pri < 0 ||
                    *pri % 4 || *alt % 4 || count < 0 || count % 4 || count > 4096 ||
                    std::int64_t(*alt) + count > 0 || *alt < sp ||
                    std::uint64_t(*pri) + count > p.data.size() * 4) {
                    stop("Memory copy is outside the supported constant-to-local range.");
                    return {};
                }
                for (int k = 0; k < count; k += 4)
                    frame[*alt + k] = p.data[unsigned(*pri + k) / 4];
            } else if (op == "load.i") {
                if (!pri) {
                    pri = {};
                } else if (pri.local) {
                    if (pri.owner != entry) {
                        stop("A by-reference value belongs to another helper.");
                        return {};
                    }
                    pri = get(*pri);
                } else if (*pri >= 0 && *pri % 4 == 0 && std::size_t(*pri) / 4 < p.data.size())
                    pri = p.data[unsigned(*pri) / 4];
                else {
                    stop("Indirect read is outside the constant data segment.");
                    return {};
                }
            } else if (op == "smul.c" || op == "smul.p.c")
                pri = pri && !pri.local ? Value(std::int32_t(unsigned(*pri) * unsigned(operand())))
                                        : Value{};
            else if (op == "add")
                pri = sum(pri, alt);
            else if (op == "idxaddr.b" || op == "idxaddr.p.b") {
                auto shift = operand();
                require(shift >= 0 && shift < 32, "Invalid AMX index shift");
                pri = pri && !pri.local ? sum(alt, Value(std::int32_t(unsigned(*pri) << shift)))
                                        : Value{};
            } else if (op == "zero.pri")
                pri = 0;
            else if (op == "zero.alt")
                alt = 0;
            else if (op == "push" || op == "push.p")
                push(unsigned(operand()) == variable->second ? Value(std::int32_t(script))
                                                             : Value{});
            else if (op == "load.s.pri" || op == "load.p.s.pri")
                pri = get(operand());
            else if (op == "load.s.alt" || op == "load.p.s.alt")
                alt = get(operand());
            else if (op == "stor.s.pri" || op == "stor.p.s.pri")
                frame[operand()] = pri;
            else if (op == "stor.s.alt" || op == "stor.p.s.alt")
                frame[operand()] = alt;
            else if (op == "const.s")
                frame[operand()] = operand(1);
            else if (op == "zero.s" || op == "zero.p.s")
                frame[operand()] = 0;
            else if (op == "move.pri")
                pri = alt;
            else if (op == "move.alt")
                alt = pri;
            else if (op == "push.pri")
                push(pri);
            else if (op == "push.alt")
                push(alt);
            else if (op == "pop.pri")
                pri = pop();
            else if (op == "pop.alt")
                alt = pop();
            else if (op == "push.c" || op == "push.p.c" || op == "push2.c" || op == "push3.c" ||
                     op == "push4.c" || op == "push5.c")
                for (auto v : a)
                    push(v);
            else if (op == "push.s" || op == "push.p.s" || op == "push2.s" || op == "push3.s" ||
                     op == "push4.s" || op == "push5.s")
                for (auto v : a)
                    push(get(v));
            else if (op == "stack" || op == "stack.p") {
                auto n = operand();
                require(n % 4 == 0 && n >= -4096 && n <= 4096,
                        "Unsupported interaction stack adjustment");
                if (n < 0)
                    for (int k = 0; k < -n; k += 4)
                        push({});
                else
                    for (int k = 0; k < n; k += 4)
                        pop();
            } else if (op == "and")
                pri = pri && alt && !pri.local && !alt.local ? Value(*pri & *alt) : Value{};
            else if (op == "or")
                pri = pri && alt && !pri.local && !alt.local ? Value(*pri | *alt) : Value{};
            else if (op == "not")
                pri = pri ? Value(!*pri) : Value{};
            else if (op == "add.c" || op == "add.p.c")
                pri = sum(pri, Value(operand()));
            else if (op == "eq.c.pri" || op == "eq.p.c.pri")
                pri = pri ? Value(*pri == operand()) : Value{};
            else if (op == "jump")
                next = relative_target(pc, operand());
            else if (op == "jzer" || op == "jnz" || op == "jeq" || op == "jneq") {
                Value condition;
                if (op == "jzer" || op == "jnz") {
                    if (pri && !pri.local)
                        condition = op == "jzer" ? !*pri : bool(*pri);
                } else if (pri && alt && !pri.local && !alt.local)
                    condition = op == "jeq" ? *pri == *alt : *pri != *alt;
                if (!condition) {
                    stop("Branch depends on runtime state; alternatives are available in AMX.");
                    return {};
                }
                if (*condition)
                    next = relative_target(pc, operand());
            } else if (op == "switch") {
                if (!pri || pri.local) {
                    stop("Switch depends on runtime state.");
                    return {};
                }
                next = switch_target(p, relative_target(pc, operand()), *pri);
            } else if (op == "call" || op == "sysreq.n") {
                Value count = op == "call" ? pop() : Value(operand(1));
                require(count && !count.local && *count >= 0 && *count <= 128 * 4 &&
                            *count % 4 == 0,
                        "Unresolved AMX argument count");
                std::vector<Value> values;
                for (int k = 0; k < *count; k += 4)
                    values.push_back(pop());
                if (op == "call")
                    pri = trace(relative_target(pc, operand()), values);
                else {
                    require(unsigned(operand()) < p.natives.size(),
                            "Native reference is out of bounds");
                    auto name = native_name(p.natives[operand()]);
                    std::ostringstream detail;
                    detail << name << "(";
                    for (unsigned k = 0; k < values.size(); ++k) {
                        if (k)
                            detail << ", ";
                        detail << value_text(values[k]);
                    }
                    detail << ")";
                    auto arg = [&](unsigned k) -> Value {
                        return k < values.size() && !values[k].local ? values[k] : Value{};
                    };
                    std::string title = name;
                    bool known = false;
                    if (name == "TalkMsg_Seq") {
                        title = "Show dialogue";
                        known = true;
                        auto id = arg(2);
                        if (message_context && arg(9) && !*arg(9) && id && *id >= 0 &&
                            std::size_t(*id) < current_messages.size()) {
                            detail << "\nMessage " << *id << ": " << current_messages[*id] << "\n"
                                   << current_message_source;
                            if (context_uncertain)
                                detail << "\nCandidate text: an unresolved helper may change the "
                                          "message context.";
                        } else
                            detail << "\nMessage text could not be resolved in the current context "
                                      "(including loaded message buffers).";
                        current_messages = messages;
                        current_message_source = message_source;
                        message_context = true;
                    } else if (name == "FlagGet" || name == "FlagSet" || name == "FlagReset") {
                        title = std::string(name == "FlagGet"   ? "Read flag "
                                            : name == "FlagSet" ? "Set flag "
                                                                : "Clear flag ") +
                                value_text(arg(0));
                        known = true;
                    } else if (name == "WorkGet") {
                        title = "Read story variable " + value_text(arg(0));
                        known = true;
                    } else if (name == "WorkSet") {
                        title = "Set story variable " + value_text(arg(0)) + " to " +
                                value_text(arg(1));
                        known = true;
                    } else if (name == "LastKeyWait_" || name == "ABKeyWait_") {
                        title = "Wait for button input";
                        known = true;
                    } else if (name == "MsgWinCloseNo") {
                        title = "Close dialogue window";
                        known = true;
                    } else if (name == "SEPlay") {
                        title = "Play sound";
                        known = true;
                    } else if (name == "GlobalCall") {
                        title = "Call shared script " + value_text(arg(0));
                        partial = true;
                        message_context = false;
                    } else if (name == "ExtraMsgInstant_") {
                        title = "Change message context";
                        known = true;
                        message_context = false;
                        auto archive = arg(0), member = arg(1);
                        if (archive && !archive.local && member && !member.local && *member >= 0 &&
                            load_messages) {
                            try {
                                current_messages = load_messages(*archive != 0, unsigned(*member));
                                current_message_source =
                                    std::string(*archive ? "Script" : "General") +
                                    " message member " + std::to_string(*member);
                                detail << "\n" << current_message_source;
                                message_context = true;
                            } catch (const std::exception &e) {
                                detail << "\nMessage source unavailable: " << e.what();
                                partial = true;
                            }
                        } else
                            partial = true;
                    }
                    add(pc, title, detail.str(), known);
                    for (const auto &value : values)
                        out.actions.back().arguments.push_back(
                            value && !value.local ? std::optional<std::int32_t>(*value)
                                                  : std::nullopt);
                    pri = name == "GetTargetActorEventId" ? Value(std::int32_t(event)) : Value{};
                    if (name == "TrainerIDGet" && arg(0) && *arg(0) >= 1000 && *arg(0) < 3000)
                        pri = *arg(0) - 1000;
                }
            } else {
                stop("Instruction " + op + " is not interpreted.");
                return {};
            }
            pc = next;
        }
    };
    trace(out.entry, {});
    out.notice = partial ? "Partial static trace. Unresolved helpers and branches are shown; later "
                           "caller actions depend on those helpers returning."
                         : "Static call trace for this dispatcher case. Native functions are "
                           "identified, not executed.";
    return out;
}
InteractionInspection inspect_interaction(const std::filesystem::path &dump,
                                          const ArchiveSources &archives, unsigned area,
                                          unsigned local_zone, int zone, unsigned script,
                                          unsigned event) {
    auto resolved = resolve_interaction_source(dump, archives, area, local_zone, zone, script);
    auto program = decode_field_amx(resolved.program);
    auto message_member = resolved.message_member;
    std::vector<std::string> messages;
    std::string message_notice;
    try {
        Archive text(dump / resolved.messages);
        messages = decode_location_text(text.decoded(message_member));
    } catch (const std::exception &e) {
        message_notice = " Message preview unavailable: " + std::string(e.what());
    }
    InteractionInspection out;
    try {
        out = trace_interaction(
            program, script, event, messages,
            [&](bool script_messages, unsigned selected_member) {
                Archive text(dump / (script_messages ? TargetProfile::interaction_text_archive
                                                     : TargetProfile::location_text_archive));
                return decode_location_text(text.decoded(selected_member));
            },
            std::string(resolved.shared ? "Shared script" : "Zone") + " message member " +
                std::to_string(message_member));
    } catch (const std::exception &e) {
        out.listing = program.listing();
        out.notice = std::string("Cannot summarize this interaction: ") + e.what();
    }
    out.message_member = message_member;
    out.source = (resolved.shared ? "Shared script member " : "Field member ") +
                 std::to_string(resolved.member) +
                 (resolved.shared ? "" : " / zone script " + std::to_string(local_zone)) +
                 " / message member " + std::to_string(message_member);
    out.notice += message_notice;
    return out;
}
}
