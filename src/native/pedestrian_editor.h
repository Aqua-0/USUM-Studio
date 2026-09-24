#pragma once
#include "field/pedestrian_routes.h"
#include "native/project_binding.h"
#include "native/renderer.h"
#include <imgui.h>
#include "assets/model_library.h"
#include <future>
namespace studio {
class PedestrianEditor {
  public:
    explicit PedestrianEditor(EnvironmentRenderer &renderer) : renderer_(renderer) {
    }
    void set_scene(std::shared_ptr<Environment> scene, unsigned area,
                   const std::filesystem::path &dump);
    void select(unsigned zone, unsigned row);
    void draw(ViewportCamera &camera);
    void update(bool active);
    void clear_scene_preview() {
        requested_route_ = -1;
        detach_model();
    }
    bool viewport(const float *view, const float *projection, ImVec2 origin, ImVec2 size,
                  bool hovered);
    bool active = false;
    SpatialRegion route_geometry() const;
    std::string copy_details() const;
    bool pending() const {
        return document_ && document_->dirty();
    }

  private:
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<PedestrianDocument> document_;
    ProjectBinding binding_;
    int route_ = -1, point_ = 0, choice_ = 0, axis_ = -1;
    bool point_click_ = false, playing_ = false, show_points_ = true, show_guide_ = true,
         show_preview_ = true, reverse_ = false;
    float progress_ = 0, speed_ = 100, handle_length_ = 100;
    ImVec2 start_mouse_{};
    PedestrianRoute draft_, drag_start_;
    std::string error_;
    std::filesystem::path dump_;
    unsigned area_ = 0;
    std::future<ModelDocument> model_job_;
    std::unique_ptr<ModelDocument> model_;
    std::string model_error_;
    std::size_t base_draws_ = 0, base_materials_ = 0, base_skeletons_ = 0, base_tables_ = 0;
    double seconds_ = 0;
    unsigned loaded_character_ = 0, loaded_motion_ = 0, requested_character_ = 0,
             requested_motion_ = 0;
    int requested_route_ = -1;
    bool show_model_ = true, animation_speed_ = true;
    AnimationCurve walk_;
    float walk_frames_ = 0;
    void load_model();
    void attach_model(ModelDocument model);
    void detach_model();
    void move_model(bool visible);
    SpatialPoint preview_position() const;
    void apply();
    void synchronize();
    void reveal();
};
}
