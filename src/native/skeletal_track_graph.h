#pragma once
#include "scene/skeleton.h"
#include <array>
#include <optional>
namespace studio {
struct SkeletalGraphResult {
    int frame = -1, key = -1;
    std::optional<SkeletalMotion> edited;
};
class SkeletalTrackGraph {
  public:
    SkeletalGraphResult draw(const SkeletalMotion &, const std::vector<Joint> &,
                             const std::string &identity, float playhead, int &bone, int &channel,
                             bool &playing, bool &show_bones, bool *show_weights = nullptr);

  private:
    bool open_ = false, normalized_ = true, constants_ = false, range_drag_ = false;
    int range_anchor_ = 0;
    std::array<bool, 3> groups_{true, true, true}, axes_{true, true, true};
    std::vector<unsigned> masks_;
    std::string identity_, error_;
    char search_[96]{};
    int first_ = 0, last_ = 0, shift_ = 0, paste_frame_ = 0;
    std::vector<JointTrack> clipboard_;
    float scale_ = 1;
};
}
