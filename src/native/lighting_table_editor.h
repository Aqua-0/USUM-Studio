#pragma once
#include "assets/material_document.h"
#include <imgui.h>
namespace studio {
class LightingTableEditor {
  public:
    void open(std::size_t material) {
        material_ = material;
        visible_ = focus_ = true;
        revision_ = ~std::uint64_t(0);
    }
    void close() {
        visible_ = dragging_ = false;
    }
    void draw(MaterialDocument &doc, bool editable);

  private:
    bool visible_ = false, focus_ = false, dragging_ = false;
    std::size_t material_ = 0;
    int channel_ = 1, sample_ = 0, previous_ = 0;
    float previous_value_ = 0;
    std::uint64_t revision_ = ~std::uint64_t(0), model_revision_ = ~std::uint64_t(0);
    std::map<std::uint32_t, LightingTable> tables_;
    std::array<MaterialLightingBinding, 3> bindings_{};
    std::vector<std::array<MaterialLightingBinding, 3>> users_;
    LightingTable curve_, before_;
    std::string error_;
};
}
