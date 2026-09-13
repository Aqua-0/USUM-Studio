#pragma once
#include "field/archive_sources.h"
#include "core/binary.h"
#include <array>
#include <optional>
namespace studio {
struct MapLocation {
    int zone = -1, area = 0;
    std::string name;
    std::optional<std::array<float, 3>> start;
};
struct MapCatalog {
    std::vector<MapLocation> locations;
    std::string warning;
};
std::optional<std::array<float, 3>> decode_map_start(View zone);
std::vector<std::string> decode_location_text(View bytes);
MapCatalog load_map_catalog(const std::filesystem::path &dump, const ArchiveSources &archives = {});
}
