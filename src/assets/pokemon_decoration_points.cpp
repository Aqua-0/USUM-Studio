#include "assets/pokemon_decoration_points.h"
#include "assets/material_document.h"
#include "formats/container.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace studio {
std::vector<PokemonDecorationPoint> decode_pokemon_decoration_points(View bytes) {
    if (bytes.empty())
        return {};
    require(bytes.size() >= 40 && u32(bytes, 0) == 0 && u32(bytes, 4) == 1 &&
                text(slice(bytes, 8, 8)) == "locator" && bytes[15] == 0,
            "Invalid Pokemon decoration locator header");
    auto size = u32(bytes, 16), count = u32(bytes, 24);
    require(size >= 16 && size <= bytes.size() - 24 && count <= (size - 16) / 192 &&
                size == 16 + std::size_t(count) * 192,
            "Invalid Pokemon decoration locator size");
    auto name = [&](std::size_t offset) {
        auto value = slice(bytes, offset, 64);
        auto end = std::find(value.begin(), value.end(), 0);
        require(end != value.end() && end != value.begin(), "Invalid decoration locator name");
        return std::string(value.begin(), end);
    };
    std::vector<PokemonDecorationPoint> result;
    for (unsigned i = 0; i < count; ++i) {
        auto offset = 40 + std::size_t(i) * 192;
        PokemonDecorationPoint point{name(offset), name(offset + 64), {}};
        for (unsigned j = 0; j < 16; ++j) {
            point.local[j] = f32(bytes, offset + 128 + j * 4);
            require(std::isfinite(point.local[j]), "Nonfinite decoration locator transform");
        }
        result.push_back(std::move(point));
    }
    return result;
}
namespace {
void validate_decoration(const PokemonDecorationPoint &point) {
    for (const auto &name : {point.name, point.bone})
        require(!name.empty() && name.size() < 64 && name.find('\0') == std::string::npos,
                "Decoration point and bone names must contain 1 to 63 bytes");
    for (auto value : point.local)
        require(std::isfinite(value), "Decoration transforms must be finite");
}
void write_decoration(Bytes &bytes, std::size_t at, const PokemonDecorationPoint &point,
                      const PokemonDecorationPoint *previous = nullptr) {
    if (!previous || point.name != previous->name) {
        std::fill_n(bytes.begin() + at, 64, 0);
        std::copy(point.name.begin(), point.name.end(), bytes.begin() + at);
    }
    if (!previous || point.bone != previous->bone) {
        std::fill_n(bytes.begin() + at + 64, 64, 0);
        std::copy(point.bone.begin(), point.bone.end(), bytes.begin() + at + 64);
    }
    for (unsigned i = 0; i < 16; ++i)
        if (!previous || point.local[i] != previous->local[i])
            put_float(bytes, at + 128 + i * 4, point.local[i]);
}
Bytes resized_decoration_table(View bytes, std::size_t old_count, std::size_t count) {
    require(count <= (std::numeric_limits<std::uint32_t>::max() - 40) / 192,
            "Decoration table is too large");
    if (!bytes.empty())
        require(std::all_of(bytes.begin() + 40 + old_count * 192, bytes.end(),
                            [](auto value) { return value == 0; }),
                "Decoration table has unexpected trailing data");
    Bytes result(aligned(40 + count * 192, 128));
    if (bytes.empty()) {
        put32(result, 4, 1);
        std::copy_n("locator", 7, result.begin() + 8);
    } else {
        std::copy_n(bytes.begin(), 40 + std::min(old_count, count) * 192, result.begin());
    }
    put32(result, 16, narrow(16 + count * 192));
    put32(result, 24, narrow(count));
    return result;
}
}
Bytes replace_pokemon_decoration_point(View bytes, std::size_t record, const PokemonDecorationPoint &point) {
    validate_decoration(point);
    auto points = decode_pokemon_decoration_points(bytes);
    require(record < points.size(), "Decoration point record is missing");
    for (std::size_t i = 0; i < points.size(); ++i)
        require(i == record || points[i].name != point.name, "Decoration point name already exists");
    Bytes result(bytes.begin(), bytes.end());
    write_decoration(result, 40 + record * 192, point, &points[record]);
    return result;
}
Bytes add_pokemon_decoration_point(View bytes, const PokemonDecorationPoint &point) {
    validate_decoration(point);
    auto points = decode_pokemon_decoration_points(bytes);
    require(std::none_of(points.begin(), points.end(), [&](const auto &p) { return p.name == point.name; }),
            "Decoration point name already exists");
    auto result = resized_decoration_table(bytes, points.size(), points.size() + 1);
    write_decoration(result, 40 + points.size() * 192, point);
    return result;
}
Bytes remove_pokemon_decoration_point(View bytes, std::size_t record) {
    auto points = decode_pokemon_decoration_points(bytes);
    require(record < points.size(), "Decoration point record is missing");
    auto result = resized_decoration_table(bytes, points.size(), points.size() - 1);
    std::copy(bytes.begin() + 40 + (record + 1) * 192,
              bytes.begin() + 40 + points.size() * 192, result.begin() + 40 + record * 192);
    return result;
}
PokemonDecorationPoints pokemon_decoration_point_data(const ModelDocument &model, unsigned group) {
    require(model.is_pokemon() && !model.shadow_model && model.independent_asset.empty(),
            "Decoration points require a source-linked Pokemon main model");
    require(group < TargetProfile::pokemon_decoration_point_slots.size(), "Invalid decoration motion set");
    auto member = model.pokemon.motion_member + TargetProfile::pokemon_motion_slots[group];
    auto source = std::find_if(model.sources.begin(), model.sources.end(), [&](const auto &s) {
        return s.archive == TargetProfile::pokemon_archive && s.member == member;
    });
    require(source != model.sources.end(), "Pokemon motion source is missing");
    auto pack = Container::parse(source->original, "PC");
    PokemonDecorationPoints result;
    result.source = std::size_t(source - model.sources.begin());
    result.child = TargetProfile::pokemon_decoration_point_slots[group];
    require(result.child < pack.files.size(), "Pokemon motion pack has no decoration table");
    result.original = pack.files[result.child];
    result.points = decode_pokemon_decoration_points(result.original);
    return result;
}
std::vector<PokemonDecorationPoint> pokemon_decoration_points(const ModelDocument &model, unsigned group) {
    return pokemon_decoration_point_data(model, group).points;
}
void MaterialDocument::edit_decoration_point(unsigned group, std::size_t record, const PokemonDecorationPoint &point) {
    auto pack = pokemon_decoration_point_data(model, group);
    validate_point_parent(point.bone);
    replace_point_table(pack.source, pack.child, pack.original,
                        replace_pokemon_decoration_point(pack.original, record, point));
}
void MaterialDocument::add_decoration_point(unsigned group, const PokemonDecorationPoint &point) {
    auto pack = pokemon_decoration_point_data(model, group);
    validate_point_parent(point.bone);
    replace_point_table(pack.source, pack.child, pack.original,
                        add_pokemon_decoration_point(pack.original, point));
}
void MaterialDocument::remove_decoration_point(unsigned group, std::size_t record) {
    auto pack = pokemon_decoration_point_data(model, group);
    replace_point_table(pack.source, pack.child, pack.original,
                        remove_pokemon_decoration_point(pack.original, record));
}
}
