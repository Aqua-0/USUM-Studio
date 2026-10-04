#pragma once
#include "scene/skeleton.h"
#include <optional>
namespace studio {
struct ModelDocument;
struct PokemonEffectPoint {
    std::size_t record = 0;
    std::string bone;
    std::array<float, 3> offset{};
    unsigned category = 0, index = 1;
    bool operator==(const PokemonEffectPoint &) const = default;
};
struct PokemonEffectPoints {
    std::size_t source = 0, child = 0;
    Bytes original;
    std::vector<PokemonEffectPoint> points;
};
const char *pokemon_effect_point_category(unsigned category);
std::string pokemon_effect_point_label(const PokemonEffectPoint &point);
std::vector<PokemonEffectPoint> decode_pokemon_effect_points(View bytes);
Bytes replace_pokemon_effect_point(View bytes, const PokemonEffectPoint &point);
Bytes add_pokemon_effect_point(View bytes, const PokemonEffectPoint &point);
Bytes remove_pokemon_effect_point(View bytes, std::size_t record);
PokemonEffectPoints pokemon_effect_points(const ModelDocument &model, unsigned group);
std::optional<std::array<float, 3>> pokemon_effect_point_position(
    const PokemonEffectPoint &point, const SceneSkeleton &rig, const std::vector<Matrix> &pose);
}
