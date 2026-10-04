#pragma once
#include "scene/skeleton.h"
namespace studio {
struct ModelDocument;
struct PokemonDecorationPoint {
    std::string name, bone;
    Matrix local = pose_identity();
    bool operator==(const PokemonDecorationPoint &) const = default;
};
struct PokemonDecorationPoints {
    std::size_t source = 0, child = 0;
    Bytes original;
    std::vector<PokemonDecorationPoint> points;
};
Bytes replace_pokemon_decoration_point(View bytes, std::size_t record, const PokemonDecorationPoint &point);
Bytes add_pokemon_decoration_point(View bytes, const PokemonDecorationPoint &point);
Bytes remove_pokemon_decoration_point(View bytes, std::size_t record);
PokemonDecorationPoints pokemon_decoration_point_data(const ModelDocument &model, unsigned group);
std::vector<PokemonDecorationPoint> decode_pokemon_decoration_points(View bytes);
std::vector<PokemonDecorationPoint> pokemon_decoration_points(const ModelDocument &model, unsigned group);
}
