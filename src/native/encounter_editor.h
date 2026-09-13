#pragma once
#include "field/encounter_document.h"
#include "native/project_binding.h"
#include "native/renderer.h"
#include <imgui.h>
namespace studio {
class EncounterEditor {
  public:
    explicit EncounterEditor(EnvironmentRenderer &renderer) : renderer_(renderer) {
    }
    bool gizmo(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
               bool hovered);
    bool active() const {
        return open_ && selected_ >= 0;
    }
    bool pending() const {
        return document_ && document_->changed();
    }
    void set_scene(std::shared_ptr<Environment>, unsigned area, const std::filesystem::path &dump);
    void draw(bool loading, ViewportCamera &camera, const std::function<void()> &stage_reload,
              const SpatialPoint *cursor = nullptr);

  private:
    void synchronize();
    void select(int index);
    void refresh_table();
    void ensure_editable();
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<EncounterDocument> document_;
    ProjectBinding binding_;
    unsigned area_ = 0;
    std::map<unsigned, int> zone_ids_;
    std::vector<std::string> names_;
    bool open_ = false, night_ = false;
    int selected_ = -1, shape_index_ = 0;
    EncounterRegion region_draft_;
    EncounterShape shape_draft_;
    EncounterPeriod table_draft_;
    bool shape_editable_ = false;
    unsigned new_zone_ = 0, new_table_ = 0;
    int mode_ = 0, drag_axis_ = -1;
    bool blocked_ = false, loading_ = false;
    EncounterShape drag_start_;
    ImVec2 mouse_start_{}, axis_screen_{};
    float handle_length_ = 100, angle_start_ = 0;
    std::string message_;
    char search_[128]{};
};
}
