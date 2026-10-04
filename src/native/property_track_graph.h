#pragma once
#include "scene/animation.h"
#include <array>
#include <map>
namespace studio {
struct PropertyGraphTrack {
    std::string name;
    int source = -1, channel = 0, group = 0;
    const AnimationCurve *curve = nullptr;
    struct Step {
        float frame;
        std::string label;
    };
    std::vector<Step> steps;
};
struct PropertyGraphResult {
    enum class Action { None, Retime, Delete, Show, Hide };
    int selected = -1, frame = -1;
    int first = 0, last = 0, shift = 0;
    float scale = 1;
    Action action = Action::None;
    std::vector<int> shown;
};
class PropertyTrackGraph {
  public:
    PropertyGraphResult draw(const std::vector<PropertyGraphTrack> &, const std::string &identity,
                             float duration, float playhead, const std::string &selected,
                             bool visibility, bool &playing, const std::string &error = {});

  private:
    bool open_ = false, normalized_ = true, range_drag_ = false;
    std::array<bool, 3> groups_{true, true, true};
    std::map<std::string, bool> shown_;
    std::string identity_;
    char search_[128]{};
    int first_ = 0, last_ = 0, shift_ = 0, anchor_ = 0;
    float scale_ = 1;
};
MaterialMotion edit_material_graph_range(const MaterialMotion &,
                                         const std::vector<PropertyGraphTrack> &,
                                         const PropertyGraphResult &);
VisibilityMotion edit_visibility_graph_range(const VisibilityMotion &,
                                             const std::vector<PropertyGraphTrack> &,
                                             const PropertyGraphResult &);
}
