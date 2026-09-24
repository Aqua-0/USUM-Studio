#pragma once
#include "field/overworld_document.h"
#include "native/project_binding.h"
#include "native/renderer.h"
#include "assets/model_library.h"
#include <future>
#include <imgui.h>
namespace studio {
class OverworldEditor {
  public:
    explicit OverworldEditor(EnvironmentRenderer &renderer) : renderer_(renderer) {
    }
    void set_scene(std::shared_ptr<Environment>, unsigned area, const std::filesystem::path &dump);
    void draw(ViewportCamera &camera, bool loading, const std::function<void()> &stage_reload,
              const SpatialPoint *cursor = nullptr, bool show_launcher = true);
    std::shared_ptr<Environment> poll_preview(bool loading,
                                              const std::function<void()> &refresh_transforms);
    void inspect(const OverworldReference &reference);
    std::function<void()> prepare_save;
    void adopt_preview(std::shared_ptr<Environment> scene) {
        scene_ = std::move(scene);
    }
    OverworldDocument *working_document() {
        return document_.get();
    }
    std::uint64_t take_preview_focus() {
        auto id = preview_focus_;
        preview_focus_ = 0;
        return id;
    }
    bool draft_active() const {
        return editing_;
    }
    bool preview_busy() const {
        return preview_job_.valid();
    }
    bool shape_active() const;
    bool patrol_active() const;
    bool gizmo(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
               bool hovered);
    bool interaction_target_available(const OverworldReference &reference) const {
        return !pending() ||
               (document_ && document_->interaction_target_available(reference.editor_id));
    }
    bool pending() const {
        return document_ && (document_->changed() || preview_job_.valid() ||
                             (preview_document_ && preview_document_->changed()));
    }

  private:
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<OverworldDocument> document_, preview_document_;
    ProjectBinding binding_;
    std::filesystem::path dump_;
    unsigned area_ = 0;
    bool open_ = false, editing_ = false, flag_reserved_ = false;
    std::uint64_t selected_ = 0, preview_focus_ = 0;
    int filter_ = 0;
    std::string preview_source_, preview_requested_;
    unsigned scene_generation_ = 0, preview_generation_ = 0;
    std::future<Environment> preview_job_;
    char search_[128]{}, dialogue_[2049]{};
    int dialogue_mode_ = 0;
    std::string message_;
    OverworldOperation draft_;
    std::vector<PlacementVolumeGroup> shape_groups_;
    void shapes(ViewportCamera &camera);
    void patrol_controls(const SpatialPoint *cursor);
    bool patrol_gizmo(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
                      bool hovered);
    TrainerPatrol patrol_draft_, patrol_drag_;
    std::vector<TrainerPatrol> patrol_undo_, patrol_redo_;
    bool patrol_editing_ = false;
    int patrol_point_ = 0, patrol_axis_ = -1;
    ImVec2 patrol_mouse_{}, patrol_direction_{};
    float patrol_handle_ = 100;
    void remember_patrol(TrainerPatrol before);
    void undo_patrol(bool redo);
    bool loading_ = false, gizmo_blocked_ = false;
    int shape_group_ = -1, shape_index_ = -1, shape_mode_ = 0, shape_endpoint_ = 0, drag_axis_ = -1;
    unsigned drag_entry_ = 0;
    float handle_length_ = 100;
    ImVec2 drag_mouse_{}, drag_direction_{};
    PlacementVolume drag_volume_;
    using ShapeEdits = std::map<unsigned, std::vector<PlacementVolume>>;
    ShapeEdits drag_shapes_;
    std::vector<ShapeEdits> shape_undo_, shape_redo_;
    void undo_shape(bool redo);
    void remember_shapes(ShapeEdits before);
    void cancel_shape_drag();
    std::vector<std::string> items_;
    std::vector<LibraryModel> models_;
    std::future<std::vector<LibraryModel>> model_job_;
    std::vector<unsigned> props_;
    void begin(OverworldOperation::Action);
};
}
