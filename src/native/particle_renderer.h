#pragma once
#include "scene/particles.h"
#include <bgfx/bgfx.h>
namespace studio {
class ParticleRenderer {
  public:
    explicit ParticleRenderer(const std::filesystem::path &shaders);
    ~ParticleRenderer();
    void set_scene(const std::vector<WeatherParticles> &weather);
    void render(bgfx::ViewId view, const std::vector<WeatherParticles> &weather, unsigned effect,
                double seconds, const std::array<float, 3> &origin, const float *camera);
    std::size_t particle_count = 0;

  private:
    bgfx::ProgramHandle program_;
    bgfx::UniformHandle sampler_;
    bgfx::VertexLayout layout_;
    std::vector<std::vector<bgfx::TextureHandle>> textures_;
};
}
