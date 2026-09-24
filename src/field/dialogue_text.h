#pragma once
#include "core/binary.h"
namespace studio {
struct DialogueText {
    std::vector<unsigned> words;
    std::vector<unsigned> species, items;
    std::vector<std::string> setup;
    std::string preview;
};
DialogueText encode_dialogue_translation(const std::string &fallback, const std::string &translation,
                                         bool formatted = false);
DialogueText encode_dialogue_text(const std::string &text, bool formatted = false);
}
