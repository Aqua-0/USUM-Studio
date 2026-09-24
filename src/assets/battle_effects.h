#pragma once
#include "assets/model_document.h"
namespace studio {
enum class EffectResourceKind {
    Pack,
    Model,
    Texture,
    Motion,
    Particle,
    Shader,
    Environment,
    Unknown
};
struct EffectResource {
    std::size_t member = 0;
    unsigned subfile = 0;
    std::vector<std::size_t> path;
    EffectResourceKind kind = EffectResourceKind::Unknown;
    std::string name, error;
    std::size_t bytes = 0;
};
const char *effect_resource_kind(EffectResourceKind kind);
std::vector<EffectResource> load_effect_catalog(const std::filesystem::path &dump,
                                                std::atomic_bool *cancel = nullptr);
Bytes read_effect_resource(const std::filesystem::path &dump, const EffectResource &resource);
ModelDocument load_effect_model(const std::filesystem::path &dump, std::size_t member,
                                unsigned subfile, const std::vector<std::size_t> &selected = {},
                                const std::vector<EffectMotionSource> &motions = {},
                                std::atomic_bool *cancel = nullptr,
                                const std::map<std::size_t, Bytes> &replacements = {});
ModelDocument load_effect_particles(const std::filesystem::path &dump,
                                    const EffectResource &resource);
}
