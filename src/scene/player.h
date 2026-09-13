#pragma once
#include "scene/spatial.h"
#include "scene/skeleton.h"
#include <optional>
namespace studio {
struct PlayerCamera {
    SpatialPoint eye{}, target{}, up{0, 1, 0};
    float fov = 45;
};
class PlayerController {
  public:
    bool active = false, paused = false;
    int camera_override = -1;
    float camera_override_blend = 0;
    int camera_setting = -1;
    float camera_ratio = 0, support_scale = 1;
    std::map<int, unsigned> camera_variables;
    unsigned appearance = 0, motion = 0;
    int zone = -1, camera_region = -1;
    float yaw = 0, radius = 18, height = 145, run_multiplier = 1;
    void adjust_running_speed(float wheel);
    double seconds = 0;
    SpatialPoint position{};
    PlayerCamera camera;
    std::string notice;
    bool start(const SpatialScene &scene, SpatialPoint spawn, int initial_zone);
    void step(const SpatialScene &scene, float sideways, float forward, bool running,
              bool through_walls, float dt);
    Matrix transform() const;

  private:
    int entered_camera_ = -1, resolved_camera_ = -1;
    bool support_out_ = false;
    float support_time_ = 0, support_from_ = 1;
    CameraSupport support_data_{};
    PlayerCamera transition_from_;
    float transition_time_ = 0, transition_duration_ = 0;
    std::optional<float> ground(const SpatialScene &scene, SpatialPoint p, float rise,
                                float fall) const;
    void collide(const SpatialScene &scene, SpatialPoint &p) const;
    void update_camera(const SpatialScene &scene, float dt, bool snap);
};
std::vector<std::vector<Matrix>> evaluate_scene_poses(const std::vector<SceneSkeleton> &rigs,
                                                      const PlayerController &player,
                                                      double seconds, float hour, bool enabled);
}
