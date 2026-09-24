#pragma once
#include "minigames/ride_route.h"
#include <array>
#include <cstdint>
#include <random>
#include <vector>
namespace studio {
enum class RideObjectKind { Energy, Obstacle, Wormhole };
struct RideObject {
    RideObjectKind kind = RideObjectKind::Energy;
    double distance = 0;
    float x = 0, y = 0;
    unsigned type = 0, rarity = 0;
    bool consumed = false;
};
struct RideTuning {
    float initial_energy = .7f, drain_frames = 1200, pickup_energy = .1f, obstacle_loss = .2f,
          minimum_speed = 50, maximum_speed = 120;
    float steering = 12, pickup_radius = 180, obstacle_radius = 130, wormhole_radius = 170,
          attraction = .06f;
    float spacing = 1500;
    unsigned obstacle_percent = 25, wormhole_percent = 15;
    bool curved = true, distance_rules = true;
    double start_distance = 0;
    bool story = false;
    unsigned story_ride = 0;
    bool valid() const;
};
struct RidePlacementBand {
    double begin, end;
    float energy_begin, energy_end, obstacle_probability;
    unsigned energy_trials, obstacle_trials, wormhole_trials;
};
struct RideResult {
    bool entered = false;
    unsigned type = 0, rarity = 0;
    double distance = 0;
};
class WarpRide {
  public:
    static constexpr double tick_seconds = 1.0 / 30.0;
    static constexpr float rail_radius = 450;
    RideTuning tuning;
    RideRoute route;
    float x = 0, y = 0, energy = .7f, energy_cap = 1;
    double distance = 0;
    std::uint64_t ticks = 0;
    unsigned pickups = 0, hits = 0;
    std::vector<RideObject> objects;
    RideResult result;
    void restart(std::uint32_t seed, const RideTuning &settings);
    void tick(float horizontal, float vertical);
    unsigned advance(double seconds, float horizontal, float vertical);
    float speed() const;
    static const RidePlacementBand &placement_band(double distance);
    static unsigned rarity_band(double distance);
    static const char *type_name(unsigned type);

  private:
    std::mt19937 random_;
    double accumulator_ = 0, next_spawn_ = 6000;
    unsigned random_below(unsigned limit);
    unsigned weighted(const unsigned *weights, unsigned count);
    void generate();
    void spawn(RideObjectKind kind, double at, bool spread);
};
}
