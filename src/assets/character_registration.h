#pragma once
#include "formats/archive.h"
namespace studio {
struct CharacterArchiveLayout {
    unsigned models = 0, animations = 0;
    std::vector<unsigned> animation_offsets;
};
CharacterArchiveLayout read_character_archive_layout(const Archive &archive);
struct CharacterRegistration {
    unsigned donor = 0, character = 0;
    std::string source_identity;
    Bytes replacement_model;
    std::string serialize() const;
    static CharacterRegistration parse(const std::string &text);
    bool operator==(const CharacterRegistration &) const = default;
};
CharacterRegistration plan_character_registration(const Archive &archive, unsigned donor);
void export_character_registration(const Archive &archive, const CharacterRegistration &plan,
                                   const std::filesystem::path &output);
}
