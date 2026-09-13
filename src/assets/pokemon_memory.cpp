#include "assets/pokemon_memory.h"
namespace studio {
PokemonMemoryEstimate
estimate_pokemon_summary_memory(const PokemonEntry &entry,
                                const std::function<std::size_t(std::size_t)> &size) {
    PokemonMemoryEstimate result;
    constexpr unsigned xerneas = 716, kyurem = 646;
    result.refresh_motions = entry.species == xerneas ||
                             (entry.species == kyurem && (entry.form == 1 || entry.form == 2));
    result.model = size(entry.model_member);
    result.normal_textures = size(entry.texture_member + 1);
    result.shiny_textures = size(entry.texture_member + 2);
    result.motions = size(entry.motion_member +
                          TargetProfile::pokemon_motion_slots[result.refresh_motions ? 1 : 0]);
    result.settings = size(1 + entry.data_index * TargetProfile::pokemon_stride +
                           TargetProfile::pokemon_settings_slot);
    return result;
}
}
