#pragma once
#include <utility>
#include "assets/material_document.h"
#include "native/renderer.h"
namespace studio {
class MaterialMotionEditor {
  public:
    void draw(MaterialDocument &document, ModelDocument &preview, EnvironmentRenderer &renderer,
              bool &playing, bool &repeat);

    void focus_uv(const std::string &name, int unit) {
        track_ = name + " / UV transform / " + std::to_string(unit);
        channel_ = 3;
    }
    bool take_mapping_request() {
        return std::exchange(mapping_request_, false);
    }

  private:
    bool mapping_request_ = false;
    bool graph_expanded_ = false;
    std::string identity_, track_, error_, texture_;
    int channel_ = 0, key_frame_ = 0, material_ = 0, kind_ = 0, slot_ = 0;
    int atlas_columns_ = 4, atlas_rows_ = 4, atlas_first_ = 1, atlas_count_ = 8, atlas_hold_ = 1;
    bool atlas_repeat_ = true, atlas_tile_uvs_ = false;
    char filter_[128]{};
    float value_ = 0, slope_ = 0;
};
}
