#include "field/map_catalog.h"
#include "field/area.h"
#include <set>
#include <cmath>
namespace studio {
std::optional<std::array<float, 3>> decode_map_start(View zone) {
    slice(zone, 0, 84);
    std::array<float, 3> position{};
    for (unsigned i = 0; i < 3; ++i) {
        position[i] = float(std::int32_t(u32(zone, 16 + i * 4)));
        if (std::abs(position[i]) > 1e7f)
            return {};
    }
    return position;
}
std::vector<std::string> decode_location_text(View b) {
    require(u16(b, 0) == 1 && u32(b, 8) == 0 && u32(b, 12) == 16,
            "Unsupported location text header");
    require(std::size_t(u32(b, 4)) + 16 == b.size() && u32(b, 16) == u32(b, 4),
            "Invalid location text length");
    auto count = u16(b, 2);
    slice(b, 20, std::size_t(count) * 8);
    std::vector<std::string> out;
    for (unsigned i = 0; i < count; ++i) {
        auto offset = std::size_t(u32(b, 20 + i * 8)) + 16;
        auto length = u16(b, 24 + i * 8);
        require(offset >= 20 + std::size_t(count) * 8, "Location text overlaps its table");
        auto data = slice(b, offset, std::size_t(length) * 2);
        std::vector<unsigned> words;
        std::uint16_t key = std::uint16_t(0x7c89 + i * 0x2983);
        for (unsigned j = 0; j < length; ++j) {
            words.push_back(u16(data, j * 2) ^ key);
            key = std::uint16_t((key << 3) | (key >> 13));
        }
        std::string line;
        for (std::size_t j = 0; j < words.size() && words[j]; ++j) {
            auto c = words[j];
            if (c == 16) {
                require(j + 1 < words.size(), "Truncated location text control");
                auto n = words[++j];
                require(n <= words.size() - j - 1, "Truncated location text arguments");
                j += n;
                continue;
            }
            if (c >= 0xd800 && c <= 0xdbff) {
                require(j + 1 < words.size() && words[j + 1] >= 0xdc00 && words[j + 1] <= 0xdfff,
                        "Invalid location text surrogate");
                c = 0x10000 + ((c - 0xd800) << 10) + (words[++j] - 0xdc00);
            } else
                require(c < 0xdc00 || c > 0xdfff, "Invalid location text surrogate");
            if (c == 0xe08e) c = 0x2642;
            else if (c == 0xe08f) c = 0x2640;
            if (c < 32)
                c = ' ';
            if (c < 128)
                line += char(c);
            else if (c < 2048) {
                line += char(0xc0 | (c >> 6));
                line += char(0x80 | (c & 63));
            } else if (c < 65536) {
                line += char(0xe0 | (c >> 12));
                line += char(0x80 | ((c >> 6) & 63));
                line += char(0x80 | (c & 63));
            } else {
                line += char(0xf0 | (c >> 18));
                line += char(0x80 | ((c >> 12) & 63));
                line += char(0x80 | ((c >> 6) & 63));
                line += char(0x80 | (c & 63));
            }
        }
        out.push_back(std::move(line));
    }
    return out;
}
MapCatalog load_map_catalog(const std::filesystem::path &dump, const ArchiveSources &archives) {
    MapCatalog out;
    Archive fields(archives.resolve(dump, TargetProfile::field_archive)),
        zone_archive(dump / TargetProfile::zone_archive),
        world_archive(dump / TargetProfile::world_archive);
    auto zones = zone_archive.decoded(0), worlds = zone_archive.decoded(1);
    require(zones.size() % 84 == 0 && worlds.size() == zones.size() / 84 * 2,
            "Unsupported map zone table");
    require(fields.size() % TargetProfile::area_stride == 0, "Invalid field area count");
    auto count = fields.size() / TargetProfile::area_stride;
    std::vector<std::string> names;
    try {
        Archive messages(dump / TargetProfile::location_text_archive);
        names = decode_location_text(messages.decoded(TargetProfile::location_text_member));
    } catch (const std::exception &e) {
        out.warning = "Location names unavailable: " + std::string(e.what());
    }
    std::map<unsigned, Bytes> headers;
    std::set<int> covered;
    for (unsigned zone = 0; zone < zones.size() / 84; ++zone) {
        auto z = slice(zones, zone * 84, 84);
        if (z[31] != 0)
            continue;
        auto world = u16(worlds, zone * 2);
        if (!headers.contains(world)) {
            auto pack = Container::parse(world_archive.decoded(world), "WD");
            headers[world] = pack.files.at(0);
        }
        auto &h = headers.at(world);
        auto begin = u32(h, 8), end = u32(h, 12);
        require(end >= begin && (end - begin) % 4 == 0, "Invalid world area mapping");
        slice(h, begin, end - begin);
        auto name = u16(z, 28);
        std::string label =
            name < names.size() && !names[name].empty() ? names[name] : "Unnamed map";
        std::set<int> mapped;
        for (auto p = begin; p < end; p += 4)
            if (u16(h, p) == zone) {
                int area = u16(h, p + 2);
                require(std::size_t(area) < count, "Map references a missing field area");
                if (mapped.insert(area).second) {
                    out.locations.push_back({int(zone), area, label, decode_map_start(z)});
                    covered.insert(area);
                }
            }
    }
    for (std::size_t area = 0; area < count; ++area)
        if (!covered.contains(int(area)))
            out.locations.push_back({-1, int(area), "Unassigned field area"});
    return out;
}
std::map<unsigned, int> load_area_zone_ids(const std::filesystem::path &dump, unsigned area) {
    auto catalog = load_map_catalog(dump);
    Archive zones(dump / TargetProfile::zone_archive), worlds(dump / TargetProfile::world_archive);
    auto mapping = zones.decoded(1);
    std::set<unsigned> selected_worlds;
    for (auto &location : catalog.locations)
        if (location.area == int(area) && location.zone >= 0)
            selected_worlds.insert(u16(mapping, std::size_t(location.zone) * 2));
    std::map<unsigned, int> result;
    for (auto world : selected_worlds) {
        auto pack = Container::parse(worlds.decoded(world), "WD");
        auto &header = pack.files.at(0);
        auto begin = u32(header, 8), end = u32(header, 12);
        require(end >= begin && (end - begin) % 4 == 0, "Invalid world zone list");
        slice(header, begin, end - begin);
        unsigned previous_area = ~0u, local = 0;
        for (auto at = begin; at < end; at += 4, ++local) {
            auto member_area = u16(header, at + 2);
            if (member_area != previous_area)
                local = 0;
            previous_area = member_area;
            if (member_area != area)
                continue;
            auto zone = int(u16(header, at));
            auto [it, added] = result.emplace(local, zone);
            require(added || it->second == zone, "Conflicting area-local zone registrations");
        }
    }
    return result;
}

}
