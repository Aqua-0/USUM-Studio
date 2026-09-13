#pragma once
#include "field/pickup_document.h"
#include "native/project_binding.h"
#include "native/renderer.h"
namespace studio {
class PickupEditor {
  public:
    explicit PickupEditor(EnvironmentRenderer &renderer) : renderer_(renderer) {
    }
    void set_scene(std::shared_ptr<Environment> scene, unsigned area,
                   const std::filesystem::path &dump);
    void draw(bool loading);

  private:
    void synchronize();
    void synchronize_visuals();
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<PickupDocument> document_;
    ProjectBinding project_;
    std::map<int, Matrix> visual_matrices_;
    std::map<int, SpatialPoint> visual_offsets_;
    SpatialScene original_spatial_;
    std::filesystem::path dump_;
    std::vector<std::string> names_;
    std::string message_;
    char search_[128]{};
    bool open_ = false;
    unsigned area_ = 0;
};
}
