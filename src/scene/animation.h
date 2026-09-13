#pragma once
#include "core/binary.h"
#include <array>
#include <memory>
namespace studio {
struct AnimationKey {
    float frame, value, slope;
    bool operator==(const AnimationKey &) const = default;
};
struct AnimationCurve {
    std::vector<AnimationKey> keys;
    bool operator==(const AnimationCurve &) const = default;
    float sample(float frame, float fallback) const;
};
AnimationCurve decode_animation_curve(View bytes, std::size_t &cursor, unsigned mode,
                                      unsigned frames);
struct MaterialTrack {
    enum class Kind { TextureTransform, ConstantColor, TexturePattern };
    Kind kind = Kind::TextureTransform;
    std::string material;
    unsigned slot = 0;
    std::array<AnimationCurve, 5> curves;
    struct TextureKey {
        unsigned frame;
        std::string texture;
        bool operator==(const TextureKey &) const = default;
    };
    std::vector<TextureKey> textures;
    bool operator==(const MaterialTrack &) const = default;
};
struct MaterialMotion {
    float frames = 0;
    bool looping = false;
    std::vector<MaterialTrack> tracks;
    bool operator==(const MaterialMotion &) const = default;
};
MaterialMotion decode_material_motion(View bytes);
struct SceneModelSource;
struct MaterialAnimation {
    std::string name, texture_prefix;
    MaterialMotion motion;
    bool daily = false;
    struct Binding {
        std::size_t track, material;
    };
    std::vector<Binding> bindings;
    std::shared_ptr<const SceneModelSource> source;
    double seconds_offset = 0;
};
struct VisibilityTrack {
    std::string mesh;
    std::vector<bool> frames;
    bool operator==(const VisibilityTrack &) const = default;
};
struct VisibilityMotion {
    MaterialMotion clock;
    std::vector<VisibilityTrack> tracks;
    bool operator==(const VisibilityMotion &) const = default;
};
VisibilityMotion decode_visibility_motion(View bytes);
struct VisibilityAnimation {
    std::string scope;
    VisibilityMotion motion;
    bool daily = false, override_previous = false;
    double seconds_offset = 0;
};
float animation_frame(const MaterialMotion &motion, double seconds, bool daily, float hour);
}
