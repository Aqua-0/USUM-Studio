#pragma once
#include "assets/material_document.h"
#include "native/renderer.h"
namespace studio {
class SkeletalMotionEditor {
  public:
    void draw(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
              bool &playing, bool &repeat, int &bone, bool &show_bones);

  private:
    bool graph_expanded_ = false;
    std::string identity_, error_;
    int channel_ = 6, key_frame_ = 0;
    float value_ = 0, slope_ = 0;
    char search_[96]{};
};
}
