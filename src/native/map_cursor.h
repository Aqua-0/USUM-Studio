#pragma once
#include "native/renderer.h"
#include <imgui.h>
namespace studio {
class MapCursor {
  public:
    void draw(const Environment *scene, ViewportCamera &camera, bool show_launcher = true);
    bool viewport(const Environment &scene, EnvironmentRenderer &renderer, const float *view,
                  const float *projection, ImVec2 origin, ImVec2 size, bool hovered, bool cutaway);
    void begin_placement() {
        enabled_ = true;
        placing_ = true;
    }
    const SpatialPoint *position() const {
        return enabled_ ? &position_ : nullptr;
    }

  private:
    const Environment *scene_ = nullptr;
    bool enabled_ = false, placing_ = false, blocked_ = false;
    int axis_ = -1;
    SpatialPoint position_{}, drag_start_{};
    std::optional<SpatialPoint> start_;
    ImVec2 mouse_start_{}, direction_{};
    float length_ = 100;
    std::string message_;
};
}
