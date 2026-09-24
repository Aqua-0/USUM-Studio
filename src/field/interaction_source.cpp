#include "field/interaction_source.h"
#include "field/map_catalog.h"
#include "formats/amx.h"
#include "formats/container.h"
#include <algorithm>
#include <optional>
namespace studio {
std::vector<SharedScriptRoute> decode_script_routes(View bytes) {
    require(!bytes.empty() && bytes.size() % TargetProfile::script_route_size == 0,
            "Invalid shared script routing table");
    std::vector<SharedScriptRoute> routes;
    for (unsigned at = 0; at < bytes.size(); at += TargetProfile::script_route_size) {
        SharedScriptRoute route{u16(bytes, at), u16(bytes, at + 2), u16(bytes, at + 4),
                                u16(bytes, at + 6), u16(bytes, at + 8)};
        require(route.first <= route.last && route.message_kind <= 1,
                "Invalid shared script route");
        routes.push_back(route);
    }
    std::sort(routes.begin(), routes.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });
    for (unsigned i = 1; i < routes.size(); ++i)
        require(routes[i - 1].last < routes[i].first, "Overlapping shared script routes");
    return routes;
}
Bytes read_shared_script(const Archive &archive, unsigned member) {
    auto raw = archive.raw(member);
    if (raw.size() >= 8 && u16(raw, 4) == 0xf1e0)
        return raw;
    return archive.decoded(member);
}
InteractionSource resolve_interaction_source(const std::filesystem::path &dump,
                                             const ArchiveSources &archives, unsigned area,
                                             unsigned local_zone, int zone, unsigned script) {
    require(zone >= 0 && script > 0 && script <= 65535, "Interaction has no valid zone or script");
    InteractionSource result;
    if (script >= TargetProfile::first_shared_script) {
        auto routes = decode_script_routes(
            Archive(archives.resolve(dump, TargetProfile::script_routing_archive))
                .decoded(TargetProfile::script_routing_member));
        auto route = std::find_if(routes.begin(), routes.end(), [&](const auto &r) {
            return script >= r.first && script <= r.last;
        });
        require(route != routes.end(), "Script ID is absent from the shared routing table");
        result.shared = true;
        result.archive = TargetProfile::shared_script_archive;
        result.member = route->member;
        result.message_member = route->message_member;
        result.messages = route->message_kind ? TargetProfile::interaction_text_archive
                                              : TargetProfile::location_text_archive;
        result.program =
            read_shared_script(Archive(archives.resolve(dump, result.archive)), result.member);
    } else {
        result.archive = GameProfile::field_archive(dump);
        result.member = area * TargetProfile::area_stride + TargetProfile::zone_script_slot;
        result.local_zone = local_zone;
        auto scripts = Container::parse(
            Archive(archives.resolve(dump, result.archive)).decoded(result.member), "ZS");
        require(local_zone < scripts.files.size(), "Zone script slot is unavailable");
        result.program = scripts.files[local_zone];
        auto zones = Archive(dump / TargetProfile::zone_archive).decoded(0);
        result.message_member = u16(zones, std::size_t(zone) * 84 + 12);
        result.messages = TargetProfile::interaction_text_archive;
    }
    decode_field_amx(result.program);
    return result;
}
std::vector<InteractionUse> shared_script_uses(const std::filesystem::path &dump,
                                               const ArchiveSources &archives, unsigned member) {
    auto routes =
        decode_script_routes(Archive(archives.resolve(dump, TargetProfile::script_routing_archive))
                                 .decoded(TargetProfile::script_routing_member));
    Archive field(archives.resolve(dump, GameProfile::field_archive(dump)));
    std::vector<InteractionUse> result;
    struct Layout {
        unsigned category, size, script;
        const char *name;
    };
    const Layout layouts[] = {{TargetProfile::position_event_pack, 60, 44, "Position trigger"},
                              {TargetProfile::character_placement_pack, 120, 56, "NPC"},
                              {TargetProfile::interaction_placement_pack, 60, 48, "Scenery"},
                              {TargetProfile::trainer_placement_pack, 84, 52, "Trainer"}};
    for (unsigned area = 0; std::size_t(area) * TargetProfile::area_stride < field.size(); ++area) {
        auto ed = Container::parse(
            field.decoded(area * TargetProfile::area_stride + TargetProfile::placement_slot), "ED");
        std::optional<std::map<unsigned, int>> zone_ids;
        for (const auto &layout : layouts) {
            if (layout.category >= ed.files.size() || ed.files[layout.category].empty())
                continue;
            auto zones = Container::parse(ed.files[layout.category]);
            for (unsigned z = 0; z < zones.files.size(); ++z) {
                const auto &data = zones.files[z];
                if (data.empty())
                    continue;
                auto count = u32(data, 0);
                slice(data, 4, std::size_t(count) * layout.size);
                for (unsigned row = 0; row < count; ++row) {
                    auto at = 4 + row * layout.size, script = u32(data, at + layout.script);
                    if (!std::any_of(routes.begin(), routes.end(), [&](const auto &r) {
                            return r.member == member && script >= r.first && script <= r.last;
                        }))
                        continue;
                    if (!zone_ids)
                        zone_ids = load_area_zone_ids(dump, area);
                    result.push_back(
                        {area, z, row,
                         layout.category == TargetProfile::position_event_pack ? 0u
                                                                               : u32(data, at + 44),
                         script, zone_ids->contains(z) ? zone_ids->at(z) : -1, layout.name});
                }
            }
        }
    }
    return result;
}
}
