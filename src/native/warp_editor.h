#pragma once
#include "field/warp_document.h"
#include "native/project_binding.h"
#include "native/renderer.h"
#include <imgui.h>
namespace studio {
class WarpEditor {
  public:
    explicit WarpEditor(EnvironmentRenderer &renderer) : renderer_(renderer) {
    }
    void set_scene(std::shared_ptr<Environment> scene, unsigned area,
                   const std::filesystem::path &dump);
    void draw(bool loading, ViewportCamera &camera, const SpatialPoint *cursor = nullptr,
              bool show_launcher = true);
    bool gizmo(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
               bool hovered);

  private:
    void synchronize();
    int selected_record() const;
    void select(unsigned index);
    void refresh_destinations();
    bool loading_ = false, blocked_ = false;
    int mode_ = 0, shape_ = 0, endpoint_ = 0, drag_axis_ = -1, drag_record_ = -1;
    float handle_length_ = 100, start_angle_ = 0;
    ImVec2 start_mouse_{};
    WarpValues drag_values_;
    SpatialPoint drag_point_{};
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<WarpDocument> document_;
    ProjectBinding project_;
    std::filesystem::path dump_;
    std::vector<WarpDestination> destinations_;
    std::vector<EntranceBehavior> behaviors_;
    std::string behavior_error_;
    std::string message_;
    char search_[128]{};
    bool open_ = false;
    unsigned area_ = 0;
};
}
