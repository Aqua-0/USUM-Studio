#pragma once
#include "field/archive_sources.h"
#include "field/area.h"
namespace studio {
struct PokemonEntry {
    unsigned species = 0, form = 0, data_index = 0, model_member = 0, texture_member = 0,
             motion_member = 0, flags = 0;
    bool female = false;
    std::string name, label;
};
std::vector<PokemonEntry> decode_pokemon_catalog(View management, std::size_t members,
                                                 const std::vector<std::string> &names);
std::vector<PokemonEntry> load_pokemon_catalog(const std::filesystem::path &dump,
                                               const ArchiveSources &archives = {});
}
