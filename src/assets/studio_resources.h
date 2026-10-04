#pragma once
#include "assets/model_document.h"
namespace studio {
enum class StudioResourceKind { Mesh, Material, Texture, Bone, Motion, Other };
struct StudioResource {
    StudioResourceKind kind = StudioResourceKind::Other;
    std::string key, name, detail;
    int index = -1, skeleton = -1;
    std::vector<std::string> uses, used_by;
};
std::vector<StudioResource> studio_resources(const ModelDocument &);
const char *studio_resource_kind_name(StudioResourceKind);
struct StudioResourceChange {
    StudioResourceKind kind = StudioResourceKind::Other;
    std::string key, name, detail;
    bool removed = false;
};
class StudioResourceHistory {
  public:
    void visit(const std::string &key);
    std::string back();
    std::string forward();
    bool can_back() const {
        return position_ > 0;
    }
    bool can_forward() const {
        return !entries_.empty() && position_ + 1 < entries_.size();
    }
    std::string current() const {
        return entries_.empty() ? std::string{} : entries_[position_];
    }

  private:
    std::vector<std::string> entries_;
    std::size_t position_ = 0;
};
}
