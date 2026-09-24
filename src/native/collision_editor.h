#pragma once
#include "native/project_binding.h"
#include "field/collision_document.h"
#include "native/renderer.h"
#include "native/folder_picker.h"
#include "native/camera.h"
#include <functional>
#include <future>
#include <imgui.h>
namespace studio {
class CollisionEditor {
  public:
    CollisionEditor(SDL_Window *window, EnvironmentRenderer &renderer)
        : window_(window), renderer_(renderer) {
    }
    void set_scene(std::shared_ptr<Environment> scene, unsigned area,
                   const std::filesystem::path &dump);
    void set_authored_scene(std::shared_ptr<Environment> scene, unsigned area,
                            const std::filesystem::path &dump,
                            std::function<void(std::vector<CollisionState>)> changed,
                            std::function<bool()> dirty);
    void prepare_save();
    bool operation_pending() const {
        return export_.valid() || dialog_kind_ != 0;
    }
    void update();
    bool draw_workspace(bool loading);
    void request_leave(std::function<void()> action);
    void activate(bool active);
    void open_patch(const std::filesystem::path &path);
    void request_focus() {
        focus_requested_ = true;
    }
    bool take_focus_request() {
        bool value = focus_requested_;
        focus_requested_ = false;
        return value;
    }
    std::string report() const;

  private:
    std::function<void(std::vector<CollisionState>)> authored_changed_;
    std::function<bool()> authored_dirty_;
    unsigned authored_revision_ = 0;
    std::unique_ptr<CollisionDocument> authored_exchange_;
    CollisionDocument &exchange_document();
    ProjectBinding project_;
    void bind_project();
    enum class Tool { Select, Box, Move, Rotate };
    void dialog(int kind);
    void save();
    void synchronize();
    void inspector(bool busy);
    void layers(bool busy);
    void toolbar(bool busy);
    void modal();
    bool viewport(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
                  bool hovered);
    std::vector<CollisionVertex> selection_vertices() const;
    void select_mode(bool vertices);
    void select_all();
    void select_connected();
    void select_layer(unsigned kind);
    void select_changed();
    void frame_selection(bool all = false);
    void height_edit(bool absolute, float amount);
    void cancel_drag();
    bool busy() const {
        return loading_ || export_.valid() || dialog_kind_ != 0;
    }
    SDL_Window *window_;
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<CollisionDocument> document_;
    ViewportCamera camera_;
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    std::future<void> export_;
    int dialog_kind_ = 0;
    int inspector_page_ = 0;
    unsigned revision_ = 0, area_ = 0;
    bool focus_requested_ = false, active_ = false, loading_ = false, vertices_mode_ = false,
         shared_ = true, snap_ = false, leave_ = false, save_leave_ = false, obj_imported_ = false;
    bool export_reference_ = true;
    bool context_ = true, filled_ = true, edges_ = true, xray_ = true, attribute_colors_ = true;
    std::array<bool, 5> visible_{true, false, false, false, false};
    Tool tool_ = Tool::Move;
    std::set<unsigned> selection_;
    std::set<CollisionVertex> vertices_;
    float height_ = 0, step_ = 5;
    std::uint32_t raw_attribute_ = 0;
    bool selecting_ = false, selection_add_ = false, mouse_blocked_ = false, drag_valid_ = false;
    ImVec2 mouse_start_{};
    int drag_axis_ = -1;
    SpatialPoint pivot_{};
    float handle_length_ = 0, axis_start_ = 0, last_angle_ = 0, drag_angle_ = 0;
    std::map<CollisionVertex, SpatialPoint> drag_vertices_;
    std::filesystem::path path_;
    std::string message_;
    std::function<void()> leave_action_;
};
}
