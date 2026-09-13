#pragma once
#include "formats/texture.h"
#include <array>
namespace studio {
struct ParticleEmitter {
    std::string name;
    TextureImage texture;
    unsigned blend = 0, billboard = 0, volume = 0;
    unsigned addressing = 0;
    std::array<float, 2> uv_scale{1, 1};
    unsigned tiles_x = 1, tiles_y = 1, pattern_count = 1, pattern_frequency = 1;
    bool random_pattern = false, animate_pattern = false;
    std::array<unsigned, 32> pattern{};
    float rate = 0, life = 1, life_random = 0, start = 0, end = 0, velocity = 0,
          velocity_random = 0, alpha = 1, alpha_middle = 1, alpha_end = 1, alpha_split = .5f,
          scale_split = .5f, scale_random = 0;
    std::array<float, 3> translation{}, radius{}, direction{}, gravity{};
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 2> size{1, 1}, scale{1, 1}, scale_middle{1, 1}, scale_end{1, 1};
};
std::vector<ParticleEmitter> decode_particle_emitters(View bytes,
                                                      std::vector<std::string> &diagnostics);
struct WeatherParticles {
    unsigned effect = 0;
    std::vector<ParticleEmitter> emitters;
};
std::vector<WeatherParticles> load_weather_particles(const std::filesystem::path &dump,
                                                     std::vector<std::string> &diagnostics);
struct ParticleVertex {
    float x, y, z, u, v;
    std::uint32_t color;
};
std::vector<ParticleVertex> particle_vertices(const ParticleEmitter &emitter, double seconds,
                                              const std::array<float, 3> &origin,
                                              const std::array<float, 3> &right,
                                              const std::array<float, 3> &up, float light = 1);
}
