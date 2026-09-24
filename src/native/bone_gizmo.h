#pragma once
#include "assets/material_document.h"
#include "native/renderer.h"
#include "native/camera.h"
#include <imgui.h>
namespace studio {
class BoneGizmo {
  public:
    void toolbar();
    bool draw(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
              const ViewportCamera &camera, const float *view, const float *projection,
              ImVec2 origin, ImVec2 size, bool hovered, bool &playing, bool &repeat, int &bone);
    void cancel(MaterialDocument &document, ModelDocument &preview, bool repeat);
    bool dragging() const {
        return axis_ >= 0;
    }

  private:
    int mode_ = 0, axis_ = -1, bone_ = -1, motion_ = -1;
    unsigned frame_ = 0;
    std::uint64_t revision_ = 0;
    float start_ = 0, last_angle_ = 0, angle_ = 0, length_ = 1;
    bool changed_ = false, blocked_ = false, keyboard_ = false;
    bool auto_key_ = true, pending_ = false, insert_ = false, discard_ = false;
    SkeletalMotion pending_motion_;
    ImVec2 start_mouse_{};
    Matrix frame_matrix_{};
    std::array<float, 3> pivot_{};
    SkeletalMotion original_, next_;
    std::string identity_, error_;
};
}
