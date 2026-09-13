#include "native/particle_renderer.h"
#include <cstring>
namespace studio {
ParticleRenderer::ParticleRenderer(const std::filesystem::path &shaders) {
    auto shader = [&](const char *name) {
        auto b = read_file(shaders / name);
        auto h = bgfx::createShader(bgfx::copy(b.data(), narrow(b.size())));
        require(bgfx::isValid(h), "Cannot create particle shader");
        return h;
    };
    program_ = bgfx::createProgram(shader("vs_particle.bin"), shader("fs_particle.bin"), true);
    require(bgfx::isValid(program_), "Cannot link particle shaders");
    sampler_ = bgfx::createUniform("s_particleTexture", bgfx::UniformType::Sampler);
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
}
ParticleRenderer::~ParticleRenderer() {
    set_scene({});
    bgfx::destroy(sampler_);
    bgfx::destroy(program_);
}
void ParticleRenderer::set_scene(const std::vector<WeatherParticles> &weather) {
    for (auto &set : textures_)
        for (auto texture : set)
            if (bgfx::isValid(texture))
                bgfx::destroy(texture);
    textures_.clear();
    for (auto &set : weather) {
        textures_.emplace_back();
        for (auto &emitter : set.emitters) {
            auto &t = emitter.texture;
            auto h = bgfx::createTexture2D(
                t.width, t.height, false, 1, bgfx::TextureFormat::RGBA8,
                (emitter.addressing & 1 ? BGFX_SAMPLER_U_MIRROR : BGFX_SAMPLER_U_CLAMP) |
                    (emitter.addressing & 2 ? BGFX_SAMPLER_V_MIRROR : BGFX_SAMPLER_V_CLAMP),
                bgfx::copy(t.rgba.data(), narrow(t.rgba.size())));
            require(bgfx::isValid(h), "Cannot allocate particle texture");
            textures_.back().push_back(h);
        }
    }
}
void ParticleRenderer::render(bgfx::ViewId view, const std::vector<WeatherParticles> &weather,
                              unsigned effect, double seconds, const std::array<float, 3> &origin,
                              const float *camera) {
    particle_count = 0;
    if (effect == 3)
        effect = 1;
    for (unsigned s = 0; s < weather.size(); ++s)
        if (weather[s].effect == effect)
            for (unsigned i = 0; i < weather[s].emitters.size(); ++i) {
                auto &emitter = weather[s].emitters[i];
                auto vertices =
                    particle_vertices(emitter, seconds, origin, {camera[0], camera[4], camera[8]},
                                      {camera[1], camera[5], camera[9]});
                auto count = narrow(vertices.size());
                if (!count || bgfx::getAvailTransientVertexBuffer(count, layout_) < count)
                    continue;
                bgfx::TransientVertexBuffer buffer;
                bgfx::allocTransientVertexBuffer(&buffer, count, layout_);
                std::memcpy(buffer.data, vertices.data(), vertices.size() * sizeof(ParticleVertex));
                bgfx::setVertexBuffer(0, &buffer);
                bgfx::setTexture(0, sampler_, textures_.at(s).at(i));
                auto blend = emitter.blend == 1   ? BGFX_STATE_BLEND_ADD
                             : emitter.blend == 4 ? BGFX_STATE_BLEND_MULTIPLY
                                                  : BGFX_STATE_BLEND_ALPHA;
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                               BGFX_STATE_DEPTH_TEST_LEQUAL | blend);
                bgfx::submit(view, program_);
                particle_count += vertices.size() / 6;
            }
}
}
