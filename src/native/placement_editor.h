#pragma once
#include "native/project_binding.h"
#include "native/renderer.h"
#include "native/material_inspector.h"
#include "native/folder_picker.h"
#include "field/placement_document.h"
#include "field/overworld_document.h"
#include <functional>
#include <future>
#include <imgui.h>
namespace studio {
class PlacementEditor {
  public:
    PlacementEditor(SDL_Window *window, EnvironmentRenderer &renderer)
        : window_(window), renderer_(renderer) {
    }
    ~PlacementEditor();
    void set_scene(std::shared_ptr<Environment> scene, unsigned area,
                   const std::filesystem::path &dump);
    void set_working_scene(std::shared_ptr<Environment> scene, OverworldDocument *document);
    void refresh_working_transforms();
    void finish_working_edit() {
        if (working_) {
            require(!dragging(), "Finish moving the placement before saving");
            commit();
        }
    }
    bool uses_working_scene() const {
        return working_ != nullptr;
    }
    void draw(const MaterialSelection &selection, const SpatialPoint *cursor = nullptr,
              bool show_controls = true);
    void open_patch(const std::filesystem::path &path);
    bool gizmo(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
               bool hovered);
    void request_leave(std::function<void()> action);
    bool busy() const {
        return export_job_.valid() || dialog_kind_ != 0;
    }
    bool dragging() const {
        return drag_axis_ >= 0;
    }

  private:
    ProjectBinding project_;
    OverworldDocument *working_ = nullptr;
    std::vector<PlacementState> committed_;
    std::vector<float> base_turns_;
    std::vector<Matrix> synchronized_transforms_;
    void commit();
    void bind_project();
    void synchronize();
    void save();
    void poll();
    void open_dialog(int kind);
    SDL_Window *window_;
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<PlacementDocument> document_;
    SpatialScene original_spatial_;
    std::filesystem::path dump_, patch_path_;
    std::uint64_t revision_ = 0;
    int selected_ = -1, mode_ = 0, drag_axis_ = -1, dialog_kind_ = 0;
    bool mouse_blocked_ = false, snap_ = false, leave_modal_ = false, save_then_leave_ = false;
    float step_ = 10, angle_step_ = 15;
    PlacementState drag_start_;
    ImVec2 mouse_start_{}, axis_screen_{};
    float handle_length_ = 100, angle_start_ = 0;
    std::string message_;
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    std::future<std::string> export_job_;
    std::function<void()> leave_action_;
};
}
