#pragma once
#include "assets/pokemon_effect_points.h"
#include <imgui.h>
namespace studio {
class MaterialDocument;
class PokemonEffectPointEditor {
  public:
    explicit PokemonEffectPointEditor(bool decorations = false) : decorations_(decorations) {}
    void draw(const ModelDocument &model, MaterialDocument *edit);
    bool viewport(const ModelDocument &model, double seconds, bool animated,
                  const float *view, const float *projection, ImVec2 origin, ImVec2 size,
                  bool hovered, MaterialDocument *edit = nullptr, bool *playing = nullptr);
    bool dragging() const { return drag_axis_ >= 0; }
    void cancel_drag() { drag_axis_ = -1; }
    void reset();

  private:
    bool move_gizmo_ = true, drag_blocked_ = false;
    int drag_axis_ = -1, drag_selection_ = -1, drag_motion_ = -1;
    unsigned drag_group_ = 0;
    std::uint64_t drag_revision_ = 0;
    std::string drag_identity_;
    double drag_seconds_ = 0;
    bool drag_animated_ = false;
    ImVec2 drag_mouse_{}, drag_center_{}, drag_direction_{};
    std::array<float, 4> drag_clip_{}, drag_axis_clip_{};
    std::array<float, 3> drag_original_{}, drag_offset_{};
    bool decorations_ = false;
    unsigned group_ = 0;
    int selected_ = 0;
    bool labels_ = true, selected_only_ = false, reveal_selection_ = false;
    std::string error_, new_bone_;
    char new_name_[64]{};
    unsigned new_category_ = 0;
    int new_index_ = 1;
    std::array<float, 3> new_offset_{};
};
}
