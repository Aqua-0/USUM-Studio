#include "minigames/warp_ride.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace studio {
namespace {
constexpr unsigned type_weights[5][5] = {{4, 24, 24, 24, 24},
                                         {16, 21, 21, 21, 21},
                                         {32, 17, 17, 17, 17},
                                         {44, 14, 14, 14, 14},
                                         {100, 0, 0, 0, 0}};
constexpr unsigned rarity_weights[10][4] = {
    {100, 0, 0, 0},  {78, 20, 2, 0},  {66, 30, 4, 0},  {52, 40, 8, 0},  {33, 50, 16, 1},
    {32, 50, 16, 2}, {31, 50, 16, 3}, {30, 50, 16, 4}, {29, 50, 16, 5}, {29, 50, 16, 5}};
}
bool RideTuning::valid() const {
    for (auto v :
         {initial_energy, drain_frames, pickup_energy, obstacle_loss, minimum_speed, maximum_speed,
          steering, pickup_radius, obstacle_radius, wormhole_radius, attraction, spacing})
        if (!std::isfinite(v))
            return false;
    return initial_energy >= 0 && initial_energy <= 2 && drain_frames >= 1 && pickup_energy >= 0 &&
           pickup_energy <= 2 && obstacle_loss >= 0 && obstacle_loss <= 2 && minimum_speed > 0 &&
           maximum_speed >= minimum_speed && maximum_speed <= 500 && steering > 0 &&
           steering <= 100 && pickup_radius > 0 && pickup_radius <= 450 && obstacle_radius > 0 &&
           obstacle_radius <= 450 && wormhole_radius > 0 && wormhole_radius <= 450 &&
           attraction >= 0 && attraction <= 1 &&
           (distance_rules ||
            (spacing >= 500 && spacing <= 10000 && obstacle_percent <= 100 &&
             wormhole_percent <= 100 && obstacle_percent + wormhole_percent <= 100)) &&
           story_ride < 5 && std::isfinite(start_distance) && start_distance >= 0 &&
           start_distance <= 1000000;
}
void WarpRide::restart(std::uint32_t seed, const RideTuning &settings) {
    if (!settings.valid())
        throw std::invalid_argument("Invalid ride settings");
    tuning = settings;
    random_.seed(seed);
    route.seed = seed;
    route.curved = settings.curved;
    x = y = 0;
    energy = settings.initial_energy;
    energy_cap = std::max(1.f, energy);
    distance = settings.start_distance;
    accumulator_ = 0;
    ticks = pickups = hits = 0;
    next_spawn_ = std::max(6000.0, distance + 1500);
    objects.clear();
    result = {};
    generate();
}
unsigned WarpRide::random_below(unsigned limit) {
    const auto threshold = std::uint32_t(-std::uint32_t(limit)) % limit;
    std::uint32_t value;
    do {
        value = std::uint32_t(random_());
    } while (value < threshold);
    return value % limit;
}
unsigned WarpRide::weighted(const unsigned *weights, unsigned count) {
    unsigned sum = 0;
    for (unsigned i = 0; i < count; ++i)
        sum += weights[i];
    auto value = random_below(sum);
    for (unsigned i = 0; i < count; ++i) {
        if (value < weights[i])
            return i;
        value -= weights[i];
    }
    return count - 1;
}
unsigned WarpRide::rarity_band(double d) {
    if (d < 100000)
        return 0;
    return unsigned(std::min(9.0, 1.0 + std::floor((d - 100000) / 50000)));
}
const char *WarpRide::type_name(unsigned type) {
    constexpr const char *names[] = {"White", "Red", "Blue", "Yellow", "Green"};
    return type < 5 ? names[type] : "Unknown";
}
const RidePlacementBand &WarpRide::placement_band(double distance) {
    static constexpr RidePlacementBand bands[] = {{0, 30000, .8f, .8f, 0, 1, 0, 0},
                                                  {30000, 60000, .8f, .4f, .2f, 1, 1, 0},
                                                  {60000, 100000, .4f, .3f, .25f, 1, 1, 1},
                                                  {100000, 150000, .3f, .3f, .35f, 1, 1, 1},
                                                  {150000, 200000, .25f, .25f, .4f, 1, 1, 1},
                                                  {200000, 210000, .2f, .2f, 1, 1, 2, 1},
                                                  {210000, 250000, .2f, .2f, .45f, 1, 1, 1},
                                                  {250000, 260000, .2f, .2f, 1, 1, 2, 1},
                                                  {260000, 300000, .15f, .15f, .5f, 1, 1, 1},
                                                  {300000, 310000, .125f, .125f, 1, 1, 2, 1},
                                                  {310000, 350000, .125f, .125f, .55f, 1, 1, 1},
                                                  {350000, 360000, .1f, .1f, 1, 1, 2, 1},
                                                  {360000, 400000, .1f, .1f, .6f, 1, 1, 1},
                                                  {400000, 410000, .075f, .075f, 1, 1, 2, 1},
                                                  {410000, 450000, .075f, .075f, .65f, 1, 1, 1},
                                                  {450000, 460000, .05f, .05f, 1, 1, 2, 1},
                                                  {460000, 500000, .05f, .05f, .7f, 1, 1, 1},
                                                  {500000, 510000, .05f, .05f, 1, 1, 2, 1},
                                                  {510000, 1e100, 0, 0, .75f, 0, 1, 1}};
    for (const auto &band : bands)
        if (distance < band.end)
            return band;
    return bands[18];
}
void WarpRide::spawn(RideObjectKind kind, double at, bool spread) {
    RideObject object;
    object.kind = kind;
    object.distance = at + (spread ? double(random_below(6001)) : 0);
    do {
        object.x = float(random_below(801)) - 400;
        object.y = float(random_below(801)) - 400;
    } while (object.x * object.x + object.y * object.y > 400 * 400);
    if (kind == RideObjectKind::Wormhole) {
        object.type = weighted(type_weights[tuning.story ? tuning.story_ride : 0], 5);
        object.rarity = weighted(rarity_weights[rarity_band(at)], 4);
        if (tuning.story && object.type == 0)
            object.x = object.y = 0;
    }
    objects.push_back(object);
}
void WarpRide::generate() {
    std::erase_if(objects, [&](const auto &o) {
        return o.distance < distance - 1000;
    });
    while (next_spawn_ < distance + 10000) {
        if (tuning.distance_rules) {
            const auto &band = placement_band(next_spawn_);
            float progress = float((next_spawn_ - band.begin) / (band.end - band.begin));
            float energy_probability =
                band.energy_begin + (band.energy_end - band.energy_begin) * progress;
            auto trial = [&](RideObjectKind kind, unsigned count, float probability) {
                for (unsigned i = 0; i < count; ++i)
                    if (float(random_below(1000000)) / 1000000 < probability)
                        spawn(kind, next_spawn_, true);
            };
            trial(RideObjectKind::Energy, band.energy_trials, energy_probability);
            float obstacle_probability = band.obstacle_probability;
            if (band.begin == 60000)
                obstacle_probability += .05f * progress;
            trial(RideObjectKind::Obstacle, band.obstacle_trials, obstacle_probability);
            trial(RideObjectKind::Wormhole, band.wormhole_trials, 1.f / 3);
            next_spawn_ += 1500;
        } else {
            const auto roll = random_below(100);
            spawn(roll < tuning.wormhole_percent ? RideObjectKind::Wormhole
                  : roll < tuning.wormhole_percent + tuning.obstacle_percent
                      ? RideObjectKind::Obstacle
                      : RideObjectKind::Energy,
                  next_spawn_, false);
            next_spawn_ += tuning.spacing;
        }
    }
    std::stable_sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) {
        return a.distance < b.distance;
    });
}
float WarpRide::speed() const {
    return tuning.minimum_speed +
           (tuning.maximum_speed - tuning.minimum_speed) * std::clamp(energy, 0.f, 1.f);
}
void WarpRide::tick(float horizontal, float vertical) {
    if (result.entered)
        return;
    if (!std::isfinite(horizontal) || !std::isfinite(vertical))
        return;
    horizontal = std::clamp(horizontal, -1.f, 1.f);
    vertical = std::clamp(vertical, -1.f, 1.f);
    const float length = std::max(1.f, std::hypot(horizontal, vertical));
    const float movement =
        tuning.steering * (energy > 0 ? .6f + .4f * std::min(energy, 1.f) : .15f);
    const float old_x = x, old_y = y;
    const double old_distance = distance;
    x += horizontal / length * movement;
    y += vertical / length * movement;
    for (const auto &o : objects) {
        auto ahead = o.distance - distance;
        if (!o.consumed && o.kind == RideObjectKind::Wormhole && ahead > 0 && ahead < 500) {
            auto lateral = std::hypot(o.x - x, o.y - y);
            if (lateral < tuning.wormhole_radius + 120) {
                x += (o.x - x) * tuning.attraction;
                y += (o.y - y) * tuning.attraction;
            }
        }
    }
    const float radius = std::hypot(x, y);
    if (radius > rail_radius) {
        x *= rail_radius / radius;
        y *= rail_radius / radius;
    }
    distance += speed();
    energy = std::max(0.f, energy - 1.f / tuning.drain_frames);
    ++ticks;
    for (auto &o : objects) {
        if (o.consumed || o.distance < old_distance || o.distance > distance)
            continue;
        float fraction = float((o.distance - old_distance) / (distance - old_distance));
        float dx = o.x - (old_x + (x - old_x) * fraction);
        float dy = o.y - (old_y + (y - old_y) * fraction);
        float hit_radius = o.kind == RideObjectKind::Energy     ? tuning.pickup_radius
                           : o.kind == RideObjectKind::Obstacle ? tuning.obstacle_radius
                                                                : tuning.wormhole_radius;
        if (dx * dx + dy * dy > hit_radius * hit_radius)
            continue;
        o.consumed = true;
        if (o.kind == RideObjectKind::Energy) {
            if (energy + tuning.pickup_energy > energy_cap)
                energy_cap = std::min(2.f, energy_cap + .02f);
            energy = std::min(energy_cap, energy + tuning.pickup_energy);
            ++pickups;
        } else if (o.kind == RideObjectKind::Obstacle) {
            energy = std::max(0.f, energy - tuning.obstacle_loss);
            ++hits;
        } else {
            result = {true, o.type, o.rarity, o.distance};
            distance = o.distance;
            break;
        }
    }
    generate();
}
unsigned WarpRide::advance(double seconds, float horizontal, float vertical) {
    if (!std::isfinite(seconds) || seconds < 0 || result.entered)
        return 0;
    accumulator_ += std::min(seconds, .25);
    unsigned count = 0;
    while (accumulator_ + 1e-10 >= tick_seconds) {
        accumulator_ -= tick_seconds;
        tick(horizontal, vertical);
        ++count;
        if (result.entered) {
            accumulator_ = 0;
            break;
        }
    }
    return count;
}
}
