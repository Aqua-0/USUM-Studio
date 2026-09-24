#pragma once
#include "assets/material_document.h"
#include "native/material_inspector.h"
#include <imgui.h>
namespace studio {
class TexturePainter {
  public:
    ~TexturePainter();
    void open(const std::string &texture);
    void close();
    void draw(MaterialDocument &doc, MaterialSelection &selection, bool editable);

  private:
    void upload();
    bool visible_ = false, focus_ = false, stroke_ = false, changed_ = false, upload_ = true;
    bool overlay_ = true, selected_uvs_ = false;
    int tool_ = 0, channel_ = 0;
    float radius_ = 4, opacity_ = 1, zoom_ = 1;
    std::array<float, 4> color_{1, 1, 1, 1};
    ImVec2 pan_{}, previous_{};
    std::string texture_, error_;
    TextureImage pixels_, before_;
    std::uint64_t revision_ = ~std::uint64_t(0);
    bgfx::TextureHandle preview_ = BGFX_INVALID_HANDLE;
};
}
