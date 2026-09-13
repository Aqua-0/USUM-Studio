#include "native/post_process.h"
#include "native/render_views.h"
#include <algorithm>
namespace studio {
PostProcess::PostProcess(const std::filesystem::path &path) {
    auto shader = [&](const char *name) {
        auto b = read_file(path / name);
        auto h = bgfx::createShader(bgfx::copy(b.data(), narrow(b.size())));
        require(bgfx::isValid(h), "Cannot create post-processing shader");
        return h;
    };
    program_ = bgfx::createProgram(shader("vs_post.bin"), shader("fs_post.bin"), true);
    require(bgfx::isValid(program_), "Cannot link post-processing shaders");
    mask_ = bgfx::createUniform("s_postMask", bgfx::UniformType::Sampler);
    mask_mapping_ = bgfx::createUniform("u_postMaskMapping", bgfx::UniformType::Vec4);
    source_ = bgfx::createUniform("s_postSource", bgfx::UniformType::Sampler);
    blur_ = bgfx::createUniform("s_postBlur", bgfx::UniformType::Sampler);
    options_ = bgfx::createUniform("u_postOptions", bgfx::UniformType::Vec4);
    weights_ = bgfx::createUniform("u_postWeights", bgfx::UniformType::Vec4);
    float vertices[] = {-1, -1, 0, 0, 1, 3, -1, 0, 2, 1, -1, 3, 0, 0, -1};
    if (bgfx::getCaps()->originBottomLeft)
        for (unsigned i = 0; i < 3; ++i)
            vertices[i * 5 + 4] = 1 - vertices[i * 5 + 4];
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    triangle_ = bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), layout);
    require(bgfx::isValid(triangle_), "Cannot create post-processing geometry");
}
PostProcess::~PostProcess() {
    bgfx::destroy(mask_);
    bgfx::destroy(mask_mapping_);
    for (auto h : targets_)
        if (bgfx::isValid(h))
            bgfx::destroy(h);
    bgfx::destroy(triangle_);
    bgfx::destroy(source_);
    bgfx::destroy(blur_);
    bgfx::destroy(options_);
    bgfx::destroy(weights_);
    bgfx::destroy(program_);
}
void PostProcess::outlines(bgfx::FrameBufferHandle target, bgfx::TextureHandle edges,
                           unsigned width, unsigned height, float thickness) {
    auto view = RenderViews::outlines;
    bgfx::setViewName(view, "Outline composite");
    bgfx::setViewFrameBuffer(view, target);
    bgfx::setViewRect(view, 0, 0, std::uint16_t(width), std::uint16_t(height));
    bgfx::setViewTransform(view, nullptr, nullptr);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    float options[] = {3.f, std::clamp(thickness, .5f, 4.f) / width,
                       std::clamp(thickness, .5f, 4.f) / height, 0};
    bgfx::setUniform(options_, options);
    bgfx::setTexture(0, source_, edges,
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                         BGFX_SAMPLER_MAG_POINT);
    bgfx::setVertexBuffer(0, triangle_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(view, program_);
}
bgfx::TextureHandle PostProcess::render(bgfx::TextureHandle color, unsigned width, unsigned height,
                                        const BloomSettings &bloom, bgfx::TextureHandle mask,
                                        std::array<float, 2> mask_scale) {
    if (!bloom.enabled || bloom.strength <= 0)
        return color;
    unsigned reduce = std::min(bloom.reductions, 4u) + 1u;
    unsigned small_width = std::max(1u, width >> reduce),
             small_height = std::max(1u, height >> reduce);
    if (width != width_ || height != height_ || reduce != reduce_) {
        width_ = width;
        height_ = height;
        reduce_ = reduce;
        for (unsigned i = 0; i < 3; ++i) {
            if (bgfx::isValid(targets_[i]))
                bgfx::destroy(targets_[i]);
            targets_[i] = bgfx::createFrameBuffer(
                std::uint16_t(i == 2 ? width : small_width),
                std::uint16_t(i == 2 ? height : small_height), bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            require(bgfx::isValid(targets_[i]), "Cannot allocate bloom target");
        }
    }
    float mapping[] = {mask_scale[0], mask_scale[1], bgfx::getCaps()->originBottomLeft ? 0.f : 1.f,
                       0.f};
    auto radius = std::clamp(bloom.range, 0.f, 64.f);
    float weights[] = {bloom.weight[0], bloom.weight[1], bloom.weight[2], bloom.threshold};
    for (unsigned pass = 0; pass < 3; ++pass) {
        auto view = bgfx::ViewId(RenderViews::bloom + pass);
        bgfx::setViewName(view, pass == 0   ? "Bloom extraction / horizontal blur"
                                : pass == 1 ? "Bloom vertical blur"
                                            : "Bloom composite");
        bgfx::setViewFrameBuffer(view, targets_[pass]);
        bgfx::setViewRect(view, 0, 0, std::uint16_t(pass == 2 ? width : small_width),
                          std::uint16_t(pass == 2 ? height : small_height));
        bgfx::setViewTransform(view, nullptr, nullptr);
        bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0);
        float options[] = {float(pass), radius * .002f, radius * .002f, bloom.strength};
        bgfx::setUniform(options_, options);
        bgfx::setUniform(weights_, weights);
        bgfx::setUniform(mask_mapping_, mapping);
        bgfx::setTexture(2, mask_, mask, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        bgfx::setTexture(0, source_, pass == 1 ? bgfx::getTexture(targets_[0]) : color);
        bgfx::setTexture(1, blur_, pass == 2 ? bgfx::getTexture(targets_[1]) : color);
        bgfx::setVertexBuffer(0, triangle_);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view, program_);
    }
    return bgfx::getTexture(targets_[2]);
}
}
