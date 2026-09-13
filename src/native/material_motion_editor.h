#pragma once
#include "assets/material_document.h"
#include "native/renderer.h"
namespace studio {
class MaterialMotionEditor {
  public:
    void draw(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
              bool &playing, bool &repeat);

  private:
    std::string identity_, track_, error_, texture_;
    int channel_ = 0, key_frame_ = 0, material_ = 0, kind_ = 0, slot_ = 0;
    char filter_[128]{};
    float value_ = 0, slope_ = 0;
};
}
