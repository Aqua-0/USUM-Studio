#pragma once
#include "field/overworld_document.h"
#include "native/project_binding.h"
#include "native/renderer.h"
#include "assets/model_library.h"
#include <future>
namespace studio {
class OverworldEditor {
  public:
    explicit OverworldEditor(EnvironmentRenderer &renderer) : renderer_(renderer) {
    }
    void set_scene(std::shared_ptr<Environment>, unsigned area, const std::filesystem::path &dump);
    void draw(ViewportCamera &camera, bool loading, const std::function<void()> &stage_reload,
              const SpatialPoint *cursor = nullptr);
    bool pending() const {
        return document_ && document_->changed();
    }

  private:
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<OverworldDocument> document_;
    ProjectBinding binding_;
    std::filesystem::path dump_;
    unsigned area_ = 0;
    bool open_ = false, editing_ = false, flag_reserved_ = false;
    int selected_ = -1, filter_ = 0;
    char search_[128]{};
    std::string message_;
    OverworldOperation draft_;
    std::vector<std::string> items_;
    std::vector<LibraryModel> models_;
    std::future<std::vector<LibraryModel>> model_job_;
    std::vector<unsigned> props_;
    void begin(OverworldOperation::Action);
};
}
