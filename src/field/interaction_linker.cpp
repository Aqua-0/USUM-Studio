#include "field/interaction_linker.h"
#include <algorithm>
#include <limits>
#include <set>
namespace studio {
namespace {
struct Opcode {
    const char *name;
    int width;
};
const Opcode codes[] = {
#include "formats/amx_opcodes.inc"
};
unsigned opcode(const std::string &name) {
    for (unsigned i = 1; i < std::size(codes); ++i)
        if (name == codes[i].name)
            return i;
    throw std::runtime_error("Unknown AMX instruction: " + name);
}
std::vector<std::int32_t> encode(const AmxProgram &p) {
    std::vector<std::int32_t> out;
    for (const auto &[at, i] : p.instructions) {
        require(at == out.size() * 4, "Noncontiguous AMX instructions");
        auto op = opcode(i.name);
        if (codes[op].width == -1)
            out.push_back(std::int32_t(op | (unsigned(i.operands.at(0)) << 16)));
        else {
            out.push_back(std::int32_t(op));
            out.insert(out.end(), i.operands.begin(), i.operands.end());
        }
    }
    return out;
}
unsigned destination(unsigned at, std::int32_t relative) {
    auto value = std::int64_t(at) + relative;
    require(value >= 0 && value <= std::numeric_limits<std::int32_t>::max(),
            "Invalid AMX branch destination");
    return unsigned(value);
}
const AmxInstruction &dispatcher(const AmxProgram &p) {
    auto i = p.instructions.at(p.entry);
    require(i.name == "proc", "Zone script has no supported entry");
    i = p.instructions.at(i.next);
    auto mode = p.variables.find(pawn_name_hash("g_mode"));
    require(mode != p.variables.end() && (i.name == "load.pri" || i.name == "load.p.pri") &&
                unsigned(i.operands.at(0)) == mode->second,
            "Zone script has no supported interaction selector");
    i = p.instructions.at(i.next);
    require(i.name == "switch", "Zone script has no supported dispatcher");
    auto &table = p.instructions.at(destination(i.address, i.operands.at(0)));
    require(table.name == "casetbl", "Invalid interaction case table");
    return table;
}
void append_cells(Bytes &out, const std::vector<std::int32_t> &cells, bool compact) {
    for (auto value : cells) {
        if (!compact) {
            append32(out, unsigned(value));
            continue;
        }
        Bytes encoded;
        for (;;) {
            auto byte = unsigned(value) & 127;
            value >>= 7;
            encoded.push_back(std::uint8_t(byte));
            if ((value == 0 && !(byte & 64)) || (value == -1 && (byte & 64)))
                break;
        }
        for (auto it = encoded.rbegin(); it != encoded.rend(); ++it)
            out.push_back(*it | (it + 1 != encoded.rend() ? 128 : 0));
    }
}
}
InteractionLinkResult link_authored_interaction(View original, View compiled, unsigned script,
                                                std::optional<TrainerInteractionTarget> trainer) {
    require(trainer ? (script > 1000 && script < 3000) : (script > 0 && script < 256),
            "Choose a supported interaction selector");
    auto base = decode_field_amx(original), authored = decode_field_amx(compiled);
    require(authored.data.empty(),
            "Authored Pawn uses global/static data; relocation is not supported yet");
    require(u32(compiled, 32) == u32(compiled, 36) && u32(compiled, 40) == u32(compiled, 44) &&
                u32(compiled, 44) == u32(compiled, 48) && u32(compiled, 48) == u32(compiled, 52),
            "Authored Pawn must not declare public functions, public variables, libraries or tags");
    require(!(u16(original, 8) & 2) && !(u16(compiled, 8) & 2),
            "Debug AMX records are not supported by the interaction linker");
    require(u32(original, 24) >= u32(original, 20) && u32(compiled, 24) >= u32(compiled, 20) &&
                u32(compiled, 24) - u32(compiled, 20) <= u32(original, 24) - u32(original, 20),
            "Authored Pawn requests more stack/heap memory than the zone provides");
    require(authored.instructions.at(authored.entry).name == "proc",
            "Authored Pawn main is not callable");
    const auto *table_ptr = trainer ? nullptr : &dispatcher(base);
    auto code = encode(base), extra = encode(authored);
    unsigned start = narrow(code.size() * 4);
    auto natives = base.natives;
    std::vector<unsigned> native_map;
    InteractionLinkResult result;
    for (auto hash : authored.natives) {
        auto it = std::find(natives.begin(), natives.end(), hash);
        if (it == natives.end()) {
            native_map.push_back(narrow(natives.size()));
            natives.push_back(hash);
            result.added_natives.push_back(hash);
        } else
            native_map.push_back(unsigned(it - natives.begin()));
    }
    auto native = [&](const char *name) {
        auto hash = pawn_name_hash(name);
        auto found = std::find(natives.begin(), natives.end(), hash);
        if (found != natives.end())
            return unsigned(found - natives.begin());
        result.added_natives.push_back(hash);
        natives.push_back(hash);
        return unsigned(natives.size() - 1);
    };
    const std::set<std::string> local = {
        "proc",       "zero.pri",   "zero.alt",   "retn",       "push.c",      "push.s",
        "push.pri",   "push.alt",   "pop.pri",    "pop.alt",    "inc.s",       "dec.s",
        "load.s.pri", "load.s.alt", "stor.s.pri", "stor.s.alt", "load.s.both", "const.pri",
        "const.alt",  "const.s",    "stack",      "zero.s",     "eq.c.pri",    "eq.c.alt",
        "eq",         "neq",        "not",        "move.alt",   "move.pri",    "xchg",
        "add.c",      "add",        "sub",        "sub.alt",    "smul",        "neg",
        "and",        "or",         "xor",        "invert",     "sless",       "sleq",
        "sgrtr",      "sgeq",       "push2.c",    "push3.c",    "push4.c",     "push5.c",
        "push2.s",    "push3.s",    "push4.s",    "push5.s",    "addr.pri",    "addr.alt",
        "push.adr",   "load.i",     "fill"};
    const std::set<std::string> branches = {"jump",   "jzer",  "jnz",    "jeq",  "jneq",
                                            "jsless", "jsleq", "jsgrtr", "jsgeq"};
    for (const auto &[at, i] : authored.instructions) {
        auto operation = i.name;
        if (auto packed = operation.find(".p."); packed != std::string::npos)
            operation.erase(packed, 2);
        else if (operation.ends_with(".p"))
            operation.resize(operation.size() - 2);
        if (i.name == "sysreq.n") {
            require(i.operands[0] >= 0 && unsigned(i.operands[0]) < native_map.size() &&
                        i.operands[1] >= 0 && i.operands[1] % 4 == 0,
                    "Invalid authored native call");
            extra.at(at / 4 + 1) = std::int32_t(native_map[unsigned(i.operands[0])]);
        } else if (i.name == "call" || branches.contains(i.name) || i.name == "switch") {
            auto target = destination(at, i.operands.at(0));
            require(authored.instructions.contains(target),
                    "Authored branch leaves its code segment");
            if (i.name == "call")
                require(authored.instructions.at(target).name == "proc",
                        "Authored call is not a function");
            if (i.name == "switch")
                require(authored.instructions.at(target).name == "casetbl",
                        "Invalid authored switch");
        } else if (i.name == "casetbl") {
            require(authored.instructions.contains(destination(at + 4, i.operands.at(1))),
                    "Invalid authored default branch");
            for (int n = 0; n < i.operands[0]; ++n)
                require(authored.instructions.contains(
                            destination(at + unsigned(3 + 2 * n) * 4, i.operands.at(3 + 2 * n))),
                        "Invalid authored case branch");
        } else if (operation == "halt") {
            require(i.operands.at(0) == 12 || (at == 0 && i.operands.at(0) == 0),
                    "Unsupported authored halt");
        } else
            require(local.contains(operation), "Unsupported authored instruction: " + i.name);
    }
    code.insert(code.end(), extra.begin(), extra.end());
    auto emit = [&](const char *name, std::initializer_list<std::int32_t> args = {}) {
        unsigned at = narrow(code.size() * 4);
        code.push_back(std::int32_t(opcode(name)));
        code.insert(code.end(), args);
        return at;
    };
    auto entry = emit("proc");
    if (trainer) {
        require(base.variables.contains(pawn_name_hash("g_mode")),
                "Trainer program has no selector");
        std::vector<unsigned> skip;
        emit("load.pri", {std::int32_t(base.variables.at(pawn_name_hash("g_mode")))});
        emit("eq.c.pri", {std::int32_t(script)});
        skip.push_back(emit("jzer", {0}));
        emit("push.c", {1});
        emit("sysreq.n", {std::int32_t(native("TrainerGetScrID")), 4});
        skip.push_back(emit("jnz", {0}));
        emit("sysreq.n", {std::int32_t(native("PlayerGetZoneID")), 0});
        emit("eq.c.pri", {std::int32_t(trainer->zone)});
        skip.push_back(emit("jzer", {0}));
        emit("push.c", {0});
        emit("sysreq.n", {std::int32_t(native("TrainerGetEventID")), 4});
        emit("eq.c.pri", {std::int32_t(trainer->event)});
        skip.push_back(emit("jzer", {0}));
        result.handler = emit("push.c", {0});
        auto call = emit("call", {0});
        code.at(call / 4 + 1) = std::int32_t(start + authored.entry) - std::int32_t(call);
        emit("retn");
        auto fallback = emit("push.c", {0});
        for (auto branch : skip)
            code.at(branch / 4 + 1) = std::int32_t(fallback) - std::int32_t(branch);
        call = emit("call", {0});
        code.at(call / 4 + 1) = std::int32_t(base.entry) - std::int32_t(call);
        emit("retn");
    } else {
        const auto &table = *table_ptr;
        emit("load.pri", {std::int32_t(base.variables.at(pawn_name_hash("g_mode")))});
        auto select = emit("switch", {0});
        result.handler = narrow(code.size() * 4);
        emit("push.c", {0});
        auto call = emit("call", {0});
        code.at(call / 4 + 1) = std::int32_t(start + authored.entry) - std::int32_t(call);
        emit("zero.pri");
        emit("retn");
        std::map<std::int32_t, unsigned> cases;
        for (int n = 0; n < table.operands[0]; ++n) {
            auto target =
                destination(table.address + unsigned(3 + 2 * n) * 4, table.operands.at(3 + 2 * n));
            require(base.instructions.contains(target), "Original case target is invalid");
            require(cases.emplace(table.operands.at(2 + 2 * n), target).second,
                    "Duplicate original interaction selector");
        }
        require(!cases.contains(std::int32_t(script)),
                "Interaction ID already exists; allocate a new handler for this NPC");
        cases.emplace(std::int32_t(script), result.handler);
        auto fallback = destination(table.address + 4, table.operands.at(1));
        require(base.instructions.contains(fallback), "Original default branch is invalid");
        auto new_table = emit("casetbl");
        code.at(select / 4 + 1) = std::int32_t(new_table) - std::int32_t(select);
        code.push_back(std::int32_t(cases.size()));
        code.push_back(std::int32_t(fallback) - std::int32_t(new_table + 4));
        for (auto [id, target] : cases) {
            code.push_back(id);
            code.push_back(std::int32_t(target) - std::int32_t(code.size() * 4 - 4));
        }
    }
    auto old_cod = u32(original, 12), library = u32(original, 40);
    Bytes registrations;
    for (auto hash : result.added_natives) {
        append32(registrations, 0);
        append32(registrations, hash);
    }
    Bytes out(original.begin(), original.begin() + old_cod);
    out.insert(out.begin() + library, registrations.begin(), registrations.end());
    unsigned cod = old_cod + narrow(registrations.size()), dat = cod + narrow(code.size() * 4);
    unsigned growth = dat - u32(original, 16);
    require(std::uint64_t(u32(original, 24)) + growth <= std::numeric_limits<std::int32_t>::max(),
            "Linked AMX allocation overflows");
    for (unsigned offset = 40; offset <= 56; offset += 4)
        put32(out, offset, u32(original, offset) + narrow(registrations.size()));
    put32(out, 12, cod);
    put32(out, 16, dat);
    put32(out, 20, u32(original, 20) + growth);
    put32(out, 24, u32(original, 24) + growth);
    put32(out, 28, entry);
    for (unsigned at = u32(original, 32); at < u32(original, 36); at += 8)
        if (u32(original, at) == base.entry)
            put32(out, at, entry);
    code.insert(code.end(), base.data.begin(), base.data.end());
    append_cells(out, code, (u16(original, 8) & 4) != 0);
    if (!(u16(original, 8) & 4))
        append(out, slice(original, u32(original, 20), u32(original, 0) - u32(original, 20)));
    put32(out, 0, narrow(out.size()));
    out.resize(aligned(out.size(), 4));
    const auto check = decode_field_amx(out);
    require(check.data == base.data && check.variables == base.variables &&
                check.natives == natives,
            "Linked interaction data/interface readback failed");
    for (const auto &[at, before] : base.instructions) {
        const auto &after = check.instructions.at(at);
        require(before.name == after.name && before.operands == after.operands,
                "Linking changed existing zone instructions");
    }
    result.program = std::move(out);
    return result;
}
}
