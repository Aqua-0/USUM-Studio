#include "field/npc_dialogue.h"
#include "field/dialogue_text.h"
#include "field/map_catalog.h"
#include <algorithm>
namespace studio {
namespace {
struct Opcode {
    const char *name;
    int width;
};
const Opcode opcodes[] = {
#include "formats/amx_opcodes.inc"
};
unsigned opcode(const std::string &name) {
    for (unsigned i = 1; i < std::size(opcodes); ++i)
        if (name == opcodes[i].name)
            return i;
    throw std::runtime_error("Unknown dialogue instruction");
}
const AmxInstruction &dispatcher(const AmxProgram &p) {
    auto i = p.instructions.at(p.entry);
    require(i.name == "proc", "Zone script has no supported entry");
    i = p.instructions.at(i.next);
    require((i.name == "load.pri" || i.name == "load.p.pri") &&
                unsigned(i.operands.at(0)) == p.variables.at(pawn_name_hash("g_mode")),
            "Zone script has no supported interaction selector");
    i = p.instructions.at(i.next);
    require(i.name == "switch", "Zone script has no supported dispatcher");
    auto &table = p.instructions.at(unsigned(std::int64_t(i.address) + i.operands.at(0)));
    require(table.name == "casetbl", "Invalid zone script dispatcher");
    return table;
}
}
std::set<unsigned> zone_script_ids(View source) {
    auto p = decode_field_amx(source);
    auto &table = dispatcher(p);
    std::set<unsigned> result;
    for (int i = 0; i < table.operands[0]; ++i)
        result.insert(unsigned(table.operands[2 + i * 2]));
    return result;
}
Bytes append_npc_dialogue_script(View source, unsigned script, unsigned event,
                                 const std::vector<unsigned> &messages,
                                 std::optional<NpcBattlePrompt> battle) {
    require(script > 0 && script < 256 && !messages.empty(), "Invalid local dialogue script");
    auto p = decode_field_amx(source);
    auto table = dispatcher(p);
    require(!zone_script_ids(source).contains(script), "Dialogue script ID already exists");
    auto native = [&](const char *name) {
        auto it = std::find(p.natives.begin(), p.natives.end(), pawn_name_hash(name));
        require(it != p.natives.end(), std::string("This zone lacks dialogue support: ") + name);
        return unsigned(it - p.natives.begin());
    };
    for (auto name :
         {"TalkMsg_Seq", "TalkMsg_SeqWait", "TalkMsg_VisubleCursor", "ABKeyWait_", "MsgWinCloseNo",
          "_Suspend", "TalkMdlStartInit_", "TalkMdlEndInit_", "TalkMdlGetRepairAngle_",
          "ChrRotTarget_", "ChrRot_", "ChrActionCommandIsPlaying_", "SEPlay"})
        native(name);
    const auto original_native_count = p.natives.size();
    if (battle) {
        require(messages.size() == 1 && battle->encounter <= 65535,
                "A battle prompt needs one question and a valid encounter ID");
        for (auto name : {"YesNoWin_Seq", "WorkGet", "CallWildBattleCore", "GetWildBattleResult",
                          "ChangeEventWildLose_", "LoadFieldDLL_", "ResetEventBasePos"}) {
            auto hash = pawn_name_hash(name);
            if (std::find(p.natives.begin(), p.natives.end(), hash) == p.natives.end())
                p.natives.push_back(hash);
        }
    }
    std::vector<std::int32_t> code;
    for (auto &[address, i] : p.instructions) {
        require(address == code.size() * 4, "Noncontiguous AMX code");
        auto op = opcode(i.name);
        if (opcodes[op].width == -1)
            code.push_back(std::int32_t(op | (unsigned(i.operands.at(0)) << 16)));
        else {
            code.push_back(std::int32_t(op));
            code.insert(code.end(), i.operands.begin(), i.operands.end());
        }
    }
    auto emit = [&](const char *name, std::initializer_list<std::int32_t> args = {}) {
        unsigned at = narrow(code.size() * 4);
        code.push_back(std::int32_t(opcode(name)));
        code.insert(code.end(), args);
        return at;
    };
    auto call = [&](const char *name, std::initializer_list<std::int32_t> args) {
        for (auto it = std::rbegin(args); it != std::rend(args); ++it)
            emit("push.c", {*it});
        emit("sysreq.n", {std::int32_t(native(name)), std::int32_t(args.size() * 4)});
    };
    auto branch = [&](unsigned at, unsigned target) {
        code.at(at / 4 + 1) = std::int32_t(target) - std::int32_t(at);
    };
    auto wait_for_turn = [&] {
        auto poll = narrow(code.size() * 4);
        call("ChrActionCommandIsPlaying_", {std::int32_t(event)});
        auto ready = emit("jzer", {0});
        call("_Suspend", {1});
        emit("zero.pri");
        emit("zero.alt");
        emit("halt", {12});
        auto repeat = emit("jump", {0});
        branch(repeat, poll);
        branch(ready, narrow(code.size() * 4));
    };
    auto dialogue_sound = [&] {
        call("SEPlay", {327694, 1065353216, 0, 1065353216});
    };
    auto entry = emit("proc");
    emit("load.pri", {std::int32_t(p.variables.at(pawn_name_hash("g_mode")))});
    auto select = emit("switch", {0});
    auto handler = narrow(code.size() * 4);
    call("TalkMdlStartInit_", {std::int32_t(event), 0});
    dialogue_sound();
    call("ChrRotTarget_", {std::int32_t(event), -1, 8, 1, 1, 0});
    wait_for_turn();
    for (auto message : messages) {
        call("TalkMsg_Seq",
             {0, std::int32_t(event), std::int32_t(message), 1, 1, 0, -168, 0, -1, 0, 0});
        auto poll = narrow(code.size() * 4);
        call("TalkMsg_SeqWait", {});
        auto ready = emit("jnz", {0});
        call("_Suspend", {1});
        emit("zero.pri");
        emit("zero.alt");
        emit("halt", {12});
        auto repeat = emit("jump", {0});
        branch(repeat, poll);
        branch(ready, narrow(code.size() * 4));
        if (battle) {
            auto choice_poll = narrow(code.size() * 4);
            call("YesNoWin_Seq",
                 {1, std::int32_t(battle->yes_message), std::int32_t(battle->no_message), 0, 0});
            auto chosen = emit("jnz", {0});
            call("_Suspend", {1});
            emit("zero.pri");
            emit("zero.alt");
            emit("halt", {12});
            auto repeat_choice = emit("jump", {0});
            branch(repeat_choice, choice_poll);
            branch(chosen, narrow(code.size() * 4));
            call("WorkGet", {32784});
            emit("push.pri");
        } else {
            call("TalkMsg_VisubleCursor", {1});
            call("ABKeyWait_", {});
            call("TalkMsg_VisubleCursor", {0});
            dialogue_sound();
        }
        call("MsgWinCloseNo", {0});
    }
    call("TalkMdlEndInit_", {std::int32_t(event)});
    emit("push.c", {0});
    emit("push.c", {1});
    emit("push.c", {1});
    call("TalkMdlGetRepairAngle_", {});
    emit("push.pri");
    emit("push.c", {8});
    emit("push.c", {std::int32_t(event)});
    emit("sysreq.n", {std::int32_t(native("ChrRot_")), 24});
    wait_for_turn();
    if (battle) {
        emit("pop.pri");
        auto declined = emit("jnz", {0});
        call("LoadFieldDLL_", {});
        call("CallWildBattleCore", {std::int32_t(battle->encounter), 0, 0});
        call("GetWildBattleResult", {});
        auto survived = emit("jnz", {0});
        call("ResetEventBasePos", {});
        call("ChangeEventWildLose_", {});
        branch(declined, narrow(code.size() * 4));
        branch(survived, narrow(code.size() * 4));
    }
    emit("zero.pri");
    emit("retn");
    std::map<std::int32_t, unsigned> cases;
    for (int i = 0; i < table.operands[0]; ++i)
        cases.emplace(
            table.operands[2 + i * 2],
            unsigned(std::int64_t(table.address + (3 + i * 2) * 4) + table.operands[3 + i * 2]));
    cases.emplace(std::int32_t(script), handler);
    auto new_table = emit("casetbl");
    branch(select, new_table);
    code.push_back(std::int32_t(cases.size()));
    auto fallback = std::int64_t(table.address + 4) + table.operands[1];
    code.push_back(std::int32_t(fallback - (new_table + 4)));
    for (auto [value, target] : cases) {
        code.push_back(value);
        code.push_back(std::int32_t(target) - std::int32_t(code.size() * 4 - 4));
    }
    auto old_cod = u32(source, 12), old_dat = u32(source, 16);
    Bytes registrations;
    for (auto i = original_native_count; i < p.natives.size(); ++i) {
        append32(registrations, 0);
        append32(registrations, p.natives[i]);
    }
    auto cod = old_cod + narrow(registrations.size());
    auto dat = cod + narrow(code.size() * 4), extra = dat - old_dat;
    Bytes out(source.begin(), source.begin() + old_cod);
    out.insert(out.begin() + u32(source, 40), registrations.begin(), registrations.end());
    for (unsigned offset = 40; offset <= 56; offset += 4)
        put32(out, offset, u32(source, offset) + narrow(registrations.size()));
    put32(out, 12, cod);
    put32(out, 16, dat);
    put32(out, 20, u32(source, 20) + extra);
    put32(out, 24, u32(source, 24) + extra);
    put32(out, 28, entry);
    for (auto i = u32(source, 32); i < u32(source, 36); i += 8)
        if (u32(source, i) == p.entry)
            put32(out, i, entry);
    code.insert(code.end(), p.data.begin(), p.data.end());
    for (auto value : code) {
        if (!(u16(source, 8) & 4)) {
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
    put32(out, 0, narrow(out.size()));
    out.resize(aligned(out.size(), 4));
    auto check = decode_field_amx(out);
    require(zone_script_ids(out).size() == cases.size(), "Dialogue dispatcher readback failed");
    return out;
}
Bytes append_dialogue_message(View original, const std::string &text, bool formatted) {
    require(!text.empty(), "Enter dialogue text");
    return append_dialogue_message(original, encode_dialogue_text(text, formatted));
}
Bytes append_dialogue_message(View original, const DialogueText &text) {
    decode_location_text(original);
    const auto &words = text.words;
    require(!words.empty() && words.size() <= 65535 && words.back() == 0,
            "Invalid encoded dialogue text");
    auto count = u16(original, 2);
    require(count < 65535, "Message table is full");
    auto table_end = 20 + std::size_t(count) * 8;
    Bytes out(original.begin(), original.begin() + table_end);
    out.resize(table_end + 8);
    out.insert(out.end(), original.begin() + table_end, original.end());
    for (unsigned i = 0; i < count; ++i)
        put32(out, 20 + i * 8, u32(original, 20 + i * 8) + 8);
    put16(out, 2, std::uint16_t(count + 1));
    out.resize(aligned(out.size(), 4));
    put32(out, table_end, narrow(out.size() - 16));
    put16(out, table_end + 4, std::uint16_t(words.size()));
    std::uint16_t key = std::uint16_t(0x7c89 + count * 0x2983);
    for (auto word : words) {
        auto value = word ^ key;
        out.push_back(std::uint8_t(value));
        out.push_back(std::uint8_t(value >> 8));
        key = std::uint16_t((key << 3) | (key >> 13));
    }
    out.resize(aligned(out.size(), 4));
    put32(out, 4, narrow(out.size() - 16));
    put32(out, 16, narrow(out.size() - 16));
    require(decode_location_text(out).size() == count + 1u, "Dialogue text readback failed");
    return out;
}
}
