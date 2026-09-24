#pragma once
#include "assets/material_document.h"
#include "native/renderer.h"
namespace studio {
class VisibilityMotionEditor {
  public:
    void draw(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
              bool &playing, bool &repeat);

  private:
    bool graph_expanded_ = false;
    std::string identity_, mesh_, error_;
    int start_ = 0, end_ = 0;
};
}
