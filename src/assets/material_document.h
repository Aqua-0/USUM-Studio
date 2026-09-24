#pragma once
#include "assets/model_document.h"
#include "assets/model_exchange.h"
#include "assets/face_materials.h"
#include "assets/motion_exchange.h"
#include <set>
namespace studio {
struct TextureEdit {
    std::string name;
    std::array<float, 5> transform{};
    unsigned wrap_u = 0, wrap_v = 0, min_filter = 0, mag_filter = 0;
    bool operator==(const TextureEdit &) const = default;
};
struct MaterialEdit {
    std::string shader_origin;
    std::optional<CombinerSettings> combiners;
    std::array<MaterialColor, 9> colors{};
    std::array<TextureEdit, 3> textures;
    unsigned cull = 0, alpha_function = 1, alpha_reference = 0;
    std::uint32_t blend = 0x01010000, depth = 0x1f51;
    bool fragment_lighting = true;
    int layer = 0, priority = 0;
    unsigned edge_type = 2, edge_id = 0;
    bool id_edge_enabled = false;
    int edge_alpha_mask = -1;
    bool operator==(const MaterialEdit &) const = default;
};
struct MaterialLightingBinding {
    std::uint32_t table = 0;
    unsigned input = 0;
    bool unsigned_range = true;
    float scale = 1;
    bool operator==(const MaterialLightingBinding &) const = default;
};
class MaterialDocument {
  public:
    explicit MaterialDocument(ModelDocument document);
    ModelDocument model;
    const std::vector<std::string> &texture_names() const {
        return texture_names_;
    }
    const std::vector<MaterialEdit> &edits() const {
        return edits_;
    }
    void preview(std::size_t material, const MaterialEdit &edit);
    void preview_refresh(std::size_t mask, View ids);
    void preview_feeding(const RefreshFeedingParameters &parameters);
    void reset_feeding();
    void preview_feeding_camera(const RefreshFeedingCamera &camera);
    void reset_feeding_camera();
    void preview_refresh_camera(unsigned slot, const RefreshCamera &camera);
    void reset_refresh_camera(unsigned slot);
    void preview_refresh_batch(std::map<std::size_t, Bytes> masks);
    void reset_refresh(std::size_t mask);
    std::size_t refresh_write_count() const {
        return touched_refresh_.size();
    }
    std::map<std::uint32_t, LightingTable> lighting_tables() const;
    std::array<MaterialLightingBinding, 3> lighting_bindings(std::size_t material) const;
    void edit_lighting_bindings(std::size_t material,
                                const std::array<MaterialLightingBinding, 3> &bindings);
    void edit_lighting_table(std::uint32_t table, const LightingTable &values);
    void commit();
    void undo();
    void redo();
    void reset(std::size_t material);
    bool can_undo() const {
        return cursor_ > 0 || bool(structural_parent_);
    }
    bool can_redo() const {
        return cursor_ + 1 < history_.size() || bool(structural_redo_);
    }
    bool dirty() const;
    bool changed() const {
        return !map_motion_edits_.empty() || edits_ != initial_ || !texture_edits_.empty() ||
               !refresh_edits_.empty() || !camera_edit_.empty() || feeding_edit_.has_value() ||
               structural_version_ != 0;
    }
    void mark_saved();
    void accept_written(const MaterialDocument &snapshot);
    void discard();
    std::uint64_t revision() const {
        return revision_;
    }
    std::uint64_t texture_revision() const {
        return texture_revision_;
    }
    std::uint64_t model_revision() const {
        return model_revision_;
    }
    bool has_structural_edits() const {
        return bool(structural_);
    }
    ModelDocument preview_model() const;
    std::map<std::size_t, Bytes> compiled_members() const;
    SkinnedModel geometry() const;
    ModelExchange model_exchange() const;
    std::vector<Bytes> package_members() const;
    void import_native_members(const std::map<std::size_t, Bytes> &members);
    FaceMaterialEdit assign_material_faces(const MaterialFaces &faces, std::size_t material);
    MotionExchange motion_exchange(std::size_t motion) const;
    void import_motion_exchange(std::size_t motion, const MotionExchange &replacement);
    void edit_uvs(unsigned channel, const MeshUvEdits &edits);
    void import_model_exchange(const ModelExchange &replacement);
    void import_new_model(const ModelExchange &replacement,
                          const std::vector<std::size_t> &materials);
    void edit_skeleton(const std::vector<Joint> &joints);
    void edit_visibility_motion(std::size_t index, const VisibilityMotion &replacement);
    void edit_geometry(const SkinnedModel &replacement);
    void edit_skeletal_motion(std::size_t motion, const SkeletalMotion &replacement);
    void edit_material_motion(std::size_t motion, const MaterialMotion &replacement);
    std::string borrow_effect(std::size_t material, const MaterialDocument &donor,
                              const std::vector<std::size_t> &passes, bool keep_original = false);
    std::string serialize() const;
    void restore(const std::string &text);
    Bytes compile() const;
    Bytes fragment_resource(std::size_t material) const;
    void export_to(const std::filesystem::path &folder, bool replace_existing = false) const;
    void write_archive(const std::filesystem::path &archive, bool replace_existing) const;
    void write_to_dump(const std::filesystem::path &dump) const;
    std::filesystem::path archive_path() const;
    bool compatible_model(View bytes) const;
    std::size_t write_count() const {
        return touched_map_motions_.size() + touched_camera_.size() +
               std::size_t(touched_feeding_) + touched_.size() + touched_textures_.size() +
               touched_refresh_.size() + (structural_ ? structural_->originals.size() : 0);
    }
    std::size_t texture_write_count() const {
        return touched_textures_.size();
    }
    void borrow_shader(std::size_t material, const ModelDocument &donor, std::size_t donor_material,
                       bool include_material_state);
    void replace_texture(const std::string &name, const TextureImage &image,
                         std::optional<TextureFormat> format = std::nullopt);
    void replace_encoded_texture(const std::string &name, View encoded);
    void add_encoded_texture(const std::string &name, View encoded);
    Bytes texture_resource(const std::string &name) const;
    void add_material(std::size_t donor, const std::string &name);
    void make_material_unique(std::size_t donor, const std::string &name,
                              const std::set<int> &draws);
    void remove_material(std::size_t material, std::size_t replacement);
    void add_texture(const std::string &name, const TextureImage &image,
                     TextureFormat format = TextureFormat::RGBA8);
    void remove_texture(const std::string &name, const std::string &replacement);
    void reset_texture(const std::string &name);
    std::set<std::filesystem::path> archive_paths() const;
    void apply_to(Environment &scene) const;
    std::string identity() const;

  private:
    void set_map_motion(std::size_t index, Bytes bytes);
    void synchronize_map_motions();
    void apply_map_motions(Environment &scene) const;
    using MapMotionEdits = std::map<std::size_t, Bytes>;
    MapMotionEdits map_motion_edits_, saved_map_motions_;
    std::set<std::size_t> touched_map_motions_;
    mutable std::map<std::size_t, std::set<std::string>> accepted_map_motions_;
    Bytes merge_map_motion(std::size_t index, View destination) const;
    void synchronize();
    void synchronize_refresh();
    void synchronize_feeding();
    std::optional<RefreshFeedingParameters> feeding_edit_, saved_feeding_;
    bool touched_feeding_ = false;
    std::set<unsigned> touched_camera_;
    using CameraEdits = std::map<unsigned, RefreshCamera>;
    CameraEdits camera_edit_, saved_camera_;
    mutable std::map<unsigned, std::set<std::string>> accepted_camera_;
    std::shared_ptr<const RefreshFeedingData> initial_feeding_;
    mutable std::set<std::string> accepted_feeding_;

    Bytes merge_refresh(std::size_t mask, View destination) const;
    using RefreshEdits = std::map<std::size_t, Bytes>;
    RefreshEdits refresh_edits_, saved_refresh_;
    std::set<std::size_t> touched_refresh_;
    mutable std::map<std::size_t, std::set<std::string>> accepted_refresh_;
    std::shared_ptr<const RefreshRegionPack> initial_refresh_;
    std::string serialize_materials() const;
    void adopt_structure(MaterialDocument next);
    void replace_members(const std::map<std::size_t, Bytes> &members);
    struct StructuralSession {
        std::map<std::size_t, Bytes> originals;
        std::map<std::size_t, std::set<std::string>> accepted;
        std::shared_ptr<MaterialDocument> saved;
        std::uint64_t saved_version = 0, next_version = 1;
    };
    std::shared_ptr<StructuralSession> structural_;
    std::shared_ptr<MaterialDocument> structural_parent_, structural_redo_;
    std::uint64_t structural_version_ = 0, model_revision_ = 0;

    const AssetResourceLink &fragment_link(std::size_t material) const;
    std::set<std::size_t> touched_;
    std::vector<std::string> texture_names_;
    std::vector<MaterialEdit> initial_, saved_, edits_;
    using TextureEdits = std::map<std::string, std::shared_ptr<const Bytes>>;
    TextureEdits texture_edits_, saved_textures_;
    std::set<std::string> touched_textures_;
    struct State {
        std::vector<MaterialEdit> materials;
        TextureEdits textures;
        RefreshEdits refresh;
        std::optional<RefreshFeedingParameters> feeding;
        CameraEdits camera;
        MapMotionEdits map_motions;
    };
    void write_targets(const std::map<std::filesystem::path, std::filesystem::path> &targets,
                       bool replace_existing) const;
    std::vector<State> history_;
    std::size_t cursor_ = 0;
    std::uint64_t revision_ = 0, texture_revision_ = 0;
};
Bytes asset_resource(View member, const std::vector<std::size_t> &path);
Bytes replace_asset_resource(View member, const std::vector<std::size_t> &path, View replacement);
}
