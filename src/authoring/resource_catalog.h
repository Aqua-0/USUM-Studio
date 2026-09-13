#pragma once
#include "scene/environment.h"
#include <optional>

namespace studio {
enum class MapResourceKind { StaticModel, BakedTerrain, DistantScenery };
struct MapSourceReference {
    SceneModelSource location;
    std::string member_hash;
};
struct MapResourceDependency {
    enum class Kind { Model, Texture, Shader, Motion };
    Kind kind = Kind::Model;
    std::string name;
    MapSourceReference source;
};
struct MapMeshSummary {
    std::string mesh, material;
    MapSourceReference model;
    std::size_t draw = 0, vertices = 0, triangles = 0;
};
struct MapResourceEntry {
    std::string name;
    MapResourceKind kind = MapResourceKind::StaticModel;
    unsigned area = 0;
    std::optional<std::uint16_t> static_model;
    MapSourceReference source;
    std::vector<MapResourceDependency> dependencies;
    std::vector<MapMeshSummary> meshes;
    std::size_t vertices = 0, triangles = 0, draws = 0, materials = 0;
    bool geometry_complete = false;
    std::vector<std::string> issues;
    bool reusable_static() const {
        return kind == MapResourceKind::StaticModel && static_model && geometry_complete &&
               issues.empty();
    }
};
struct MapResourceCatalog {
    unsigned area = 0;
    std::vector<MapResourceEntry> entries;
    std::vector<std::string> issues;
    std::string report() const;
};
MapResourceCatalog load_map_resources(const std::filesystem::path &dump, unsigned area,
                                      std::atomic_bool *cancel = nullptr);
Bytes read_map_source(const std::filesystem::path &dump, const MapSourceReference &source);
Environment preview_map_resource(const std::filesystem::path &dump, const MapResourceEntry &entry,
                                 std::atomic_bool *cancel = nullptr);
}
