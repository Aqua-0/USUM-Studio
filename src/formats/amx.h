#pragma once
#include "core/binary.h"
#include <map>
#include <string>
namespace studio {
struct AmxInstruction {
    unsigned address = 0, next = 0;
    std::string name;
    std::vector<std::int32_t> operands;
};
struct AmxProgram {
    unsigned entry = 0;
    std::map<unsigned, AmxInstruction> instructions;
    std::vector<unsigned> natives;
    std::map<unsigned, unsigned> variables;
    std::vector<std::int32_t> data;
    std::string listing() const;
};
unsigned pawn_name_hash(const std::string &name);
AmxProgram decode_field_amx(View bytes);
}
