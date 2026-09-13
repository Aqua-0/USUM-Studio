#pragma once
#include "native/project_binding.h"
#include "native/renderer.h"
#include "field/weather_document.h"
namespace studio {
class WeatherEditor {
  public:
    void load(const std::filesystem::path &dump);
    void draw(LightingContext &context, EnvironmentRenderer &renderer, int &preview);
    void update(Environment &scene, EnvironmentRenderer &renderer, int &preview);
    void manual_preview() {
        follow_time_ = false;
    }
    bool follows_time() const {
        return follow_time_;
    }

  private:
    ProjectBinding project_;
    std::unique_ptr<WeatherDocument> document_;
    std::filesystem::path dump_;
    std::string error_;
    bool follow_time_ = true;
};
}
