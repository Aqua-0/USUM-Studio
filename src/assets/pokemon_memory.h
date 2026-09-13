#pragma once
#include "assets/pokemon_catalog.h"
#include <functional>
namespace studio {
struct PokemonMemoryEstimate {
    static constexpr std::size_t heap = 0x340000, warning_margin = 0x40000;
    std::size_t model = 0, normal_textures = 0, shiny_textures = 0, motions = 0, settings = 0;
    bool refresh_motions = false;
    std::size_t total(bool shiny) const {
        return model + (shiny ? shiny_textures : normal_textures) + motions + settings;
    }
    bool exceeds() const {
        return total(false) >= heap || total(true) >= heap;
    }
    bool warning() const {
        return total(false) >= heap - warning_margin || total(true) >= heap - warning_margin;
    }
};
PokemonMemoryEstimate
estimate_pokemon_summary_memory(const PokemonEntry &,
                                const std::function<std::size_t(std::size_t)> &decoded_size);
}
