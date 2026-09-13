#pragma once
#include "native/project_binding.h"
#include "native/preferences.h"
#include "field/map_creation.h"
#include "field/map_catalog.h"
#include "native/folder_picker.h"
#include <future>
#include <optional>
#include <chrono>
namespace studio {
class ProjectWorkspace {
  public:
    ProjectWorkspace(SDL_Window *, Preferences &, const std::string &directory, bool required);
    ~ProjectWorkspace();
    std::string source() const;
    bool gate() const {
        return required_ && !store_;
    }
    bool tutorials_suspended() const {
        std::lock_guard lock(picker_->mutex);
        return busy() || picker_->pending;
    }
    bool restarting() const {
        return restart_;
    }
    void stage_and_reload();
    void menu(int area);
    void draw();
    void update();
    bool close();

  private:
    bool map_dialog_ = false, map_inherited_ = false;
    int map_template_ = -1, map_entrance_ = -1;
    char map_name_[161] = "New map";
    MapCatalog map_templates_;
    std::optional<MapCreation> map_plan_;
    void draw_map_creation();
    SDL_Window *window_;
    Preferences &preferences_;
    bool required_, reload_after_stage_ = false, restart_ = false, manager_ = false, setup_ = false,
                    creating_ = false;
    std::optional<ProjectStore> store_;
    std::future<ProjectBuildResult> stage_;
    std::future<void> export_;
    bool busy() const {
        return stage_.valid() || export_.valid();
    }
    std::shared_ptr<FolderSelection> picker_ = std::make_shared<FolderSelection>();
    int picking_ = 0;
    char directory_[4096]{}, dump_[4096]{};
    std::string error_, notice_, selection_, diff_, import_target_;
    std::chrono::steady_clock::time_point saved_ = std::chrono::steady_clock::now(),
                                          staged_ = saved_;
    void remember(const std::filesystem::path &);
    void switch_to(const std::filesystem::path &);
    void save();
    void stage();
    void build_game_export();
    void choose(int);
    void reload();
};
}
