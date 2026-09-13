#pragma once
#include "authoring/resource_catalog.h"
#include "authoring/project_asset.h"
#include "authoring/tile_grid.h"
#include "authoring/ground_surface.h"
#include "field/placement_document.h"
#include "field/collision_document.h"

namespace studio {
struct ObjectCollisionBox {
    SpatialPoint offset{}, size{1, 1, 1};
    bool operator==(const ObjectCollisionBox &) const = default;
};
void validate_object_collision(const ObjectCollisionBox &box);
ObjectCollisionBox approximate_object_collision(const Environment &model);
struct CompositionInstance {
    std::uint64_t id = 0;
    std::size_t resource = 0;
    PlacementState transform;
    std::optional<ObjectCollisionBox> collision;
    bool operator==(const CompositionInstance &) const = default;
};
class CompositionDocument {
  public:
    CompositionDocument(MapResourceCatalog catalog, std::string template_hash, AuthoringGrid grid);
    const MapResourceCatalog &catalog() const {
        return catalog_;
    }
    const std::vector<ProjectAsset> &project_assets() const {
        return state_.assets;
    }
    const ProjectAsset *project_asset(std::size_t resource) const;
    static constexpr std::size_t project_resource_base = 1u << 28;
    std::size_t project_resource(std::size_t id) const {
        return project_resource_base + id;
    }
    void add_library(const MapResourceCatalog &catalog);
    std::string resource_name(std::size_t resource) const;
    bool can_place(std::size_t resource) const;
    std::size_t save_project_asset(ProjectAsset asset);
    const AuthoringGrid &grid() const {
        return state_.grid;
    }
    const std::vector<CompositionInstance> &instances() const {
        return state_.instances;
    }
    std::uint64_t add(std::size_t resource, PlacementState transform);
    std::uint64_t duplicate(std::uint64_t id, PlacementState transform);
    void transform(std::uint64_t id, PlacementState transform);
    void erase(std::uint64_t id);
    void object_collision(std::uint64_t id, std::optional<ObjectCollisionBox> box);
    void set_grid(AuthoringGrid grid);
    const std::optional<GroundSurface> &ground() const {
        return state_.ground;
    }
    void create_ground(AuthoringGrid grid, float height);
    void apply_ground_brush(const GroundBrushStroke &stroke);
    void paint_ground(AuthoringTile first, AuthoringTile last, const std::string &texture);
    void blend_ground(AuthoringTile first, AuthoringTile last, const std::string &texture,
                      float coverage, int direction = -1);
    void edit_terrain(GroundSurface ground);
    void move_vertex(AuthoringTile vertex, SpatialPoint delta);
    void scale_ground_texture(AuthoringTile first, AuthoringTile last, std::array<float, 2> size);
    void extend_ground(int edge, int cells);
    void tilt_ground(AuthoringTile first, AuthoringTile last, float x_degrees, float z_degrees);
    void influence_ground_height(AuthoringTile first, AuthoringTile last, float value,
                                 float surrounding_cells);
    void shape_ground(AuthoringTile first, AuthoringTile last, float value, bool flatten = false);
    bool stage_objects() const {
        return state_.stage_objects;
    }
    unsigned object_zone() const {
        return state_.object_zone;
    }
    void object_export_settings(bool enabled, unsigned zone);
    bool replaces_terrain() const {
        return state_.replace_terrain;
    }
    void replace_terrain(bool enabled);
    bool stage_ground() const {
        return state_.stage_ground;
    }
    std::uint32_t ground_attribute() const {
        return state_.ground_attribute;
    }
    const std::optional<std::vector<CollisionState>> &custom_collision() const {
        return state_.custom_collision;
    }
    void customize_collision(std::vector<CollisionState> faces);
    void follow_terrain_collision();
    void ground_export_settings(bool enabled, std::uint32_t attribute);
    void undo();
    void redo();
    bool can_undo() const {
        return cursor_ > 0;
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size();
    }
    bool dirty() const {
        return !(state_ == saved_);
    }
    void mark_saved() {
        saved_ = state_;
    }
    std::string serialize() const;
    void restore(const std::string &text);
    void save(const std::filesystem::path &path);

  private:
    struct State {
        bool stage_objects = false;
        unsigned object_zone = 0;
        std::optional<std::vector<CollisionState>> custom_collision;
        bool stage_ground = false;
        bool replace_terrain = false;
        std::uint32_t ground_attribute = 1;
        std::vector<ProjectAsset> assets;
        AuthoringGrid grid;
        std::optional<GroundSurface> ground;
        std::vector<CompositionInstance> instances;
        bool operator==(const State &) const = default;
    };
    void commit(State next);
    MapResourceCatalog catalog_;
    std::string template_hash_;
    State state_, saved_;
    std::vector<State> history_;
    std::size_t cursor_ = 0;
    std::uint64_t next_id_ = 1;
    std::size_t next_asset_id_ = 1;
};
MapResourceCatalog load_composition_catalog(const std::filesystem::path &dump, unsigned area,
                                            const std::string &document,
                                            std::atomic_bool *cancel = nullptr);
std::string map_template_fingerprint(const std::filesystem::path &dump,
                                     const MapResourceCatalog &catalog);
std::array<SpatialPoint, 8> object_collision_corners(const CompositionInstance &instance);
Matrix composition_transform(const PlacementState &state);
Environment compose_map_preview(const Environment &base,
                                const std::vector<CompositionInstance> &instances,
                                const std::map<std::size_t, Environment> &resources);
}
