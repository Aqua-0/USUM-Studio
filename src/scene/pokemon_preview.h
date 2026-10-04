#pragma once
#include "scene/environment.h"
#include <algorithm>
namespace studio {
enum class PokemonPreviewProfile { Studio, Summary, Battle };
struct PokemonPreviewSettings {
    PokemonPreviewProfile profile = PokemonPreviewProfile::Studio;
    unsigned environment = 0, stencil_offset = 0;
    bool shared_outline_depth() const {
        return profile != PokemonPreviewProfile::Studio;
    }
    bool battle() const {
        return profile == PokemonPreviewProfile::Battle;
    }
    float brightness() const {
        return environment == 0 ? 1.f : environment == 1 ? .9f : .7f;
    }
};
inline void apply_pokemon_battle_preview(SceneMaterial &material,
                                         const PokemonPreviewSettings &settings) {
    if (!settings.battle())
        return;
    float light = settings.brightness();
    for (unsigned stage = 0; stage < 6; ++stage)
        if (material.constant_assignments[stage] == 5)
            material.combiner.stages[stage].constant = {light, light, light, 1};
    if (material.runtime_depth_state)
        material.depth_state = *material.runtime_depth_state;
    auto reference = (material.stencil_test >> 16) & 255;
    reference = std::min(255u, reference + settings.stencil_offset);
    material.stencil_test = (material.stencil_test & ~0xff0000u) | (reference << 16);
}
}
