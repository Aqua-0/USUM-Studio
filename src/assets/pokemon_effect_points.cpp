#include "assets/pokemon_effect_points.h"
#include "assets/material_document.h"
#include "formats/container.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace studio {
const char *pokemon_effect_point_category(unsigned category) {
    static constexpr const char *names[]{
        "Head", "Above head", "Eye", "Mouth", "Horn", "Center", "Front", "Hand", "Tail", "Foot",
        "Physical attack 1", "Physical attack 2", "Physical attack 3", "Physical attack 4",
        "Special attack 1", "Special attack 2", "Special attack 3", "Special attack 4",
        "General purpose"};
    require(category < std::size(names), "Unknown Pokemon effect point category");
    return names[category];
}
std::string pokemon_effect_point_label(const PokemonEffectPoint &point) {
    return std::string(pokemon_effect_point_category(point.category)) + " / " +
           std::to_string(point.index);
}
namespace {
void validate(const PokemonEffectPoint &point) {
    pokemon_effect_point_category(point.category);
    require(point.index >= 1 && point.index <= 8, "Effect point index must be between 1 and 8");
    require(!point.bone.empty() && point.bone.size() < 32 &&
                point.bone.find('\0') == std::string::npos,
            "Effect point bone name must contain 1 to 31 bytes");
    for (float value : point.offset)
        require(std::isfinite(value), "Effect point offsets must be finite");
}
}
std::vector<PokemonEffectPoint> decode_pokemon_effect_points(View bytes) {
    std::vector<PokemonEffectPoint> points;
    std::set<std::pair<unsigned, unsigned>> keys;
    std::size_t at = 0;
    for (; at + 48 <= bytes.size(); at += 48) {
        if (bytes[at] == 0)
            continue;
        auto name = slice(bytes, at, 32);
        auto end = std::find(name.begin(), name.end(), 0);
        require(end != name.end(), "Effect point bone name is not terminated");
        PokemonEffectPoint point;
        point.record = at / 48;
        point.bone.assign(name.begin(), end);
        for (unsigned axis = 0; axis < 3; ++axis)
            point.offset[axis] = f32(bytes, at + 32 + axis * 4);
        point.category = bytes[at + 44];
        point.index = bytes[at + 45];
        validate(point);
        require(keys.emplace(point.category, point.index).second,
                "Duplicate Pokemon effect point category and index");
        points.push_back(std::move(point));
    }
    require(std::all_of(bytes.begin() + at, bytes.end(), [](auto value) { return value == 0; }),
            "Truncated Pokemon effect point record");
    return points;
}
Bytes replace_pokemon_effect_point(View bytes, const PokemonEffectPoint &point) {
    validate(point);
    auto points = decode_pokemon_effect_points(bytes);
    auto found = std::find_if(points.begin(), points.end(), [&](const auto &p) {
        return p.record == point.record;
    });
    require(found != points.end(), "Effect point record is missing");
    require(found->category == point.category && found->index == point.index,
            "Effect point category and index cannot change");
    Bytes result(bytes.begin(), bytes.end());
    auto at = point.record * 48;
    if (point.bone != found->bone) {
        std::fill_n(result.begin() + at, 32, 0);
        std::copy(point.bone.begin(), point.bone.end(), result.begin() + at);
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        if (point.offset[axis] != found->offset[axis])
            put_float(result, at + 32 + axis * 4, point.offset[axis]);
    return result;
}
Bytes add_pokemon_effect_point(View bytes, const PokemonEffectPoint &point) {
    validate(point);
    auto points = decode_pokemon_effect_points(bytes);
    require(std::none_of(points.begin(), points.end(), [&](const auto &p) {
                return p.category == point.category && p.index == point.index;
            }), "This effect category and index already exists; choose an unused index");
    Bytes result(bytes.begin(), bytes.end());
    std::size_t at = 0;
    for (; at + 48 <= result.size(); at += 48)
        if (std::all_of(result.begin() + at, result.begin() + at + 48,
                        [](auto value) { return value == 0; }))
            break;
    if (at + 48 > result.size())
        result.resize(aligned(at + 48, 128));
    std::copy(point.bone.begin(), point.bone.end(), result.begin() + at);
    for (unsigned axis = 0; axis < 3; ++axis)
        put_float(result, at + 32 + axis * 4, point.offset[axis]);
    result[at + 44] = std::uint8_t(point.category);
    result[at + 45] = std::uint8_t(point.index);
    return result;
}
Bytes remove_pokemon_effect_point(View bytes, std::size_t record) {
    auto points = decode_pokemon_effect_points(bytes);
    require(std::any_of(points.begin(), points.end(), [&](const auto &p) { return p.record == record; }),
            "Effect point record is missing");
    Bytes result(bytes.begin(), bytes.end());
    std::fill_n(result.begin() + record * 48, 48, 0);
    return result;
}
PokemonEffectPoints pokemon_effect_points(const ModelDocument &model, unsigned group) {
    require(model.is_pokemon() && !model.shadow_model && model.independent_asset.empty(),
            "Effect points require a source-linked Pokemon main model");
    require(group < TargetProfile::pokemon_effect_point_slots.size(),
            "Effect points are unavailable for this motion group");
    auto member = model.pokemon.motion_member + TargetProfile::pokemon_motion_slots[group];
    auto source = std::find_if(model.sources.begin(), model.sources.end(), [&](const auto &s) {
        return s.archive == TargetProfile::pokemon_archive && s.member == member;
    });
    require(source != model.sources.end(), "Pokemon motion source is missing");
    auto pack = Container::parse(source->original, "PC");
    PokemonEffectPoints result;
    result.source = std::size_t(source - model.sources.begin());
    result.child = TargetProfile::pokemon_effect_point_slots[group];
    require(result.child < pack.files.size(), "Pokemon motion pack has no effect point table");
    result.original = pack.files[result.child];
    result.points = decode_pokemon_effect_points(result.original);
    return result;
}
std::optional<std::array<float, 3>> pokemon_effect_point_position(
    const PokemonEffectPoint &point, const SceneSkeleton &rig, const std::vector<Matrix> &pose) {
    for (std::size_t i = 0; i < rig.joints.size() && i < pose.size(); ++i) {
        if (rig.joints[i].name != point.bone)
            continue;
        auto world = pose_multiply(pose_multiply(pose[i], rig.placement), rig.joints[i].bind);
        std::array<float, 3> position{};
        for (unsigned axis = 0; axis < 3; ++axis)
            position[axis] = world[axis * 4 + 3] + world[axis * 4] * point.offset[0] +
                             world[axis * 4 + 1] * point.offset[1] +
                             world[axis * 4 + 2] * point.offset[2];
        return position;
    }
    return std::nullopt;
}
void MaterialDocument::validate_point_parent(const std::string &bone) const {
    require(model.scene && !model.scene->skeletons.empty(), "Pokemon skeleton is unavailable");
    const auto &joints = model.scene->skeletons.front().joints;
    require(std::any_of(joints.begin(), joints.end(), [&](const auto &j) { return j.name == bone; }),
            "Point parent bone is missing from this model");
}
void MaterialDocument::replace_point_table(std::size_t source_index, std::size_t child,
                                         View original, const Bytes &replacement) {
    if (std::equal(original.begin(), original.end(), replacement.begin(), replacement.end()))
        return;
    auto members = compiled_members();
    const auto &source = model.sources.at(source_index);
    auto bytes = members.contains(source.member) ? members.at(source.member) : source.original;
    members[source.member] = replace_asset_resource(bytes, {child}, replacement);
    commit();
    replace_members(members);
}
void MaterialDocument::edit_effect_point(unsigned group, const PokemonEffectPoint &point) {
    auto pack = pokemon_effect_points(model, group);
    validate_point_parent(point.bone);
    replace_point_table(pack.source, pack.child, pack.original,
                        replace_pokemon_effect_point(pack.original, point));
}
void MaterialDocument::add_effect_point(unsigned group, const PokemonEffectPoint &point) {
    auto pack = pokemon_effect_points(model, group);
    validate_point_parent(point.bone);
    replace_point_table(pack.source, pack.child, pack.original,
                        add_pokemon_effect_point(pack.original, point));
}
void MaterialDocument::remove_effect_point(unsigned group, std::size_t record) {
    auto pack = pokemon_effect_points(model, group);
    replace_point_table(pack.source, pack.child, pack.original,
                        remove_pokemon_effect_point(pack.original, record));
}
}
