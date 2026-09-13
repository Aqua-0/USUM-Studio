#pragma once
#include "field/weather_document.h"
#include "scene/material.h"
#include "formats/texture.h"
#include <memory>
#include "scene/animation.h"
namespace studio {
using LightKey = AnimationKey;
using LightCurve = AnimationCurve;
struct LightTrack {
    std::string name;
    unsigned set = 0, type = 0;
    std::array<LightCurve, 11> curves;
};
struct FogSettings {
    bool enabled = false;
    float near_distance = 0, far_distance = 10000, strength = 1;
    MaterialColor color{};
};
struct BloomSettings {
    bool enabled = false;
    unsigned reductions = 1;
    float strength = 1, range = 1, threshold = .5f;
    MaterialColor weight{.333333f, .333333f, .333333f, 0};
};
struct EnvironmentEffects {
    std::array<FogSettings, 4> fog;
    BloomSettings bloom;
};
struct FogTrack {
    unsigned slot = 0;
    std::array<AnimationCurve, 6> curves;
};
struct LightMotion {
    std::vector<FogTrack> fog;
    std::array<AnimationCurve, 6> bloom;
    float frames = 0;
    std::vector<LightTrack> tracks;
};
struct SceneLight {
    std::string name;
    unsigned type = 0;
    std::array<float, 3> position{}, direction{};
    MaterialColor color{};
};
struct LightSet {
    unsigned index = 0;
    std::vector<SceneLight> lights;
};
struct WeatherProfile {
    unsigned kind = 0, effect = 0, sky = 0, motion = 0;
};
struct LightingContext {
    std::shared_ptr<const TextureImage> bloom_mask;
    bool sky_enabled = false;
    float sky_height = 0;
    EnvironmentEffects effects;
    std::vector<WeatherProfile> weather;
    WeatherSchedule schedule{};
    unsigned zone = 0;
    std::vector<LightSet> sets;
    std::vector<LightMotion> motions;
};
const WeatherProfile *weather_at_time(const LightingContext &context, float hour);
struct LightingTable {
    std::array<float, 256> values{};
};
std::map<std::uint32_t, LightingTable> model_lighting_tables(const Model &model);
EnvironmentEffects decode_environment_effects(View bytes);
EnvironmentEffects evaluate_environment_effects(const LightingContext &context, unsigned motion,
                                                float hour);
std::vector<LightSet> decode_light_sets(View bytes);
LightMotion decode_light_motion(View bytes);
std::vector<LightSet> evaluate_lights(const LightingContext &context, unsigned motion, float hour);
std::vector<LightingContext> load_area_lighting(const std::filesystem::path &dump, std::size_t area,
                                                View environments);
}
