#pragma once
#include "assets/material_document.h"
#include "native/renderer.h"
#include <optional>
namespace studio {
struct StudioResourceRequest {
    enum class Action {
        Select,
        Edit,
        Uvs,
        Frame,
        Isolate,
        Save,
        Pose,
        MaterialMotion,
        VisibilityMotion
    };
    StudioResource resource;
    Action action = Action::Select;
};
class StudioResourceBrowser {
  public:
    void open(bool changes = false) {
        open_ = true;
        changes_ = changes;
        focus_tab_ = true;
    }
    void follow(StudioResourceKind kind, int index, int skeleton = -1);
    std::optional<StudioResourceRequest> draw(MaterialDocument &, const EnvironmentRenderer &,
                                              const std::string &status, bool editable);

  private:
    const MaterialDocument *document_ = nullptr;
    bool open_ = false, changes_ = false, focus_tab_ = false;
    int kind_ = 0;
    char search_[128]{};
    std::string identity_, error_;
    std::uint64_t revision_ = ~std::uint64_t(0), texture_revision_ = ~std::uint64_t(0),
                  model_revision_ = ~std::uint64_t(0);
    std::uint64_t changes_revision_ = ~std::uint64_t(0);
    std::vector<StudioResource> rows_;
    std::vector<StudioResourceChange> changes_list_;
    StudioResourceHistory history_;
    std::optional<StudioResource> pending_follow_;
    const StudioResource *find(const std::string &) const;
};
}
