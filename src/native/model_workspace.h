#pragma once
#include "assets/asset_package.h"
#include "native/uv_motion_editor.h"
#include "assets/model_document.h"
#include "assets/battle_effects.h"
#include "scene/battle_sendout.h"
#include "assets/model_library.h"
#include "assets/clothing.h"
#include "native/material_inspector.h"
#include "native/material_editor.h"
#include "native/material_motion_editor.h"
#include "native/skeletal_motion_editor.h"
#include "native/bone_gizmo.h"
#include "native/skeleton_editor.h"
#include "native/model_exchange_editor.h"
#include "native/pokemon_bundle_editor.h"
#include "native/character_registration_editor.h"
#include "native/visibility_motion_editor.h"
#include "native/geometry_editor.h"
#include "native/refresh_inspector.h"
#include "native/camera.h"
#include "native/pokemon_settings_editor.h"
#include "native/folder_picker.h"
#include <future>
#include <utility>
#include <imgui.h>
struct SDL_Window;
namespace studio {
class CryEditor;
class ModelWorkspace {
  public:
    ModelWorkspace(const std::filesystem::path &shaders, SDL_Window *window, bool studio = false);
    ~ModelWorkspace();
    void set_cry_editor(CryEditor *editor) {
        cry_editor_ = editor;
    }
    void request_leave(std::function<void()> action);
    void battle_preview() {
        pending_battle_ = true;
        settings_editor_.active = true;
    }
    void open_settings(const std::filesystem::path &path) {
        pending_settings_ = path;
    }
    void draw(std::uint32_t frame, const char *dump, const ArchiveSources &archives = {});
    void open_species(const std::filesystem::path &dump, unsigned species, unsigned form = 0,
                      bool female = false, bool shiny = false, const ArchiveSources &archives = {});
    void open_document(ModelDocument document);
    bool source_details_visible() const {
        return inspector_page_ == "Source";
    }
    bool take_character_stage_request() {
        return std::exchange(character_stage_requested_, false);
    }
    bool browsing_pokemon() const {
        return category_ == 0;
    }
    void request_catalog_refresh() {
        refresh_requested_ = true;
    }
    std::unique_ptr<ModelDocument> take_studio_request() {
        return std::move(studio_request_);
    }
    std::unique_ptr<ModelDocument> take_shader_donor() {
        return std::move(shader_donor_);
    }
    void send_to_studio();
    MaterialEditor &editor() {
        return *editor_;
    }
    bool apply(const MaterialDocument &document);
    bool ready() const {
        return document_ && renderer_.ready() && !job_.valid() && settings_editor_.ready();
    }
    std::string report() const {
        return document_ ? document_->report() + settings_editor_.report() : error_;
    }
    const std::string &error() const {
        return error_;
    }

  private:
    std::array<ProjectBinding, 4> palette_projects_;
    void bind_palette(unsigned profile);
    void browse_effects(bool busy);
    void refresh_effects();
    void preview_effect(const EffectResource &resource);
    std::vector<EffectResource> effects_;
    std::future<std::vector<EffectResource>> effects_job_;
    int effect_selected_ = -1, effect_kind_ = 0;
    bgfx::TextureHandle effect_texture_ = BGFX_INVALID_HANDLE;
    unsigned effect_width_ = 0, effect_height_ = 0;
    std::string effect_info_;
    std::optional<EffectResource> effect_environment_;
    std::optional<BattleCameraMotion> effect_camera_;
    float effect_camera_frame_ = 0;
    bool effect_camera_preview_ = false;
    void browse_clothing(bool busy);
    void clothing_leave_dialog();
    std::function<void()> clothing_leave_action_;
    bool clothing_leave_pending_ = false;
    void refresh_clothing();
    void open_clothing(const ClothingSelection &selection, int isolated_part,
                       const std::filesystem::path &dump = {});
    std::array<ClothingSelection, 4> clothing_profiles_ = [] {
        std::array<ClothingSelection, 4> result;
        for (unsigned i = 0; i < 4; ++i) {
            result[i].profile = i;
            result[i].items = ClothingProfile::defaults[i % 2];
        }
        return result;
    }();
    std::array<ClothingPalette, 4> clothing_palettes_;
    int clothing_palette_section_ = 0;
    ClothingCatalog clothing_catalog_;
    std::future<ClothingCatalog> clothing_job_;
    std::shared_ptr<FolderSelection> clothing_dialog_ = std::make_shared<FolderSelection>();
    int clothing_profile_ = 0, clothing_slot_ = 11, clothing_dialog_slot_ = -1;
    bool clothing_assembled_ = true;
    void browse(const char *dump, const ArchiveSources &archives);
    void refresh_library();
    void open_library(const std::filesystem::path &path, ModelCategory category,
                      const LibraryModel &entry);
    int category_ = 0;
    std::array<std::filesystem::path, 6> library_archives_;
    std::vector<LibraryModel> library_;
    std::future<std::vector<LibraryModel>> library_job_;
    std::shared_ptr<FolderSelection> library_dialog_ = std::make_shared<FolderSelection>();
    int library_dialog_category_ = -1;
    ModelDocument studio_asset() const;
    void refresh(const std::filesystem::path &dump, const ArchiveSources &archives);
    void open(const PokemonEntry &entry, bool shiny);
    void fit(int draw = -1);
    void poll();
    void details();
    std::optional<ModelExchange> new_object_geometry_;
    std::unique_ptr<ModelDocument> new_object_template_;
    std::vector<std::size_t> new_object_materials_;
    std::vector<std::pair<std::string, AssetPackage>> object_templates_;
    std::array<char, 128> object_name_{};
    int object_template_index_ = -1;
    ModelExchange object_template_exchange_;
    void object_import_dialog(const std::filesystem::path &dump);
    void asset_actions(const std::filesystem::path &dump);
    void open_independent(AssetPackage package, const std::filesystem::path &dump, const std::string &key = {});
    std::shared_ptr<FolderSelection> asset_dialog_ = std::make_shared<FolderSelection>();
    void viewport(std::uint32_t frame);
    void bones(const float *view, const float *projection, ImVec2 origin, ImVec2 size);
    MaterialMotionEditor motion_editor_;
    UvMotionEditor uv_motion_editor_;
    PokemonSettingsEditor settings_editor_;
    bool pending_battle_ = false;
    std::filesystem::path pending_settings_;
    SkeletalMotionEditor skeletal_motion_editor_;
    BoneGizmo bone_gizmo_;
    SkeletonEditor skeleton_editor_;
    ModelExchangeEditor model_exchange_editor_;
    PokemonBundleEditor bundle_editor_;
    CharacterRegistrationEditor character_registration_;
    bool character_stage_requested_ = false;
    VisibilityMotionEditor visibility_motion_editor_;
    GeometryEditor geometry_editor_;
    bool geometry_tab_ = false;
    int motion_kind_ = 0;
    CryEditor *cry_editor_ = nullptr;
    bool refresh_requested_ = false;
    RefreshInspector refresh_inspector_;
    bool studio_ = false, show_weights_ = false;
    bool pick_additive_ = false;
    std::string inspector_page_ = "Meshes";
    std::unique_ptr<MaterialEditor> editor_;
    std::unique_ptr<ModelDocument> studio_request_, shader_donor_;
    SDL_Window *window_;
    EnvironmentRenderer renderer_;
    ViewportCamera camera_;
    MaterialSelection selection_;
    std::unique_ptr<ModelDocument> document_;
    std::future<ModelDocument> job_;
    std::future<std::vector<PokemonEntry>> catalog_job_;
    std::vector<PokemonEntry> catalog_;
    ArchiveSources archive_sources_;
    std::filesystem::path dump_;
    std::atomic_bool cancel_ = false;
    std::string error_, status_ = "Choose a dump folder, then browse Pokemon.";
    char search_[128]{}, bone_search_[128]{};
    int chosen_ = -1, motion_group_ = 0, bone_ = -1;
    bool animation_controls_open_ = false;
    bool shiny_ = false, playing_ = true, repeat_ = true, show_bones_ = false;
    float speed_ = 1;
    unsigned pending_species_ = 0, pending_form_ = 0;
    bool pending_female_ = false, pending_shiny_ = false;
};
}
