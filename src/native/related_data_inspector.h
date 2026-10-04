#pragma once
#include "native/material_inspector.h"
#include "field/warp_document.h"
#include "field/interaction_source.h"
#include <future>
namespace studio {
struct RelatedMapTarget {
    unsigned area, local_zone, row, category, event;
    int zone;
};
class RelatedDataInspector {
  public:
    void draw(const Environment &, const std::filesystem::path &, unsigned area,
              EnvironmentRenderer &);
    std::optional<RelatedMapTarget> take_request() {
        auto result = request_;
        request_.reset();
        return result;
    }

  private:
    std::filesystem::path source_;
    ArchiveSources archives_;
    bool doors_scanned_ = false, uses_scanned_ = false;
    std::future<std::vector<WarpDestination>> doors_loading_;
    std::vector<WarpDestination> doors_;
    std::vector<EntranceBehavior> behaviors_;
    std::future<std::vector<InteractionUse>> uses_loading_;
    std::vector<InteractionUse> uses_;
    std::string identity_, error_;
    std::optional<RelatedMapTarget> request_;
};
bool related_data_inspector(const Environment &, MaterialSelection &, EnvironmentRenderer &);
}
