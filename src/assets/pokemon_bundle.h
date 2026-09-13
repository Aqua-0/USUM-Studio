#pragma once
#include "assets/pokemon_catalog.h"
namespace studio {
struct PokemonBundle {
    unsigned species = 0, form = 0;
    std::size_t data_index = 0;
    std::map<std::size_t, Bytes> members;
};
unsigned pokemon_species_rows(const Archive &archive);
PokemonBundle clone_pokemon_bundle(const Archive &archive, const PokemonEntry &donor,
                                   unsigned species, bool add_form);
void save_pokemon_bundle(const Archive &source, const PokemonBundle &bundle,
                         const std::filesystem::path &directory);
}
