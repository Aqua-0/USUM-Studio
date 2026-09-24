#pragma once
#include "formats/archive.h"
#include "formats/container.h"
#include "compiler/box.h"
#include <optional>
#include "formats/texture.h"
#include "core/game_profile.h"
namespace studio {
struct TargetProfile {
    static constexpr unsigned pedestrian_placement_pack = 9, pedestrian_record_type = 9,
                              pedestrian_record_size = 24, pedestrian_point_capacity = 16;
    static constexpr unsigned encounter_placement_pack = 6, encounter_record_type = 6,
                              encounter_table_slot = 9, encounter_actor_capacity = 16;
    static constexpr const char *trainer_records_archive = "romfs/a/1/0/6",
                                *trainer_teams_archive = "romfs/a/1/0/7";
    static constexpr unsigned trainer_names_member = 110, trainer_classes_member = 111,
                              trainer_record_size = 20, trainer_pokemon_size = 32;
    static constexpr unsigned saved_event_flag_count = 4928;
    static constexpr const char *battle_arenas_archive = "romfs/a/0/8/1",
                                *battle_trainers_archive = "romfs/a/1/7/4";
    static constexpr std::array<float, 3> battle_pokemon_distances{200, 220, 250};
    static constexpr const char *pokemon_archive = "romfs/a/0/9/4";
    static constexpr std::array<unsigned, 3> pokemon_motion_override{778, 3, 1};
    static constexpr unsigned pokemon_fixed_markings_species = 327;
    static constexpr unsigned pokemon_refresh_texture_slot = 3;
    static constexpr const char *refresh_parameters_archive = "romfs/a/2/7/8";
    static constexpr unsigned refresh_options_member = 3, refresh_cameras_member = 9;
    static constexpr unsigned pokemon_stride = 9, pokemon_names_member = 60,
                              pokemon_settings_slot = 8;
    static constexpr std::array<unsigned, 4> pokemon_motion_slots{4, 5, 6, 7};
    static constexpr std::array<const char *, 4> pokemon_motion_names{"Battle", "Refresh", "Field",
                                                                      "Photo Finder"};
    enum class OutfitVariant { Single, Hat, Legs };
    struct OutfitPart {
        const char *name;
        const char *archive;
        unsigned item;
        OutfitVariant variant;
    };
    static constexpr unsigned outfit_item_index(unsigned item, OutfitVariant variant, bool has_hat,
                                                bool short_legs) {
        return variant == OutfitVariant::Hat    ? item * 2 + unsigned(!has_hat)
               : variant == OutfitVariant::Legs ? item * 2 + unsigned(short_legs)
                                                : item;
    }
    static constexpr std::array<std::array<OutfitPart, 9>, 2> player_outfits{
        {{{{"Face", "romfs/a/2/0/2", 0, OutfitVariant::Hat},
           {"Hair", "romfs/a/2/0/3", 6, OutfitVariant::Hat},
           {"Bag", "romfs/a/2/0/5", 109, OutfitVariant::Single},
           {"Bracelet", "romfs/a/2/0/6", 0, OutfitVariant::Single},
           {"Bottoms", "romfs/a/2/0/7", 105, OutfitVariant::Single},
           {"Hat", "romfs/a/2/0/8", 58, OutfitVariant::Single},
           {"Legs", "romfs/a/2/0/9", 37, OutfitVariant::Legs},
           {"Shoes", "romfs/a/2/1/0", 95, OutfitVariant::Single},
           {"Top", "romfs/a/2/1/2", 256, OutfitVariant::Single}}},
         {{{"Face", "romfs/a/2/1/4", 0, OutfitVariant::Hat},
           {"Hair", "romfs/a/2/1/5", 25, OutfitVariant::Hat},
           {"Bag", "romfs/a/2/1/8", 145, OutfitVariant::Single},
           {"Bracelet", "romfs/a/2/1/9", 0, OutfitVariant::Single},
           {"Bottoms", "romfs/a/2/2/0", 249, OutfitVariant::Single},
           {"Hat", "romfs/a/2/2/1", 94, OutfitVariant::Single},
           {"Legs", "romfs/a/2/2/2", 0, OutfitVariant::Legs},
           {"Shoes", "romfs/a/2/2/3", 167, OutfitVariant::Single},
           {"Top", "romfs/a/2/2/5", 394, OutfitVariant::Single}}}}};
    static constexpr std::array<const char *, 2> player_colors{"romfs/a/2/0/1", "romfs/a/2/1/3"};
    static constexpr std::array<unsigned, 3> player_motions{0, 1, 2};
    static constexpr unsigned camera_slot = 10, terrain_ground_slot = 6, terrain_wall_slot = 7;
    static constexpr unsigned camera_defaults = 6, camera_support_defaults = 10;
    static constexpr const char *sky_archive = "romfs/a/0/7/2",
                                *sky_position_archive = "romfs/a/3/0/5";
    static constexpr std::array<std::array<int, 4>, 5> sky_parts{
        {{3, 10, -1, -1}, {2, -1, -1, -1}, {0, 5, 6, -1}, {1, 7, -1, 8}, {4, 11, 12, -1}}};
    static constexpr const char *resident_archive = "romfs/a/1/1/5";
    static constexpr unsigned resident_effects = 3;
    static constexpr unsigned entrance_behavior_resource = 0, entrance_transition_count = 14;
    static constexpr std::array<std::array<unsigned, 4>, 4> weather_boards{
        {{(1u << 1) | (1u << 3), 34, 0, 3},
         {(1u << 1) | (1u << 3), 34, 1, 4},
         {(1u << 4) | (1u << 5) | (1u << 6), 49, 0, 1},
         {1u << 7, 54, 0, 1}}};
    static constexpr std::array<std::array<unsigned, 3>, 4> weather_particles{
        {{1, 34, 5}, {4, 49, 4}, {5, 49, 5}, {6, 49, 2}}};
    static constexpr const char *script_events_archive = "romfs/a/1/5/9";
    static constexpr unsigned static_encounters_member = 1, static_encounter_record_size = 56;
    static constexpr unsigned zone_script_slot = 7;
    static constexpr const char *shared_script_archive = "romfs/a/0/9/2",
                                *script_routing_archive = "romfs/a/1/4/3";
    static constexpr unsigned script_routing_member = 0, script_route_size = 10,
                              first_shared_script = 1000;
    static constexpr const char *interaction_text_archive = "romfs/a/0/4/2";
    static constexpr unsigned pickup_placement_pack = 10, pickup_record_type = 10,
                              position_event_pack = 0, warp_placement_pack = 2,
                              interaction_placement_pack = 3;
    static constexpr unsigned character_placement_pack = 1, trainer_placement_pack = 7,
                              contact_placement_pack = 14;
    static constexpr const char *character_archive = "romfs/a/2/0/0";
    static constexpr const char *weather_profile_archive = "romfs/a/2/6/5",
                                *weather_type_archive = "romfs/a/2/6/7";
    static constexpr unsigned weather_kinds = 11;
    static constexpr const char *location_text_archive = "romfs/a/0/3/2";
    static constexpr std::size_t location_text_member = 72, item_names_member = 40;
    static constexpr const char *field_archive = GameProfile::moon_field_archive;
    static constexpr const char *zone_archive = "romfs/a/0/7/7", *world_archive = "romfs/a/0/9/1",
                                *light_motion_archive = "romfs/a/2/6/6";
    static constexpr std::size_t environment_slot = 6;
    static constexpr std::size_t local_material_motion = 5, daily_material_motion = 6,
                                 static_loop_motion = 2, static_daily_motion = 3;
    static constexpr const char *terrain_archive = "romfs/a/0/8/6";
    static constexpr std::size_t background_resource_slot = 1, terrain_layout_slot = 2,
                                 terrain_cell_size = 12;
    static constexpr std::size_t area_stride = 11, placement_slot = 0, static_resource_slot = 4,
                                 character_resource_slot = 3;
    static constexpr std::size_t static_pack = 4, actor_capacity = 24;
    static constexpr std::uint32_t authored_event_begin = 9000;
    static constexpr std::size_t resource_alignment = 128;
};
struct Placement {
    std::uint32_t event = 0, flags = 0, condition = 0, version = 0, collision = 0;
    std::uint16_t model = 0, alias = 0;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{};
};
Bytes attach_box_collision(View zone, std::size_t placement, BoxSize size);
std::vector<Placement> read_placements(View zone);
void validate_static_alignment(View resources);
Bytes append_placement(View zone, std::size_t template_index, std::uint16_t model,
                       std::uint32_t event, std::array<float, 3> position,
                       bool unconditional = false);
class FieldArea {
  public:
    FieldArea(const Archive &archive, std::size_t index);
    std::string inspect() const;
    std::map<std::size_t, Bytes> build_box(std::uint16_t donor, BoxSize size, std::string &preview,
                                           const TextureImage *texture = nullptr) const;
    std::map<std::size_t, Bytes> add_box(std::uint16_t donor, BoxSize size, std::size_t zone,
                                         std::size_t placement, std::uint16_t model,
                                         std::uint32_t event, std::array<float, 3> position,
                                         std::string &preview, bool unconditional = false,
                                         const TextureImage *texture = nullptr,
                                         bool collision = false) const;
    std::map<std::size_t, Bytes> add_animated(std::uint16_t donor, View model_pack, View motion,
                                              std::size_t zone, std::size_t placement,
                                              std::uint16_t model, std::uint32_t event,
                                              std::array<float, 3> position) const;

  private:
    std::map<std::size_t, Bytes> insert_resource(Container resource, std::uint16_t donor,
                                                 BoxSize size, std::size_t zone,
                                                 std::size_t placement, std::uint16_t model,
                                                 std::uint32_t event, std::array<float, 3> position,
                                                 bool unconditional, bool collision) const;
    const Archive &archive_;
    std::size_t index_;
    Container placements_, resources_;
    std::size_t resource_index(std::uint16_t model) const;
    Bytes compile_resource(std::uint16_t donor, BoxSize size, std::string &preview,
                           const TextureImage *texture = nullptr) const;
};
}
