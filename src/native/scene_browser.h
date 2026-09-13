#pragma once
#include "native/material_inspector.h"
#include "native/camera.h"
namespace studio {
class SceneBrowser {
  public:
    void rebuild(const Environment &scene);
    void draw(const Environment *scene, const EnvironmentRenderer &renderer,
              MaterialSelection &selection, ViewportCamera &camera);

  private:
    struct Object {
        std::string name, search;
        int category = 0;
        std::vector<int> draws;
    };
    std::vector<Object> objects_;
    std::vector<std::string> searches_;
    char filter_[192]{};
    bool problems_only_ = false;
};
}
