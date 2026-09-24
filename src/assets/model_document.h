#pragma once
#include "assets/pokemon_catalog.h"
#include "assets/refresh_regions.h"
#include "assets/refresh_feeding.h"
#include "scene/environment.h"
#include <memory>
namespace studio {
struct AssetSourceMember {
    std::filesystem::path archive;
    std::size_t member = 0;
    std::string role, hash;
    Bytes original;
    unsigned subfile = 0;
};
struct AssetResourceLink {
    std::size_t source = 0;
    std::vector<std::size_t> path;
    std::string role, name;
};
struct EffectMotionSource {
    std::size_t member = 0;
    unsigned subfile = 0;
    std::vector<std::size_t> path;
    bool operator==(const EffectMotionSource &) const = default;
};
struct ClothingSelection;
struct AssetMotion {
    std::string name, error;
    unsigned group = 0, slot = 0;
    std::size_t resource = 0;
    bool daily = false;
    SkeletalMotion skeletal;
    std::vector<std::shared_ptr<const SkeletalMotion>> skeleton_motions;
    std::vector<MaterialAnimation> material_channels;
    std::vector<VisibilityAnimation> visibility_channels;
    MaterialMotion material;
    VisibilityMotion visibility;
};
enum class ModelAssetKind { Pokemon, ArchiveModel };
struct ModelDocument {
    std::shared_ptr<const ClothingSelection> clothing;
    int clothing_part = -1;
    ModelAssetKind kind = ModelAssetKind::Pokemon;
    bool is_pokemon() const {
        return kind == ModelAssetKind::Pokemon && area < 0;
    }
    std::filesystem::path dump;
    ArchiveSources archive_sources;
    PokemonEntry pokemon;
    std::string name, texture_prefix = "model/";
    int area = -1;
    int originating_map = -1;
    bool battle_effect = false;
    bool project_asset = false;
    std::string independent_asset;
    std::vector<EffectMotionSource> effect_motions;
    bool shiny = false, shadow_model = false, has_shadow_model = false;
    std::shared_ptr<Environment> scene;
    std::vector<AssetSourceMember> sources;
    std::vector<AssetResourceLink> resources;
    std::vector<AssetMotion> motions;
    std::shared_ptr<const RefreshRegionPack> refresh_regions;
    std::vector<std::size_t> refresh_resources;
    std::string refresh_error;
    std::shared_ptr<const RefreshFeedingData> refresh_feeding;
    std::string feeding_error;
    std::vector<std::size_t> draw_resources, material_resources;
    std::vector<std::size_t> native_meshes;
    std::map<std::string, std::size_t> texture_resources;
    int motion = -1, looping_overlay = -1;
    bool looping_effects = true;
    void select_motion(int index, bool repeat = true);
    std::string report() const;
};
ModelDocument reload_pokemon(const ModelDocument &model,
                             const std::map<std::size_t, Bytes> &members);
ModelDocument reload_map_model(const ModelDocument &model,
                               const std::map<std::size_t, Bytes> &members);
ModelDocument isolate_map_model(const Environment &scene, int draw,
                                const std::filesystem::path &dump, int area);
ModelDocument load_pokemon(const std::filesystem::path &dump, const PokemonEntry &entry, bool shiny,
                           std::atomic_bool *cancel = nullptr, const ArchiveSources &archives = {},
                           bool shadow_model = false);
}
