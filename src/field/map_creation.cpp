#include "field/map_creation.h"
#include "field/area.h"
#include "field/warp_document.h"
#include "formats/compression.h"
#include "core/digest.h"
#include <iomanip>
#include <algorithm>
#include <set>
#include <sstream>
namespace studio {
namespace {
Bytes stored(View original, const Bytes &data) {
    return !original.empty() && original[0] == 0x11 ? compress(data) : data;
}
Bytes preserve_container(const Container &container) {
    auto result = container.original;
    auto original = Container::parse(result);
    require(original.files.size() == container.files.size(), "Map clone changed container layout");
    for (std::size_t i = 0; i < container.files.size(); ++i) {
        require(original.files[i].size() == container.files[i].size(),
                "Map clone changed resource extent");
        std::copy(container.files[i].begin(), container.files[i].end(),
                  result.begin() + u32(result, 4 + i * 4));
    }
    return result;
}
void single(const Archive &archive, std::size_t member) {
    require(archive.subfiles(member) == std::vector<unsigned>{0},
            "Map creation requires a single-subfile template resource");
}
}
std::string MapCreation::serialize() const {
    std::ostringstream out;
    out << "USUM_MAP_CREATION 1\n"
        << std::quoted(name) << '\n'
        << source_identity << '\n'
        << template_zone << ' ' << template_area << ' ' << template_world << ' ' << template_terrain
        << '\n'
        << zone << ' ' << area << ' ' << world << ' ' << terrain << '\n'
        << entrance << '\n';
    return out.str();
}
MapCreation MapCreation::parse(const std::string &text) {
    MapCreation result;
    std::istringstream in(text);
    std::string tag;
    unsigned version;
    require(bool(in >> tag >> version) && tag == "USUM_MAP_CREATION" && version == 1,
            "Unsupported map creation document");
    require(bool(in >> std::quoted(result.name) >> result.source_identity >> result.template_zone >>
                 result.template_area >> result.template_world >> result.template_terrain >>
                 result.zone >> result.area >> result.world >> result.terrain >> result.entrance),
            "Incomplete map creation document");
    in >> std::ws;
    require(in.eof(), "Unexpected map creation data");
    return result;
}
MapCreation plan_map_creation(const std::filesystem::path &source, unsigned template_zone,
                              const std::string &name, int entrance) {
    require(!name.empty() && name.size() <= 160 && name.find_first_of("\r\n") == std::string::npos,
            "Give the new map a name of up to 160 bytes");
    require(entrance >= -1, "Invalid template entrance");
    Archive fields(source / GameProfile::field_archive(source)),
        zones(source / TargetProfile::zone_archive), worlds(source / TargetProfile::world_archive),
        terrains(source / TargetProfile::terrain_archive);
    single(zones, 0);
    single(zones, 1);
    auto metadata = zones.decoded(0), mapping = zones.decoded(1);
    require(metadata.size() % 84 == 0 && mapping.size() == metadata.size() / 84 * 2,
            "Unsupported zone registration tables");
    const auto count = metadata.size() / 84;
    require(template_zone < count, "Template zone is unavailable");
    require(fields.size() % TargetProfile::area_stride == 0, "Incomplete field area archive");
    require(count < 65535 && worlds.size() < 65535 && terrains.size() < 65535 &&
                fields.size() + TargetProfile::area_stride <= 65535,
            "New map allocation exceeds the archive or zone index range");
    MapCreation plan;
    plan.name = name;
    plan.template_zone = template_zone;
    plan.template_world = u16(mapping, template_zone * 2);
    plan.zone = unsigned(count);
    plan.area = unsigned(fields.size() / TargetProfile::area_stride);
    plan.world = unsigned(worlds.size());
    plan.terrain = unsigned(terrains.size());
    plan.entrance = entrance;
    auto zone = slice(metadata, template_zone * 84, 84);
    require(zone[31] == 0 && u16(zone, 10) == template_zone,
            "Choose a field template with an independent zone registration");
    for (std::size_t i = 0; i < count; ++i)
        if (i != template_zone)
            require(u16(mapping, i * 2) != plan.template_world,
                    "Choose a template in a world containing only one zone");
    single(worlds, plan.template_world);
    auto world = Container::parse(worlds.decoded(plan.template_world), "WD");
    require(world.files.size() == 2, "Unsupported template world sections");
    auto &header = world.files[0];
    auto area_begin = u32(header, 4), zone_begin = u32(header, 8), end = u32(header, 12);
    require(zone_begin >= area_begin && zone_begin - area_begin == 4 && end >= zone_begin &&
                end - zone_begin == 4 && end <= header.size(),
            "Choose a world containing one area and one zone");
    require(u16(header, zone_begin) == template_zone,
            "Template world does not map the selected global zone");
    plan.template_area = u16(header, zone_begin + 2);
    require(u16(header, area_begin) == plan.template_area && plan.template_area < plan.area,
            "Template world area mapping is invalid");
    require(u32(world.files[1], 0) == 1, "Template membership surface must describe one zone");
    std::string identity =
        sha256(metadata) + sha256(mapping) + sha256(worlds.raw(plan.template_world));
    for (unsigned slot = 0; slot < TargetProfile::area_stride; ++slot) {
        auto member = plan.template_area * TargetProfile::area_stride + slot;
        single(fields, member);
        identity += sha256(fields.raw(member));
    }
    auto layout = Container::parse(fields.decoded(plan.template_area * TargetProfile::area_stride +
                                                  TargetProfile::terrain_layout_slot),
                                   "TR");
    require(layout.files.size() == 1, "Choose a template without terrain variants");
    auto &grid = layout.files[0];
    require(u32(grid, 16) == 0, "Choose a template without terrain replacements");
    auto cells = std::uint64_t(u32(grid, 0)) * u32(grid, 4);
    require(cells > 0 && cells <= 65536, "Invalid template terrain grid");
    slice(grid, 20, std::size_t(cells) * TargetProfile::terrain_cell_size);
    std::set<unsigned> resources;
    for (std::size_t i = 0; i < cells; ++i) {
        auto member = u16(grid, 20 + i * TargetProfile::terrain_cell_size);
        if (member != 65535)
            resources.insert(member);
    }
    require(resources.size() == 1, "Choose a template with one terrain resource");
    plan.template_terrain = *resources.begin();
    single(terrains, plan.template_terrain);
    identity += sha256(terrains.raw(plan.template_terrain));
    WarpDocument warps(plan.template_area,
                       fields.decoded(plan.template_area * TargetProfile::area_stride +
                                      TargetProfile::placement_slot));
    std::set<unsigned> events;
    for (auto &record : warps.records()) {
        require(record.local_zone == 0 && record.zone == template_zone,
                "Template entrances do not belong to its single zone");
        require(events.insert(record.event).second, "Template entrance identities are ambiguous");
    }
    if (entrance >= 0) {
        require(std::size_t(entrance) < warps.records().size(),
                "Choose an existing template entrance");
        require(!warps.records()[std::size_t(entrance)].shapes.empty(),
                "Choose an entrance with a walkable trigger");
    }
    plan.source_identity =
        sha256(View(reinterpret_cast<const std::uint8_t *>(identity.data()), identity.size()));
    return plan;
}
void export_map_creation(const std::filesystem::path &source, const MapCreation &plan,
                         const std::filesystem::path &output) {
    require(plan_map_creation(source, plan.template_zone, plan.name, plan.entrance) == plan,
            "Template or map allocations changed. Recreate the map plan against the current "
            "project source.");
    require(std::filesystem::absolute(source).lexically_normal() !=
                std::filesystem::absolute(output).lexically_normal(),
            "Choose a separate map staging output");
    Archive fields(source / GameProfile::field_archive(source)),
        zones(source / TargetProfile::zone_archive), worlds(source / TargetProfile::world_archive),
        terrains(source / TargetProfile::terrain_archive);
    std::map<std::pair<std::size_t, unsigned>, Bytes> field_members;
    for (unsigned slot = 0; slot < TargetProfile::area_stride; ++slot)
        field_members[{plan.area * TargetProfile::area_stride + slot, 0}] =
            fields.raw(plan.template_area * TargetProfile::area_stride + slot);
    auto placement_member = plan.area * TargetProfile::area_stride + TargetProfile::placement_slot;
    auto original = field_members.at({placement_member, 0});
    auto placements = decompress(original);
    WarpDocument warps(plan.template_area, placements);
    for (auto &record : warps.records()) {
        put16(placements, record.offset + 52, std::uint16_t(plan.zone));
        if (u16(placements, record.offset + 54) == plan.template_zone)
            put16(placements, record.offset + 54, std::uint16_t(plan.zone));
    }
    if (plan.entrance >= 0) {
        auto &record = warps.records().at(std::size_t(plan.entrance));
        auto connection = warps.values(unsigned(plan.entrance));
        connection.destination_zone = plan.zone;
        connection.destination_event = record.event;
        warps.set(unsigned(plan.entrance), connection);
        field_members[{
            plan.template_area * TargetProfile::area_stride + TargetProfile::placement_slot, 0}] =
            stored(original, warps.compile());
        put16(placements, record.offset + 54, std::uint16_t(plan.template_zone));
        put32(placements, record.offset + 48, record.event);
    }
    field_members[{placement_member, 0}] = stored(original, placements);
    auto layout_member =
        plan.area * TargetProfile::area_stride + TargetProfile::terrain_layout_slot;
    original = field_members.at({layout_member, 0});
    auto layout = Container::parse(decompress(original), "TR");
    auto &grid = layout.files[0];
    auto cells = std::size_t(u32(grid, 0)) * u32(grid, 4);
    for (std::size_t i = 0; i < cells; ++i) {
        auto at = 20 + i * TargetProfile::terrain_cell_size;
        if (u16(grid, at) != 65535)
            put16(grid, at, std::uint16_t(plan.terrain));
    }
    field_members[{layout_member, 0}] = stored(original, preserve_container(layout));
    auto world_raw = worlds.raw(plan.template_world);
    auto world = Container::parse(decompress(world_raw), "WD");
    auto &header = world.files[0];
    put16(header, u32(header, 4), std::uint16_t(plan.area));
    put16(header, u32(header, 8), std::uint16_t(plan.zone));
    put16(header, u32(header, 8) + 2, std::uint16_t(plan.area));
    auto metadata_raw = zones.raw(0), mapping_raw = zones.raw(1);
    auto metadata = decompress(metadata_raw), mapping = decompress(mapping_raw);
    auto donor = slice(metadata, plan.template_zone * 84, 84);
    Bytes next(donor.begin(), donor.end());
    put16(next, 10, std::uint16_t(plan.zone));
    append(metadata, next);
    mapping.resize(mapping.size() + 2);
    put16(mapping, mapping.size() - 2, std::uint16_t(plan.world));
    fields.export_appended(output / GameProfile::field_archive(source), field_members);
    worlds.export_appended(output / TargetProfile::world_archive,
                           {{{plan.world, 0}, stored(world_raw, preserve_container(world))}});
    terrains.export_appended(output / TargetProfile::terrain_archive,
                             {{{plan.terrain, 0}, terrains.raw(plan.template_terrain)}});
    zones.export_to(output / TargetProfile::zone_archive,
                    {{0, stored(metadata_raw, metadata)}, {1, stored(mapping_raw, mapping)}});
}
}
