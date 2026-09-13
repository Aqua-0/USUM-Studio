#pragma once
#include "core/binary.h"
namespace studio {
struct MapCreation {
    std::string name, source_identity;
    unsigned template_zone = 0, template_area = 0, template_world = 0, template_terrain = 0;
    unsigned zone = 0, area = 0, world = 0, terrain = 0;
    int entrance = -1;
    std::string serialize() const;
    static MapCreation parse(const std::string &);
    bool operator==(const MapCreation &) const = default;
};
MapCreation plan_map_creation(const std::filesystem::path &source, unsigned template_zone,
                              const std::string &name, int entrance = -1);
void export_map_creation(const std::filesystem::path &source, const MapCreation &,
                         const std::filesystem::path &output);
}
