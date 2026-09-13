#pragma once
#include "native/project_binding.h"
#include "native/collision_editor.h"
#include "authoring/composition_document.h"
#include "native/renderer.h"
#include "native/camera.h"
#include "native/folder_picker.h"
#include <functional>
#include <future>
#include <imgui.h>
#include <set>

namespace studio {
class MapAuthoringWorkspace {
  public:
    MapAuthoringWorkspace(const std::filesystem::path &shaders, SDL_Window *window);
    ~MapAuthoringWorkspace();
    void draw(std::uint32_t frame, const std::filesystem::path &dump, unsigned selected_area);
    void open_template(const std::filesystem::path &dump, unsigned area,
                       const std::filesystem::path &composition = {});
    void request_leave(std::function<void()> action);
    bool ready() const {
        return document_ && renderer_.ready() && !busy();
    }
    bool take_collision_request() {
        bool result = collision_request_;
        collision_request_ = false;
        return result;
    }
    void collision_tick(bool active);
    bool draw_collision();
    bool has_collision() const {
        return bool(collision_editor_);
    }
    void open_collision();
    const std::string &error() const {
        return error_;
    }
    std::string report() const;

  private:
    std::unique_ptr<CollisionEditor> collision_editor_;
    bool collision_request_ = false;
    std::string project_ground_base_;
    std::string project_baseline_;
    ProjectBinding project_;
    void bind_project();
    struct Loaded {
        std::shared_ptr<Environment> base;
        std::unique_ptr<CompositionDocument> document;
        std::map<std::size_t, Environment> resources;
        std::filesystem::path dump, path;
        std::vector<GroundTexture> textures;
        std::vector<MapLocation> maps;
    };
    bool busy() const {
        return load_job_.valid() || resource_job_.valid() || library_job_.valid() ||
               review_job_.valid() || dialog_kind_ != 0 ||
               (collision_editor_ && collision_editor_->operation_pending());
    }
    void begin_asset_edit();
    void asset_editor(std::uint32_t frame);
    void asset_editor_viewport(std::uint32_t frame);
    void refresh_asset_editor();
    void trim_asset_faces(bool keep);
    void asset_edit_history(bool redo);
    void remember_asset_edit();
    void save_asset_edit(bool copy);
    void object_exchange_dialog(bool save);
    void object_exchange_result();
    std::shared_ptr<FolderSelection> object_dialog_ = std::make_shared<FolderSelection>();
    int object_dialog_kind_ = 0;
    std::optional<ProjectAsset> object_exchange_state_;
    std::string object_exchange_signature_, object_exchange_path_;
    std::shared_ptr<Environment> asset_edit_geometry_;
    void sync_project_assets();
    bool asset_edit_ = false, asset_box_drag_ = false;
    int asset_selection_mode_ = 0;
    ImVec2 asset_box_start_{};
    ProjectAsset asset_draft_;
    std::shared_ptr<Environment> asset_edit_scene_;
    std::set<std::pair<std::size_t, std::uint32_t>> asset_faces_selected_;
    std::vector<ProjectAsset> asset_history_;
    std::size_t asset_history_cursor_ = 0;
    char asset_name_[121]{};
    std::map<std::size_t, ProjectAsset> project_cache_;
    void poll();
    void browser();
    void details();
    void viewport(std::uint32_t frame);
    void dialogs();
    enum class AuthoringStage { Terrain, Surfaces, Objects, Review };
    enum GroundTool {
        Height,
        Slope,
        Vertices,
        TextureSize,
        Extend,
        RegionPaint,
        Select,
        Sculpt,
        PaintBrush,
        BlendBrush,
        Mesh
    };
    void set_stage(AuthoringStage stage);
    void stage_toolbar();
    void review_tools();
    bool brush_input(ImVec2 origin, ImVec2 size,
                     const std::function<bool(SpatialPoint, ImVec2 &)> &project, bool hovered);
    std::optional<SpatialPoint> ground_pointer(ImVec2 origin, ImVec2 size) const;
    void ground_tools();
    void ground_browser();
    void ground_toolbar();
    void clear_ground_thumbnails();
    bool terrain_handles(ImVec2 origin, ImVec2 size,
                         const std::function<bool(SpatialPoint, ImVec2 &)> &project, bool hovered);
    int terrain_subdivision_ = 0;
    bool terrain_snap_ = false;
    int terrain_selection_mode_ = 0;
    bool terrain_box_ = false, terrain_box_add_ = false;
    ImVec2 terrain_box_start_{};
    std::optional<std::array<std::uint32_t, 3>> terrain_click_;
    std::vector<std::array<std::uint32_t, 3>> terrain_elements_;
    std::vector<std::uint32_t> terrain_selection_;
    SpatialPoint terrain_delta_{0, -100, 0};
    float terrain_smooth_ = .5f, terrain_tolerance_ = .01f, terrain_influence_ = 0;
    bool terrain_lock_boundary_ = true;
    bool vertex_handles(ImVec2 origin, ImVec2 size,
                        const std::function<bool(SpatialPoint, ImVec2 &)> &project, bool hovered);
    bool ground_handles(ImVec2 origin, ImVec2 size,
                        const std::function<bool(SpatialPoint, ImVec2 &)> &project, bool hovered);
    void cancel_ground_drag();
    void load_resource(std::size_t resource);
    void refresh_scene();
    void cancel_preview();
    void preview_transform();
    void select_instance(std::uint64_t id);
    void place_preview();
    void file_dialog(bool save);
    void save();
    SDL_Window *window_;
    EnvironmentRenderer renderer_;
    std::unique_ptr<EnvironmentRenderer> collision_renderer_;
    std::filesystem::path shaders_;
    ViewportCamera map_camera_, asset_camera_;
    std::shared_ptr<Environment> base_, authoring_base_, composed_;
    std::unique_ptr<CompositionDocument> document_;
    std::map<std::size_t, Environment> resources_;
    std::vector<CompositionInstance> shown_;
    std::future<Loaded> load_job_;
    std::future<MapResourceCatalog> library_job_;
    std::future<std::string> review_job_;
    std::vector<MapLocation> library_maps_;
    unsigned library_area_ = 0;
    std::future<std::pair<std::size_t, Environment>> resource_job_;
    std::atomic_bool cancel_ = false;
    std::filesystem::path dump_, path_;
    std::string error_, status_ = "Choose a map in Maps, then start a composition.";
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    int dialog_kind_ = 0;
    bool leave_modal_ = false, save_then_leave_ = false;
    std::function<void()> leave_action_;
    std::vector<GroundTexture> ground_textures_;
    bool ground_mode_ = true;
    int ground_texture_ = -1;
    AuthoringGrid new_ground_grid_{{0, 0}, 100, 8, 8};
    float new_ground_height_ = 0, ground_step_ = 25, ground_level_ = 0;
    int ground_blend_texture_ = -1, ground_blend_direction_ = 0;
    float ground_coverage_ = 1, ground_tilt_step_ = 5;
    std::map<std::string, bgfx::TextureHandle> ground_thumbnails_;
    std::optional<AuthoringTile> ground_vertex_;
    SpatialPoint vertex_delta_{};
    std::array<float, 2> texture_size_{1, 1};
    int extension_cells_ = 1;
    AuthoringStage stage_ = AuthoringStage::Terrain;
    int library_mode_ = 0;
    bool focus_library_ = false;
    float brush_radius_ = 2, brush_hardness_ = .25f;
    int sculpt_mode_ = 0;
    bool brush_selection_ = false;
    GroundBrushStroke brush_stroke_;
    std::optional<SpatialPoint> brush_hover_;
    std::array<int, 2> selection_start_{0, 0}, selection_size_{1, 1};
    int ground_transform_tool_ = Height, ground_handle_ = 0;
    float ground_drag_value_ = 0, ground_influence_cells_ = 0;
    ImVec2 ground_drag_start_{}, ground_drag_axis_{};
    std::optional<GroundSurface> ground_drag_preview_;
    std::optional<AuthoringTile> region_end_;
    bool selecting_ground_ = false;
    char ground_search_[128]{};
    char search_[128]{};
    char asset_section_search_[128]{};
    int kind_ = 0, resource_ = -1;
    std::uint64_t selected_ = 0;
    bool inspect_ = false, show_grid_ = true, cutaway_ = false, preview_ = false;
    float cut_height_ = 0, ground_ceiling_ = 0, height_offset_ = 0;
    std::string grid_key_;
    std::vector<std::pair<SpatialPoint, SpatialPoint>> grid_lines_;
    std::optional<AuthoringTile> tile_;
    PlacementState draft_;
    AuthoringGrid grid_edit_;
    std::uint64_t preview_id_ = 0;
};
}
