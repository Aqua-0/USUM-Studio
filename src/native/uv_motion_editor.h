#pragma once
#include <imgui.h>
#include <utility>
#include "native/material_inspector.h"
#include "assets/material_document.h"
namespace studio {
class UvMotionEditor {
  public:
    void close(EnvironmentRenderer &renderer) {
        open_ = pending_ = transforming_ = false;
        clear_preview(renderer);
    }
    std::optional<std::pair<std::string, int>> take_keyed() {
        return std::exchange(keyed_, {});
    }
    void open(const MaterialSelection &selection, unsigned unit);
    void draw(MaterialDocument &doc, ModelDocument &preview, EnvironmentRenderer &renderer,
              bool &playing, bool &repeat);

  private:
    std::optional<std::pair<std::string, int>> keyed_;
    bool open_ = false, auto_key_ = true, pending_ = false, transforming_ = false,
         mouse_drag_ = false;
    int material_ = -1, unit_ = 0, operation_ = 0, axis_ = -1, frame_ = -1, motion_ = -1;
    std::set<int> meshes_;
    std::array<float, 5> values_{1, 1, 0, 0, 0}, before_{};
    ImVec2 start_{}, pan_{};
    float zoom_ = 1;
    std::string identity_, error_;
    std::uint64_t revision_ = 0;
    std::weak_ptr<Environment> preview_scene_;
    void clear_preview(EnvironmentRenderer &renderer);
};
}
