#pragma once
#include "assets/material_document.h"
#include "native/renderer.h"
namespace studio {
class SkeletonEditor {
  public:
    void draw(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
              bool &playing, int &bone);

  private:
    std::string identity_, error_;
    std::size_t revision_ = 0;
    int selected_ = -1, parent_ = -1;
    std::array<float, 3> translation_{}, rotation_{};
    char name_[64]{};
};
}
