#include "field/interaction_document.h"
#include "field/interaction_source.h"
#include "core/digest.h"
#include "formats/container.h"
#include "formats/compression.h"
#include <algorithm>
#include <set>
#include <sstream>
namespace studio {
namespace {
bool literal(const AmxInstruction &i) {
    return i.name == "push.c" || i.name == "push.p.c";
}
std::vector<std::string> labels(const std::string &name) {
    if (name == "FlagGet" || name == "FlagSet" || name == "FlagReset")
        return {"Flag ID"};
    if (name == "WorkGet")
        return {"Work variable ID"};
    if (name == "WorkSet")
        return {"Work variable ID", "Value"};
    if (name == "ChrRotTarget_" || name == "ChrFastRotTarget_")
        return {"Character event ID",
                "Target event ID (-1: player)",
                "Rotation preset (raw)",
                "Initialize idle afterward",
                "Fit turn animation to duration",
                "Initialize turn animation"};
    if (name == "ChrRot_")
        return {"Character event ID",
                "Rotation preset (raw)",
                "Angle",
                "Initialize idle afterward",
                "Fit turn animation to duration",
                "Initialize turn animation"};
    if (name == "TalkMdlStartInit_")
        return {"Character event ID", "Argument 2 (raw)"};
    if (name == "TalkMdlEndInit_" || name == "ChrActionCommandIsPlaying_")
        return {"Character event ID"};
    if (name == "TalkMsg_Seq")
        return {"Window",
                "Character event ID (0: none)",
                "Message ID",
                "Window type (raw)",
                "Window position preset (raw)",
                "Window X",
                "Window Y",
                "Window Z",
                "Timer",
                "Use loaded message buffer",
                "Argument 11 (raw)"};
    if (name == "TalkMsg_VisubleCursor")
        return {"Show cursor"};
    if (name == "MsgWinCloseNo")
        return {"Window"};
    if (name == "_Suspend")
        return {"Frames"};
    if (name == "SEPlay")
        return {"Sound ID", "Argument 2 (float bits)", "Argument 3 (raw)",
                "Argument 4 (float bits)"};
    return {};
}
unsigned target(unsigned at, int displacement) {
    auto result = std::int64_t(at) + displacement;
    require(result >= 0 && result <= 0xffffffffll, "Invalid interaction target");
    return unsigned(result);
}
}
std::vector<InteractionStep> interaction_steps(const AmxProgram &p, unsigned entry) {
    std::set<unsigned> visited;
    std::vector<unsigned> pending{entry};
    while (!pending.empty()) {
        auto at = pending.back();
        pending.pop_back();
        if (!visited.insert(at).second)
            continue;
        require(visited.size() <= 16384, "Interaction graph exceeds inspection limit");
        auto found = p.instructions.find(at);
        require(found != p.instructions.end(), "Interaction branch points outside instructions");
        auto &i = found->second;
        auto &op = i.name;
        auto add = [&](unsigned next) {
            pending.push_back(next);
        };
        if (op == "casetbl") {
            add(target(at + 4, i.operands.at(1)));
            for (int k = 0; k < i.operands.at(0); ++k)
                add(target(at + (3 + 2 * k) * 4, i.operands.at(3 + 2 * k)));
            continue;
        }
        if (op == "switch" || op == "jump" || op == "call" || op.starts_with("j")) {
            if (op == "jump.pri")
                continue;
            add(target(at, i.operands.at(0)));
            if (op == "jump" || op == "switch")
                continue;
        }
        if (op == "ret" || op == "retn" || op == "iretn" || op == "sctrl")
            continue;
        if ((op == "halt" || op == "halt.p") && i.operands.at(0) != 12)
            continue;
        if (p.instructions.contains(i.next))
            add(i.next);
    }
    std::vector<InteractionStep> out;
    std::set<unsigned> branch_targets;
    for (auto at : visited) {
        auto &i = p.instructions.at(at);
        if ((i.name == "call" || i.name == "switch" || i.name.starts_with("j")) &&
            !i.operands.empty())
            branch_targets.insert(target(at, i.operands[0]));
        if (i.name == "casetbl") {
            branch_targets.insert(target(at + 4, i.operands.at(1)));
            for (int k = 0; k < i.operands.at(0); ++k)
                branch_targets.insert(target(at + (3 + 2 * k) * 4, i.operands.at(3 + 2 * k)));
        }
    }
    for (auto at : visited) {
        auto &i = p.instructions.at(at);
        InteractionStep row{at, i.name, {}, {}};
        if (i.name == "sysreq.n" || i.name == "call") {
            auto previous = p.instructions.find(at);
            bool contiguous = !branch_targets.contains(at);
            int count = 0;
            if (i.name == "sysreq.n") {
                require(unsigned(i.operands.at(0)) < p.natives.size(),
                        "Invalid interaction native");
                row.label = interaction_native_name(p.natives[i.operands[0]]);
                count = i.operands.at(1);
            } else {
                row.label = "Call helper";
                row.targets.push_back(target(at, i.operands.at(0)));
                if (previous != p.instructions.begin()) {
                    --previous;
                    if (literal(previous->second))
                        count = previous->second.operands.at(0);
                    else
                        contiguous = false;
                    if (branch_targets.contains(previous->first))
                        contiguous = false;
                }
            }
            auto names = labels(row.label);
            require(count >= 0 && count <= 512 && count % 4 == 0, "Invalid call argument count");
            for (int n = 0; n < count / 4; ++n) {
                InteractionArgument argument;
                argument.label = unsigned(n) < names.size()
                                     ? names[n]
                                     : "Argument " + std::to_string(n + 1) + " (raw)";
                argument.source = "Computed/stack value; inspect preceding instructions";
                if (contiguous && previous != p.instructions.begin()) {
                    --previous;
                    auto &push = previous->second;
                    if (literal(push)) {
                        argument.literal = push.address;
                        argument.value = push.operands.at(0);
                        argument.source = "Literal at AMX " + std::to_string(push.address);
                    } else if (push.name == "push.pri" || push.name == "push.alt")
                        argument.source = push.name == "push.pri"
                                              ? "PRI register (preceding return/computation)"
                                              : "ALT register";
                    else if (push.name == "push.p.s" || push.name == "push.s")
                        argument.source =
                            "Caller/local stack slot " + std::to_string(push.operands.at(0));
                    else
                        contiguous = false;
                    if (branch_targets.contains(push.address))
                        contiguous = false;
                }
                row.arguments.push_back(argument);
            }
        } else {
            if ((i.name == "call" || i.name == "switch" || i.name.starts_with("j")) &&
                !i.operands.empty())
                row.targets.push_back(target(at, i.operands[0]));
            if (i.name == "casetbl") {
                row.targets.push_back(target(at + 4, i.operands.at(1)));
                for (int k = 0; k < i.operands.at(0); ++k)
                    row.targets.push_back(target(at + (3 + 2 * k) * 4, i.operands.at(3 + 2 * k)));
            }
            for (auto operand : i.operands)
                row.label += " " + std::to_string(operand);
        }
        out.push_back(std::move(row));
    }
    return out;
}
std::vector<InteractionStateAccess>
interaction_state_accesses(const AmxProgram &p, const std::vector<InteractionStep> &steps) {
    std::set<unsigned> incoming;
    for (const auto &[at, instruction] : p.instructions) {
        if ((instruction.name == "call" || instruction.name == "switch" ||
             instruction.name.starts_with("j")) &&
            !instruction.operands.empty())
            incoming.insert(target(at, instruction.operands[0]));
        if (instruction.name == "casetbl") {
            incoming.insert(target(at + 4, instruction.operands.at(1)));
            for (int k = 0; k < instruction.operands.at(0); ++k)
                incoming.insert(target(at + (3 + 2 * k) * 4, instruction.operands.at(3 + 2 * k)));
        }
    }
    std::vector<InteractionStateAccess> result;
    for (const auto &step : steps) {
        auto name = step.label;
        if (name != "FlagGet" && name != "FlagSet" && name != "FlagReset" && name != "WorkGet" &&
            name != "WorkSet")
            continue;
        InteractionStateAccess access;
        access.address = step.address;
        access.work = name.starts_with("Work");
        access.write = name != "FlagGet" && name != "WorkGet";
        access.arguments = step.arguments;
        auto value = [&](unsigned index) {
            return index < step.arguments.size() && step.arguments[index].literal
                       ? std::to_string(step.arguments[index].value)
                       : std::string("[unresolved]");
        };
        std::string subject = (access.work ? "work variable " : "flag ") + value(0);
        access.title =
            access.write ? (name == "FlagReset" ? "Clear " : "Set ") + subject : "Read " + subject;
        if (name == "WorkSet")
            access.title += " to " + value(1);
        if (!access.write) {
            access.condition =
                "Result is runtime-dependent. Follow Calls & flow to inspect how it is used.";
            unsigned next = p.instructions.at(step.address).next;
            bool inverted = false, comparison = false;
            std::string predicate = subject + " != 0", inverse = subject + " == 0";
            for (unsigned n = 0; n < 3 && p.instructions.contains(next) && !incoming.contains(next);
                 ++n) {
                const auto &i = p.instructions.at(next);
                if (i.name == "not") {
                    inverted = !inverted;
                    next = i.next;
                    continue;
                }
                if ((i.name == "eq.c.pri" || i.name == "eq.p.c.pri") && !comparison && !inverted) {
                    predicate = subject + " == " + std::to_string(i.operands.at(0));
                    inverse = subject + " != " + std::to_string(i.operands.at(0));
                    comparison = true;
                    next = i.next;
                    continue;
                }
                if (i.name == "jzer" || i.name == "jnz") {
                    bool truth = (i.name == "jnz") != inverted;
                    access.condition =
                        "Jump if " + (truth ? predicate : inverse) + "; otherwise continue.";
                    access.branch = target(i.address, i.operands.at(0));
                    access.continuation = i.next;
                }
                break;
            }
        }
        result.push_back(std::move(access));
    }
    return result;
}
InteractionDocument::InteractionDocument(Bytes source)
    : source_(std::move(source)), original_(decode_field_amx(source_)), program_(original_) {
}
void InteractionDocument::refresh() {
    program_ = original_;
    for (auto [at, value] : edits_)
        program_.instructions.at(at).operands.at(0) = value;
}
void InteractionDocument::set(unsigned at, std::int32_t value) {
    const auto &i = original_.instructions.at(at);
    require(literal(i), "Only literal argument pushes can be changed");
    require(i.name != "push.p.c" || (value >= -32768 && value <= 32767),
            "This packed argument requires a value from -32768 to 32767");
    if (program_.instructions.at(at).operands.at(0) == value)
        return;
    undo_.push_back(edits_);
    redo_.clear();
    if (value == i.operands.at(0))
        edits_.erase(at);
    else
        edits_[at] = value;
    refresh();
}
bool InteractionDocument::undo() {
    if (undo_.empty())
        return false;
    redo_.push_back(edits_);
    edits_ = undo_.back();
    undo_.pop_back();
    refresh();
    return true;
}
bool InteractionDocument::redo() {
    if (redo_.empty())
        return false;
    undo_.push_back(edits_);
    edits_ = redo_.back();
    redo_.pop_back();
    refresh();
    return true;
}
Bytes InteractionDocument::compile() const {
    if (edits_.empty())
        return source_;
    struct Opcode {
        const char *name;
        int width;
    };
    static const Opcode codes[] = {
#include "formats/amx_opcodes.inc"
    };
    std::vector<std::int32_t> cells;
    for (auto &[at, i] : program_.instructions) {
        require(at == cells.size() * 4, "Noncontiguous interaction code");
        auto found = std::find_if(std::begin(codes) + 1, std::end(codes), [&](auto &c) {
            return i.name == c.name;
        });
        require(found != std::end(codes), "Unknown interaction instruction");
        unsigned opcode = unsigned(found - std::begin(codes));
        if (found->width == -1)
            cells.push_back(std::int32_t(opcode | (unsigned(i.operands.at(0)) << 16)));
        else {
            cells.push_back(std::int32_t(opcode));
            cells.insert(cells.end(), i.operands.begin(), i.operands.end());
        }
    }
    cells.insert(cells.end(), program_.data.begin(), program_.data.end());
    auto cod = u32(source_, 12);
    Bytes result(source_.begin(), source_.begin() + cod);
    for (auto value : cells) {
        if (!(u16(source_, 8) & 4)) {
            append32(result, unsigned(value));
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
            result.push_back(*it | (it + 1 != encoded.rend() ? 128 : 0));
    }
    // Preserve any declared trailer in uncompressed scripts.
    if (!(u16(source_, 8) & 4))
        append(result, slice(source_, u32(source_, 20), u32(source_, 0) - u32(source_, 20)));
    put32(result, 0, narrow(result.size()));
    append(result, slice(source_, u32(source_, 0), source_.size() - u32(source_, 0)));
    auto check = decode_field_amx(result);
    require(check.listing() == program_.listing() && check.data == original_.data,
            "Interaction writeback verification failed");
    return result;
}
std::string InteractionDocument::serialize() const {
    std::ostringstream out;
    out << "interaction 1 " << sha256(source_) << "\n";
    for (auto [at, value] : edits_)
        out << at << " " << value << "\n";
    return out.str();
}
void InteractionDocument::restore(const std::string &patch) {
    std::istringstream in(patch);
    std::string magic, hash;
    unsigned version;
    require(bool(in >> magic >> version >> hash) && magic == "interaction" && version == 1 &&
                hash == sha256(source_),
            "Interaction source changed; reopen against the matching project source");
    InteractionDocument next(source_);
    unsigned at;
    std::int32_t value;
    while (in >> std::ws && !in.eof()) {
        require(bool(in >> at >> value), "Malformed interaction argument edit");
        next.set(at, value);
    }
    edits_ = next.edits_;
    saved_ = edits_;
    undo_.clear();
    redo_.clear();
    refresh();
}
void InteractionDocument::export_to(const std::filesystem::path &source,
                                    const std::filesystem::path &output, unsigned area,
                                    unsigned zone) const {
    auto path = std::filesystem::path(GameProfile::field_archive(source));
    require(std::filesystem::absolute(source).lexically_normal() !=
                std::filesystem::absolute(output).lexically_normal(),
            "Choose a separate interaction export folder");
    Archive archive(source / path);
    auto member = std::size_t(area) * TargetProfile::area_stride + TargetProfile::zone_script_slot;
    auto container = Container::parse(archive.decoded(member), "ZS");
    require(zone < container.files.size() && container.files[zone] == source_,
            "Interaction source changed before staging");
    container.files[zone] = compile();
    auto bytes = container.write();
    if (archive.raw(member).front() == 0x11)
        bytes = compress(bytes);
    std::filesystem::create_directories((output / path).parent_path());
    archive.export_to(output / path, {{member, std::move(bytes)}});
}
void InteractionDocument::export_shared(const std::filesystem::path &source,
                                        const std::filesystem::path &output,
                                        unsigned member) const {
    require(std::filesystem::absolute(source).lexically_normal() !=
                std::filesystem::absolute(output).lexically_normal(),
            "Choose a separate shared script export folder");
    const std::filesystem::path path = TargetProfile::shared_script_archive;
    Archive archive(source / path);
    require(read_shared_script(archive, member) == source_, "Shared script changed before staging");
    auto bytes = compile();
    if (archive.raw(member) != source_)
        bytes = compress(bytes);
    std::filesystem::create_directories((output / path).parent_path());
    archive.export_to(output / path, {{member, std::move(bytes)}});
}

}
