#include "formats/amx.h"
#include <array>
#include <sstream>
#include <iomanip>
namespace studio {
unsigned pawn_name_hash(const std::string &name) {
    unsigned hash = 0;
    for (unsigned char c : name)
        hash = hash * 131u ^ c;
    return hash;
}
AmxProgram decode_field_amx(View b) {
    slice(b, 0, 60);
    require(u16(b, 4) == 0xf1e0 && b[6] == 10 && b[7] == 10,
            "Only field AMX version 10 is supported");
    require(u16(b, 10) == 8 && !(u16(b, 8) & 1), "Unsupported AMX symbol or overlay layout");
    auto size = u32(b, 0), cod = u32(b, 12), dat = u32(b, 16), hea = u32(b, 20);
    require(cod >= 60 && cod <= size && size <= b.size() && cod <= dat && dat <= hea &&
                hea <= 16 * 1024 * 1024 && (dat - cod) % 4 == 0 && (hea - dat) % 4 == 0,
            "Invalid AMX segment bounds");
    AmxProgram out;
    out.entry = u32(b, 28);
    std::array<unsigned, 7> tables{};
    for (unsigned i = 0; i < 7; ++i)
        tables[i] = u32(b, 32 + i * 4);
    for (unsigned i = 0; i < 7; ++i)
        require(tables[i] >= 60 && tables[i] <= cod && (!i || tables[i] >= tables[i - 1]),
                "Invalid AMX symbol tables");
    auto symbols = [&](unsigned first, unsigned last, auto accept) {
        require((last - first) % 8 == 0, "Invalid AMX symbol record size");
        for (unsigned p = first; p < last; p += 8)
            accept(u32(b, p), u32(b, p + 4));
    };
    symbols(tables[1], tables[2], [&](unsigned, unsigned hash) {
        out.natives.push_back(hash);
    });
    symbols(tables[3], tables[4], [&](unsigned address, unsigned hash) {
        require(address % 4 == 0 && address < hea - dat, "Invalid AMX public variable");
        require(out.variables.emplace(hash, address).second, "Duplicate AMX public variable hash");
    });
    std::vector<std::int32_t> cells;
    if (u16(b, 8) & 4) {
        std::int64_t value = 0;
        unsigned bytes = 0;
        for (unsigned p = cod; p < size; ++p) {
            auto byte = b[p];
            if (!bytes)
                value = (byte & 64) ? -1 : 0;
            require(++bytes <= 5, "Overlong compact AMX cell");
            value = value * 128 + (byte & 127);
            if (!(byte & 128)) {
                require(value >= -2147483648ll && value <= 4294967295ll,
                        "Compact AMX cell exceeds 32 bits");
                require(cells.size() < (hea - cod) / 4, "AMX compact data exceeds declared size");
                cells.push_back(std::int32_t(value));
                bytes = 0;
            }
        }
        require(!bytes && cells.size() == (hea - cod) / 4, "Truncated compact AMX data");
    } else {
        require(hea <= size, "Truncated uncompressed AMX data");
        for (unsigned p = cod; p < hea; p += 4)
            cells.push_back(std::int32_t(u32(b, p)));
    }
    const unsigned code_cells = (dat - cod) / 4;
    out.data.assign(cells.begin() + code_cells, cells.end());
    struct Opcode {
        const char *name;
        int width;
    };
    static const Opcode opcodes[] = {
#include "formats/amx_opcodes.inc"
    };
    for (unsigned i = 0; i < code_cells;) {
        unsigned word = unsigned(cells[i]), op = word & 65535;
        require(op > 0 && op < std::size(opcodes), "Unknown AMX opcode");
        auto spec = opcodes[op];
        AmxInstruction instruction;
        instruction.address = i * 4;
        instruction.name = spec.name;
        unsigned width = 0;
        if (spec.width == -1)
            instruction.operands.push_back(std::int16_t(word >> 16));
        else {
            width = unsigned(spec.width);
            if (spec.width == -2) {
                require(i + 1 < code_cells && cells[i + 1] >= 0 && cells[i + 1] <= 65536,
                        "Invalid AMX case table");
                width = 2 + 2 * unsigned(cells[i + 1]);
            }
            require(width <= code_cells - i - 1, "Truncated AMX instruction");
            instruction.operands.assign(cells.begin() + i + 1, cells.begin() + i + 1 + width);
        }
        i += 1 + width;
        instruction.next = i * 4;
        out.instructions.emplace(instruction.address, std::move(instruction));
    }
    require(out.instructions.contains(out.entry), "Invalid AMX entry point");
    return out;
}
std::string AmxProgram::listing() const {
    std::ostringstream out;
    out << "Read-only AMX disassembly; offsets are relative to the code segment.\n";
    for (auto &[address, i] : instructions) {
        out << std::hex << std::setw(6) << std::setfill('0') << address << "  " << i.name;
        for (auto operand : i.operands)
            out << " " << std::dec << operand;
        if (i.name == "sysreq.n" || i.name == "sysreq.c")
            if (!i.operands.empty() && unsigned(i.operands[0]) < natives.size())
                out << "  ; native hash 0x" << std::hex << natives[i.operands[0]];
        out << '\n';
    }
    return out.str();
}
}
