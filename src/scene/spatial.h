#pragma once
#include "core/binary.h"
#include <array>
#include <string>
#include <vector>
#include <map>
#include <memory>
namespace studio {
using SpatialPoint = std::array<float, 3>;
enum class SpatialKind {
    Ground,
    Wall,
    WaterWall,
    RideWall,
    MudsdaleWall,
    Placement,
    Camera,
    ScrollStop,
    Zone,
    Entrance,
    StoryTrigger,
    Interaction,
    Actor,
    Pickup,
    Encounter,
    FieldAction,
    SpecialEncounter,
    Pedestrian,
    Berry,
    Fishing,
    PhotoSpot,
    Contact,
    RockPuzzle,
    AmbientSound,
    Count
};
const char *spatial_kind_name(SpatialKind kind);
struct SpatialVertex {
    SpatialPoint position{};
    float ratio = 0;
};
struct CollisionSource {
    std::filesystem::path archive;
    std::size_t member = 0;
    std::vector<std::size_t> path;
    Bytes original;
};
struct OverworldReference {
    std::uint64_t editor_id = 0;
    unsigned category = 0, local_zone = 0, row = 0, event = 0, script = 0, version = 0,
             condition = 0, value = 0, style = 0;
    unsigned item = 0, quantity = 0, appearance = 0;
    int destination_zone = -1;
    unsigned destination_event = 0;
    SpatialPoint position{}, arrival{};
    std::array<float, 4> rotation{};
};
struct SpatialRegion {
    std::shared_ptr<const OverworldReference> overworld;

    std::shared_ptr<const CollisionSource> collision_source;
    std::vector<std::size_t> collision_faces;
    int placement = -1;
    int encounter_zone = -1, encounter_row = -1;
    SpatialPoint circle_center{};
    float circle_radius = 0, center_ratio = 0;
    std::string name, detail;
    SpatialKind kind = SpatialKind::Ground;
    int camera = -1, zone = -1;
    unsigned attribute = 0;
    std::vector<SpatialVertex> vertices;
    std::vector<std::uint32_t> triangles, lines;
};
struct CameraPoint {
    SpatialPoint offset{}, rotation{};
    float fov = 0, distance = 0;
    bool zone_default = false;
};
struct CameraSupport {
    bool zone_default = false, parameter_default = false;
    unsigned type = 0;
    float maximum = 0;
    std::array<unsigned, 3> entering{}, leaving{};
    bool single_axis = false;
    float axis = 0;
};
struct ZoneCamera {
    int zone = -1;
    unsigned camera = 0, support = 0;
};
struct CameraSetting {
    unsigned type = 0, transition = 0, easing = 0;
    int priority = 0, work = 0;
    unsigned value = 0;
    CameraPoint a, b;
    CameraSupport support;
    bool player_target = false;
    SpatialPoint hold_offset{}, target{}, position{};
    float hold_fov = 0, bank = 0;
};
struct CameraScrollStop {
    bool outside = false;
    std::array<SpatialPoint, 4> hit{}, clamp{};
};
struct SpatialScene {
    std::vector<CameraScrollStop> scroll_stops;
    std::vector<SpatialRegion> regions;
    std::vector<CameraSetting> cameras, defaults, support_defaults;
    std::vector<ZoneCamera> zones;
    std::vector<std::array<unsigned, 3>> replacements;
};
ZoneCamera decode_zone_camera(View bytes, int zone);
std::vector<CameraSetting>
decode_camera_settings(View bytes, std::vector<std::array<unsigned, 3>> *replacements = nullptr);
void decode_zone_regions(SpatialScene &scene, View bytes, unsigned area,
                         const std::map<unsigned, int> &zone_ids);
void decode_camera_regions(SpatialScene &scene, View bytes);
void decode_collision_mesh(SpatialScene &scene, View bytes, SpatialKind kind,
                           const std::string &name,
                           std::shared_ptr<const CollisionSource> source = {});
void decode_overworld_regions(SpatialScene &scene, View placements,
                              const std::map<unsigned, int> &zone_ids);
void decode_placement_collision(SpatialScene &scene, View bytes, const std::string &name, int zone);
}
