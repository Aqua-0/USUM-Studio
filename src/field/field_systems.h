#pragma once
#include "scene/spatial.h"
namespace studio {
struct FieldSystemProperty {
    std::string group, name, value;
};
struct FieldSystemEntry {
    unsigned category = 0, local_zone = 0, row = 0;
    std::string name, notice;
    std::vector<FieldSystemProperty> properties;
    SpatialRegion region;
};
std::vector<FieldSystemEntry> inspect_field_systems(View placements);
void decode_field_system_regions(SpatialScene &scene, View placements,
                                 const std::map<unsigned, int> &zone_ids);
}
