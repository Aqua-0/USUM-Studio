#pragma once
#include "assets/material_document.h"
#include "native/renderer.h"
#include <imgui.h>
namespace studio {
class UvEditor {
  public:
    void draw(MaterialDocument &doc, const EnvironmentRenderer &renderer, int material,
              unsigned unit);

  private:
    struct Point {
        std::size_t mesh, vertex, island;
        ImVec2 uv;
        bool selected = false;
    };
    std::vector<Point> points_;
    std::vector<std::array<std::size_t, 3>> triangles_;
    std::vector<ImVec2> before_;
    std::uint64_t revision_ = ~std::uint64_t(0);
    int material_ = -1;
    unsigned channel_ = 0;
    int selection_ = 1, operation_ = 0;
    bool fit_ = true, dragging_ = false, boxing_ = false, keep_selection_ = false;
    ImVec2 center_{.5f, .5f}, start_{}, pivot_{};
    float zoom_ = 1, angle_ = 0, move_[2]{}, scale_[2]{1, 1};
    std::string error_;
    void sync(MaterialDocument &doc, int material, unsigned channel);
    void apply(MaterialDocument &doc);
    void transform(ImVec2 move, float angle, ImVec2 scale);
};
}
