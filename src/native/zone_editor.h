#pragma once
#include "field/zone_document.h"
#include "field/map_catalog.h"
#include "native/project_binding.h"
#include "scene/environment.h"
#include "audio/music.h"
namespace studio {
class ZoneEditor {
  public:
    void load(const std::filesystem::path &dump);
    bool draw(const Environment *scene, int loaded_zone, bool launcher, bool loading);

  private:
    std::string zone_label(unsigned zone) const;
    std::unique_ptr<ZoneDocument> document_;
    ProjectBinding project_;
    std::filesystem::path dump_;
    std::vector<std::string> names_;
    std::map<unsigned, MusicTrack> music_;
    std::string error_, name_error_;
    const Environment *scene_ = nullptr;
    unsigned zone_ = 0;
    bool open_ = false, all_zones_ = false;
    char search_[128]{};
};
}
