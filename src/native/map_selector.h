#pragma once
#include "field/map_catalog.h"
#include <future>
#include <optional>
namespace studio {
class MapSelector {
  public:
    void refresh(const std::string &path, const ArchiveSources &archives = {});
    std::optional<MapLocation> draw(int area, int zone);

  private:
    std::string requested_, reading_, error_;
    ArchiveSources requested_archives_, reading_archives_;
    bool pending_ = false;
    std::future<MapCatalog> job_;
    MapCatalog catalog_;
    char search_[160]{};
};
}
