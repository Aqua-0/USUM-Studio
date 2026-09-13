#include "scene/environment.h"
#include <algorithm>
#include <cmath>
namespace studio {
void update_texture_transform(SceneTexture &input) {
    auto t = input.transform;
    float c = std::cos(t[2]), s = std::sin(t[2]);
    input.row_u = {t[0] * c, -t[0] * s, t[0] * (.5f * s - .5f * c + .5f - t[3]), 0};
    input.row_v = {t[1] * s, t[1] * c, t[1] * (-.5f * s - .5f * c + .5f - t[4]), 0};
}
void evaluate_material_animations(const Environment &scene, std::vector<SceneMaterial> &materials,
                                  double seconds, float hour, bool enabled) {
    if (materials.size() != scene.materials.size())
        materials = scene.materials;
    std::vector<bool> reset(materials.size());
    for (auto &animation : scene.material_animations)
        for (auto binding : animation.bindings)
            if (!reset.at(binding.material)) {
                materials[binding.material] = scene.materials[binding.material];
                reset[binding.material] = true;
            }
    if (!enabled)
        return;
    for (auto &animation : scene.material_animations) {
        float frame = animation_frame(animation.motion, seconds + animation.seconds_offset,
                                      animation.daily, hour);
        for (auto binding : animation.bindings) {
            auto &track = animation.motion.tracks.at(binding.track);
            auto &mat = materials.at(binding.material);
            if (track.kind == MaterialTrack::Kind::TextureTransform) {
                auto &input = mat.inputs.at(track.slot);
                for (unsigned k = 0; k < 5; ++k)
                    input.transform[k] =
                        track.curves[k].sample(std::floor(frame), input.transform[k]);
                update_texture_transform(input);
            } else if (track.kind == MaterialTrack::Kind::ConstantColor) {
                for (auto &stage : mat.combiner.stages) {
                    auto index = std::size_t(&stage - mat.combiner.stages.data());
                    if (mat.constant_assignments[index] != track.slot)
                        continue;
                    for (unsigned k = 0; k < 4; ++k)
                        if (!track.curves[k].keys.empty())
                            stage.constant[k] =
                                std::floor(
                                    std::clamp(track.curves[k].sample(frame, stage.constant[k]),
                                               0.f, 1.f) *
                                    255) /
                                255;
                }
            } else {
                auto next = std::upper_bound(track.textures.begin(), track.textures.end(), frame,
                                             [](float f, const auto &key) {
                                                 return f < key.frame;
                                             });
                if (next == track.textures.begin())
                    continue;
                auto name = animation.texture_prefix + (next - 1)->texture;
                if (scene.textures.contains(name)) {
                    mat.texture_inputs.at(track.slot) = name;
                    if (track.slot == 0)
                        mat.texture = name;
                }
            }
        }
    }
}
}
