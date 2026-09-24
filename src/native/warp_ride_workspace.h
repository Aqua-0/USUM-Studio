#pragma once
#include "minigames/warp_ride.h"
#include "minigames/warp_ride_assets.h"
#include "native/renderer.h"
#include "native/camera.h"
#include <future>
struct SDL_Window;
struct SDL_Gamepad;
namespace studio {
class WarpRideWorkspace {
  public:
    WarpRideWorkspace(const std::filesystem::path &shaders, SDL_Window *window);
    ~WarpRideWorkspace();
    void activate(bool active);
    void draw(const std::filesystem::path &dump);
    bool ready() const {
        return assets_.scene && renderer_.ready() && !loading_.valid();
    }
    const std::string &error() const {
        return error_;
    }

  private:
    struct Instance {
        std::size_t asset, first, count, placement;
        std::int64_t section = -999;
    };
    SDL_Window *window_;
    SDL_Gamepad *gamepad_ = nullptr;
    EnvironmentRenderer renderer_;
    ViewportCamera camera_, free_view_;
    bool free_camera_ = false;
    double free_origin_ = 0;
    void reset_free_camera();
    RideAssets assets_;
    std::vector<Instance> instances_;
    std::future<RideAssets> loading_;
    std::atomic_bool cancel_ = false;
    std::filesystem::path source_, requested_;
    std::string error_;
    WarpRide ride_;
    RideTuning settings_;
    int seed_ = 1, selected_ = 7, mount_ = 1, rider_ = 1;
    float playback_speed_ = .5f;
    bool gamepad_initialized_ = false;
    bool active_ = false, running_ = false, resource_view_ = false, animate_ = true,
         guides_ = false, focused_ = false;
    double animation_seconds_ = 0;
    void load(const std::filesystem::path &dump);
    void install(RideAssets assets);
    void restart();
    void controls();
    void viewport();
    void place(const Instance &instance, float x, float y, double distance);
    void place_course(Instance &instance, std::int64_t section);
};
}
