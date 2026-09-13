#pragma once
#include "authoring/resource_catalog.h"

namespace studio {
struct AuthoredVertex {
    std::array<float, 15> values{};
    std::uint32_t color = 0xffffffff;
    bool operator==(const AuthoredVertex &) const = default;
};
struct AuthoredMesh {
    std::vector<AuthoredVertex> vertices;
    std::vector<std::uint32_t> indices;
    bool operator==(const AuthoredMesh &) const = default;
};
struct ProjectAsset {
    std::size_t id = 0, source = 0;
    std::string name;
    SpatialPoint pivot{};
    std::vector<std::vector<std::uint32_t>> faces;
    std::vector<AuthoredMesh> meshes;
    bool operator==(const ProjectAsset &) const = default;
};
AuthoredVertex authored_vertex(const SceneVertex &);
SceneVertex scene_vertex(const AuthoredVertex &);
Environment project_asset_geometry(const Environment &, const ProjectAsset &);
void validate_authored_mesh(const AuthoredMesh &);
std::string export_object_exchange(const Environment &, const ProjectAsset &,
                                   const std::string &signature,
                                   const std::vector<std::string> &textures = {});
ProjectAsset import_object_exchange(const std::string &, const ProjectAsset &,
                                    const std::string &signature);
ProjectAsset begin_project_asset(std::size_t source, const Environment &scene);
Environment project_asset_preview(const Environment &source, const ProjectAsset &asset);
void validate_project_asset(const ProjectAsset &asset, const MapResourceCatalog &catalog);
}
