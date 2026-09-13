#pragma once
#include "field/warp_document.h"
#include "native/project_binding.h"
#include "native/renderer.h"
namespace studio {
class WarpEditor {
  public:
    explicit WarpEditor(EnvironmentRenderer &renderer) : renderer_(renderer) {
    }
    void set_scene(std::shared_ptr<Environment> scene, unsigned area,
                   const std::filesystem::path &dump);
    void draw(bool loading);

  private:
    void synchronize();
    EnvironmentRenderer &renderer_;
    std::shared_ptr<Environment> scene_;
    std::unique_ptr<WarpDocument> document_;
    ProjectBinding project_;
    std::filesystem::path dump_;
    std::vector<WarpDestination> destinations_;
    std::string message_;
    char search_[128]{};
    bool open_ = false;
    unsigned area_ = 0;
};
}
