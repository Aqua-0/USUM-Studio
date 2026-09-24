#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
namespace studio {
class EnvironmentRenderer;
struct MaterialSelection;
struct ApplicationFrame {
    bool ready = false, has_source = false, map = false;
    std::string error;
    const EnvironmentRenderer *renderer = nullptr;
    const MaterialSelection *selection = nullptr;
    unsigned width = 0, height = 0, frame_limit = 0;
    float fps = 0;
    double cpu_ms = 0;
    std::function<std::string()> describe;
};
class ApplicationObserver {
  public:
    virtual ~ApplicationObserver() = default;
    virtual void scene_changed() {
    }
    virtual std::optional<std::array<unsigned, 2>> pick_pixel(unsigned, unsigned) {
        return {};
    }
    virtual void pick_submitted() {
    }
    virtual bool frame(const ApplicationFrame &) = 0;
};
int run_application(int argc, char **argv, ApplicationObserver *observer = nullptr);
void capture_window_image(const std::string &path);
}
