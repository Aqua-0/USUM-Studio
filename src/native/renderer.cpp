#include "native/renderer.h"
#include "native/render_views.h"
#include <cstring>
#include <cmath>
#include <set>
namespace studio {
namespace {
std::uint64_t material_state(const SceneMaterial &m) {
    static constexpr std::uint64_t depth[] = {
        BGFX_STATE_DEPTH_TEST_NEVER,    BGFX_STATE_DEPTH_TEST_ALWAYS, BGFX_STATE_DEPTH_TEST_EQUAL,
        BGFX_STATE_DEPTH_TEST_NOTEQUAL, BGFX_STATE_DEPTH_TEST_LESS,   BGFX_STATE_DEPTH_TEST_LEQUAL,
        BGFX_STATE_DEPTH_TEST_GREATER,  BGFX_STATE_DEPTH_TEST_GEQUAL};
    static constexpr std::uint64_t factors[] = {
        BGFX_STATE_BLEND_ZERO,         BGFX_STATE_BLEND_ONE,
        BGFX_STATE_BLEND_SRC_COLOR,    BGFX_STATE_BLEND_INV_SRC_COLOR,
        BGFX_STATE_BLEND_DST_COLOR,    BGFX_STATE_BLEND_INV_DST_COLOR,
        BGFX_STATE_BLEND_SRC_ALPHA,    BGFX_STATE_BLEND_INV_SRC_ALPHA,
        BGFX_STATE_BLEND_DST_ALPHA,    BGFX_STATE_BLEND_INV_DST_ALPHA,
        BGFX_STATE_BLEND_FACTOR,       BGFX_STATE_BLEND_INV_FACTOR,
        BGFX_STATE_BLEND_FACTOR,       BGFX_STATE_BLEND_INV_FACTOR,
        BGFX_STATE_BLEND_SRC_ALPHA_SAT};
    static constexpr std::uint64_t equations[] = {
        BGFX_STATE_BLEND_EQUATION_ADD, BGFX_STATE_BLEND_EQUATION_SUB,
        BGFX_STATE_BLEND_EQUATION_REVSUB, BGFX_STATE_BLEND_EQUATION_MIN,
        BGFX_STATE_BLEND_EQUATION_MAX};
    std::uint64_t state = BGFX_STATE_MSAA | BGFX_STATE_FRONT_CCW;
    auto d = m.depth_state;
    if (d & 0x100)
        state |= BGFX_STATE_WRITE_R;
    if (d & 0x200)
        state |= BGFX_STATE_WRITE_G;
    if (d & 0x400)
        state |= BGFX_STATE_WRITE_B;
    if (d & 0x800)
        state |= BGFX_STATE_WRITE_A;
    if (d & 0x1000)
        state |= BGFX_STATE_WRITE_Z;
    if (d & 1)
        state |= depth[(d >> 4) & 7];
    if (m.cull == 1)
        state |= BGFX_STATE_CULL_CW;
    else if (m.cull == 2)
        state |= BGFX_STATE_CULL_CCW;
    auto blend = m.blend_state;
    if (blend != 0x01010000) {
        unsigned a = (blend >> 16) & 15, b = (blend >> 20) & 15, c = (blend >> 24) & 15,
                 e = (blend >> 28) & 15, rgb = blend & 255, alpha = (blend >> 8) & 255;
        require(a < 15 && b < 15 && c < 15 && e < 15 && rgb < 5 && alpha < 5,
                "Unsupported material blend operation");
        state |= BGFX_STATE_BLEND_FUNC_SEPARATE(factors[a], factors[b], factors[c], factors[e]) |
                 BGFX_STATE_BLEND_EQUATION_SEPARATE(equations[rgb], equations[alpha]);
    }
    return state;
}
std::uint32_t material_stencil(const SceneMaterial &m) {
    auto test = m.stencil_test;
    if (!(test & 1))
        return BGFX_STENCIL_NONE;
    static constexpr std::uint32_t functions[] = {
        BGFX_STENCIL_TEST_NEVER,    BGFX_STENCIL_TEST_ALWAYS, BGFX_STENCIL_TEST_EQUAL,
        BGFX_STENCIL_TEST_NOTEQUAL, BGFX_STENCIL_TEST_LESS,   BGFX_STENCIL_TEST_LEQUAL,
        BGFX_STENCIL_TEST_GREATER,  BGFX_STENCIL_TEST_GEQUAL};
    static constexpr unsigned operations[] = {1, 0, 2, 4, 6, 7, 3, 5};
    auto state = functions[(test >> 4) & 7] | BGFX_STENCIL_FUNC_REF(test >> 16) |
                 BGFX_STENCIL_FUNC_RMASK(test >> 24);
    bool write = m.stencil_write && ((test >> 8) & 255) == 255;
    for (unsigned i = 0; i < 3; ++i)
        state |= operations[write ? ((m.stencil_operations >> (i * 4)) & 7) : 0] << (20 + i * 4);
    return state;
}
bgfx::ShaderHandle shader(const std::filesystem::path &file) {
    auto b = read_file(file);
    auto h = bgfx::createShader(bgfx::copy(b.data(), narrow(b.size())));
    require(bgfx::isValid(h), "Could not create viewport shader " + file.string());
    return h;
}
std::uint32_t wrap(unsigned value, bool u) {
    switch (value) {
    case 0:
        return u ? BGFX_SAMPLER_U_CLAMP : BGFX_SAMPLER_V_CLAMP;
    case 1:
        return u ? BGFX_SAMPLER_U_BORDER : BGFX_SAMPLER_V_BORDER;
    case 2:
        return 0;
    case 3:
        return u ? BGFX_SAMPLER_U_MIRROR : BGFX_SAMPLER_V_MIRROR;
    default:
        return 0;
    }
}
}
EnvironmentRenderer::EnvironmentRenderer(const std::filesystem::path &path)
    : spatial(path), post_(path), particles_(path) {
    program_ = bgfx::createProgram(shader(path / "vs_environment.bin"),
                                   shader(path / "fs_environment.bin"), true);
    require(bgfx::isValid(program_), "Could not link viewport shaders");
    object_basis_ = bgfx::createUniform("u_objectBasis", bgfx::UniformType::Vec4, 2);
    skin_options_ = bgfx::createUniform("u_skin", bgfx::UniformType::Vec4);
    bone_rows_ = bgfx::createUniform("u_boneRows", bgfx::UniformType::Vec4, 93);
    normal_rows_ = bgfx::createUniform("u_normalRows", bgfx::UniformType::Vec4, 93);
    fog_params_ = bgfx::createUniform("u_fog", bgfx::UniformType::Vec4);
    fog_color_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
    fog_depth_ = bgfx::createUniform("u_fogDepth", bgfx::UniformType::Vec4);
    edge_options_ = bgfx::createUniform("u_edgeOptions", bgfx::UniformType::Vec4);
    selection_color_ = bgfx::createUniform("u_selectionColor", bgfx::UniformType::Vec4);
    pick_color_ = bgfx::createUniform("u_pickColor", bgfx::UniformType::Vec4);
    const char *samplers[] = {"s_tex", "s_tex1", "s_tex2"};
    for (unsigned i = 0; i < 3; ++i)
        samplers_[i] = bgfx::createUniform(samplers[i], bgfx::UniformType::Sampler);
    const char *combiners[] = {"u_colorSources",  "u_alphaSources", "u_colorOperands",
                               "u_alphaOperands", "u_operations",   "u_constants",
                               "u_bufferWrites"};
    for (unsigned i = 0; i < 7; ++i)
        combiners_[i] = bgfx::createUniform(combiners[i], bgfx::UniformType::Vec4, 6);
    uv_u_ = bgfx::createUniform("u_uvRowU", bgfx::UniformType::Vec4, 3);
    uv_v_ = bgfx::createUniform("u_uvRowV", bgfx::UniformType::Vec4, 3);
    buffer_ = bgfx::createUniform("u_buffer", bgfx::UniformType::Vec4);
    preview_ = bgfx::createUniform("u_preview", bgfx::UniformType::Vec4);
    cutaway_ = bgfx::createUniform("u_cutaway", bgfx::UniformType::Vec4);
    light_ = bgfx::createUniform("u_lighting", bgfx::UniformType::Vec4);
    light_direction_ = bgfx::createUniform("u_lightDirection", bgfx::UniformType::Vec4);
    surface_ = bgfx::createUniform("u_surface", bgfx::UniformType::Vec4, 5);
    texture_options_ = bgfx::createUniform("u_textureOptions", bgfx::UniformType::Vec4, 3);
    projection_rows_ = bgfx::createUniform("u_projectionRows", bgfx::UniformType::Vec4, 6);
    projection_options_ = bgfx::createUniform("u_projectionOptions", bgfx::UniformType::Vec4);
    projection_offsets_ = bgfx::createUniform("u_projectionOffsets", bgfx::UniformType::Vec4);
    vertex_effect_ = bgfx::createUniform("u_vertexEffect", bgfx::UniformType::Vec4);
    vertex_lighting_ = bgfx::createUniform("u_vertexLighting", bgfx::UniformType::Vec4);
    rim_phong_ = bgfx::createUniform("u_rimPhong", bgfx::UniformType::Vec4);
    vertex_light_direction_ =
        bgfx::createUniform("u_vertexLightDirection", bgfx::UniformType::Vec4);
    bump_ = bgfx::createUniform("u_bump", bgfx::UniformType::Vec4);
    game_light_ = bgfx::createUniform("u_gameLight", bgfx::UniformType::Vec4);
    game_ambient_ = bgfx::createUniform("u_gameAmbient", bgfx::UniformType::Vec4);
    game_directions_ = bgfx::createUniform("u_gameDirections", bgfx::UniformType::Vec4, 8);
    game_colors_ = bgfx::createUniform("u_gameColors", bgfx::UniformType::Vec4, 8);
    lookup_sampler_ = bgfx::createUniform("s_lookup", bgfx::UniformType::Sampler);
    lookup_inputs_ = bgfx::createUniform("u_lookupInputs", bgfx::UniformType::Vec4, 3);
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
        .end();
    std::uint32_t pixel = 0xffffffff;
    white_ =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&pixel, 4));
}
void EnvironmentRenderer::clear() {
    for (auto mask : bloom_masks_)
        if (bgfx::isValid(mask))
            bgfx::destroy(mask);
    bloom_masks_.clear();
    if (bgfx::isValid(scene_copy_))
        bgfx::destroy(scene_copy_);
    scene_copy_ = BGFX_INVALID_HANDLE;
    selected_draw_ = -1;
    highlighted_.clear();
    ++scene_generation_;
    if (bgfx::isValid(lookup_texture_))
        bgfx::destroy(lookup_texture_);
    lookup_texture_ = BGFX_INVALID_HANDLE;
    for (auto &d : draws_) {
        bgfx::destroy(d.vertices);
        bgfx::destroy(d.indices);
        if (bgfx::isValid(d.edges))
            bgfx::destroy(d.edges);
    }
    draws_.clear();
    for (auto &[name, t] : textures_)
        bgfx::destroy(t);
    textures_.clear();
    texture_bytes = geometry_bytes = 0;
}
EnvironmentRenderer::~EnvironmentRenderer() {
    if (bgfx::isValid(edge_target_))
        bgfx::destroy(edge_target_);
    bgfx::destroy(edge_options_);
    if (pick_pending_)
        while (bgfx::frame() < pick_ready_) {
        }
    if (bgfx::isValid(pick_target_))
        bgfx::destroy(pick_target_);
    if (bgfx::isValid(pick_readback_))
        bgfx::destroy(pick_readback_);
    bgfx::destroy(object_basis_);
    bgfx::destroy(skin_options_);
    bgfx::destroy(bone_rows_);
    bgfx::destroy(normal_rows_);
    bgfx::destroy(fog_params_);
    bgfx::destroy(fog_color_);
    bgfx::destroy(fog_depth_);
    bgfx::destroy(pick_color_);
    bgfx::destroy(selection_color_);
    clear();
    if (bgfx::isValid(target_))
        bgfx::destroy(target_);
    bgfx::destroy(white_);
    bgfx::destroy(program_);
    for (auto h : samplers_)
        bgfx::destroy(h);
    for (auto h : combiners_)
        bgfx::destroy(h);
    bgfx::destroy(uv_u_);
    bgfx::destroy(uv_v_);
    bgfx::destroy(buffer_);
    bgfx::destroy(preview_);
    bgfx::destroy(cutaway_);
    bgfx::destroy(projection_rows_);
    bgfx::destroy(projection_options_);
    bgfx::destroy(projection_offsets_);
    bgfx::destroy(vertex_effect_);
    bgfx::destroy(vertex_lighting_);
    bgfx::destroy(rim_phong_);
    bgfx::destroy(vertex_light_direction_);
    bgfx::destroy(light_);
    bgfx::destroy(light_direction_);
    bgfx::destroy(surface_);
    bgfx::destroy(texture_options_);
    bgfx::destroy(bump_);
    bgfx::destroy(game_light_);
    bgfx::destroy(game_ambient_);
    bgfx::destroy(game_directions_);
    bgfx::destroy(game_colors_);
    bgfx::destroy(lookup_sampler_);
    bgfx::destroy(lookup_inputs_);
}
void EnvironmentRenderer::select_draw(int draw) {
    if (draw == selected_draw_)
        return;
    selected_draw_ = draw;
    highlighted_.assign(scene_ ? scene_->draws.size() : 0, false);
    if (!scene_ || draw < 0 || std::size_t(draw) >= scene_->draws.size())
        return;
    auto &selected = scene_->draws[draw];
    auto name = selected.name.substr(0, selected.name.find(" / "));
    for (unsigned i = 0; i < scene_->draws.size(); ++i) {
        auto &part = scene_->draws[i];
        highlighted_[i] =
            i == unsigned(draw) ||
            (highlight_object &&
             (selected.placement >= 0 ? part.placement == selected.placement
                                      : (!name.empty() && part.scope == selected.scope &&
                                         part.name.substr(0, part.name.find(" / ")) == name)));
    }
}
void EnvironmentRenderer::set_draw_visible(std::size_t draw, bool visible) {
    if (draw >= hidden_draws_.size() || hidden_draws_[draw] == !visible)
        return;
    hidden_draws_[draw] = !visible;
    invalidate_selection_readback();
}
void EnvironmentRenderer::show_all_draws() {
    std::fill(hidden_draws_.begin(), hidden_draws_.end(), false);
    invalidate_selection_readback();
}
void EnvironmentRenderer::set_scene(std::shared_ptr<const Environment> scene) {
    if (scene_ == scene)
        return;
    refresh.set_regions({}, {});
    clear();
    scene_ = std::move(scene);
    hidden_draws_.assign(scene_ ? scene_->draws.size() : 0, false);
    spatial.set_scene(scene_);
    particles_.set_scene(scene_ ? scene_->weather_particles : std::vector<WeatherParticles>{});
    materials_ = scene_->materials;
    lighting.context = 0;
    lighting.motion = 0;
    for (const auto &context : scene_->lighting_contexts) {
        bgfx::TextureHandle mask = BGFX_INVALID_HANDLE;
        if (context.bloom_mask) {
            const auto &image = *context.bloom_mask;
            mask = bgfx::createTexture2D(image.width, image.height, false, 1,
                                         bgfx::TextureFormat::RGBA8,
                                         BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                         bgfx::copy(image.rgba.data(), narrow(image.rgba.size())));
            require(bgfx::isValid(mask), "Cannot allocate environment bloom mask");
            texture_bytes += image.rgba.size();
        }
        bloom_masks_.push_back(mask);
    }
}
bool EnvironmentRenderer::ready() const {
    return scene_ && draws_.size() == scene_->draws.size() &&
           textures_.size() == scene_->textures.size();
}
void EnvironmentRenderer::refresh_textures() {
    for (auto &[name, t] : textures_)
        bgfx::destroy(t);
    textures_.clear();
    texture_bytes =
        bgfx::isValid(lookup_texture_) ? std::size_t(lookup_height_) * 256 * sizeof(float) : 0;
    if (scene_)
        for (const auto &context : scene_->lighting_contexts)
            if (context.bloom_mask)
                texture_bytes += context.bloom_mask->rgba.size();
    upload_step();
}
void EnvironmentRenderer::preview_vertices(std::size_t draw,
                                           const std::vector<SceneVertex> &vertices) {
    require(scene_ && draw < draws_.size() &&
                vertices.size() == scene_->draws.at(draw).vertices.size(),
            "Geometry preview does not match the uploaded mesh");
    auto buffer = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), narrow(vertices.size() * sizeof(SceneVertex))), layout_);
    require(bgfx::isValid(buffer), "Geometry preview buffer allocation failed");
    bgfx::destroy(draws_[draw].vertices);
    draws_[draw].vertices = buffer;
    invalidate_selection_readback();
}
void EnvironmentRenderer::upload_step(std::size_t budget) {
    if (!scene_)
        return;
    std::size_t used = 0;
    if (!bgfx::isValid(lookup_texture_)) {
        lookup_height_ = unsigned(std::max(std::size_t(1), scene_->lighting_tables.size()));
        require(lookup_height_ <= bgfx::getCaps()->limits.maxTextureSize,
                "Too many lighting lookup tables for this GPU");
        std::vector<float> values(std::size_t(lookup_height_) * 256, 1.f);
        for (std::size_t i = 0; i < scene_->lighting_tables.size(); ++i)
            std::copy(scene_->lighting_tables[i].values.begin(),
                      scene_->lighting_tables[i].values.end(), values.begin() + i * 256);
        lookup_texture_ = bgfx::createTexture2D(
            256, std::uint16_t(lookup_height_), false, 1, bgfx::TextureFormat::R32F, 0,
            bgfx::copy(values.data(), narrow(values.size() * sizeof(float))));
        require(bgfx::isValid(lookup_texture_), "Lighting lookup allocation failed");
        texture_bytes += values.size() * sizeof(float);
    }

    for (auto &[name, t] : scene_->textures) {
        if (textures_.contains(name))
            continue;
        auto mips = texture_mip_chain(t);
        auto handle = bgfx::createTexture2D(t.width, t.height, true, 1, bgfx::TextureFormat::RGBA8,
                                            0, bgfx::copy(mips.data(), narrow(mips.size())));
        require(bgfx::isValid(handle), "Texture allocation failed for " + name);
        textures_.emplace(name, handle);
        used += mips.size();
        texture_bytes += mips.size();
        if (used >= budget)
            return;
    }
    while (draws_.size() < scene_->draws.size()) {
        auto &d = scene_->draws[draws_.size()];
        auto vb = bgfx::createVertexBuffer(
            bgfx::copy(d.vertices.data(), narrow(d.vertices.size() * sizeof(SceneVertex))),
            layout_);
        auto ib =
            bgfx::createIndexBuffer(bgfx::copy(d.indices.data(), narrow(d.indices.size() * 2)));
        if (!bgfx::isValid(vb) || !bgfx::isValid(ib)) {
            if (bgfx::isValid(vb))
                bgfx::destroy(vb);
            if (bgfx::isValid(ib))
                bgfx::destroy(ib);
            throw std::runtime_error("Scene buffer allocation failed");
        }
        draws_.push_back({vb, ib});
        auto n = d.vertices.size() * sizeof(SceneVertex) + d.indices.size() * 2;
        used += n;
        geometry_bytes += n;
        if (used >= budget)
            return;
    }
}
bgfx::TextureHandle EnvironmentRenderer::render(unsigned width, unsigned height, const float *view,
                                                const float *projection, bool vertex_colors,
                                                bool cutaway, float cut_height, bool raw_materials,
                                                bool picking) {
    const bool refresh_active = refresh.active(), draw_wireframe = wireframe && !refresh_active;
    auto &target = picking ? pick_target_ : target_;
    auto &target_width = picking ? pick_width_ : width_;
    auto &target_height = picking ? pick_height_ : height_;
    auto view_id = bgfx::ViewId(picking ? RenderViews::picking : RenderViews::scene);
    if (width != target_width || height != target_height) {
        if (!picking && bgfx::isValid(edge_target_)) {
            bgfx::destroy(edge_target_);
            edge_target_ = BGFX_INVALID_HANDLE;
        }
        if (!picking && bgfx::isValid(scene_copy_)) {
            bgfx::destroy(scene_copy_);
            scene_copy_ = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(target))
            bgfx::destroy(target);
        target_width = width;
        target_height = height;
        bgfx::TextureHandle attachments[2] = {
            bgfx::createTexture2D(std::uint16_t(width), std::uint16_t(height), false, 1,
                                  bgfx::TextureFormat::RGBA8,
                                  BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP),
            bgfx::createTexture2D(std::uint16_t(width), std::uint16_t(height), false, 1,
                                  bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY)};
        require(bgfx::isValid(attachments[0]) && bgfx::isValid(attachments[1]),
                "Could not allocate viewport target");
        target = bgfx::createFrameBuffer(2, attachments, true);
        require(bgfx::isValid(target), "Could not create viewport framebuffer");
    }
    bgfx::setViewName(view_id, picking ? "Surface selection" : "Environment");
    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(view_id, target);
    bgfx::setViewRect(view_id, 0, 0, std::uint16_t(width), std::uint16_t(height));
    bgfx::setViewClear(view_id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
                       picking ? 0u : background_color, 1.f, 0);
    bgfx::setViewTransform(view_id, view, projection);
    bgfx::touch(view_id);
    if (!picking) {
        auto id = RenderViews::selection_wire;
        bgfx::setViewName(id, "Selected mesh wireframe");
        bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
        bgfx::setViewFrameBuffer(id, target);
        bgfx::setViewRect(id, 0, 0, std::uint16_t(width), std::uint16_t(height));
        bgfx::setViewTransform(id, view, projection);
        bgfx::setViewClear(id, BGFX_CLEAR_NONE);
    }
    bool draw_outlines = outlines && !draw_wireframe && !picking && !refresh_active;
    if (draw_outlines) {
        if (!bgfx::isValid(edge_target_)) {
            bgfx::TextureHandle attachments[] = {
                bgfx::createTexture2D(std::uint16_t(width), std::uint16_t(height), false, 1,
                                      bgfx::TextureFormat::RGBA8,
                                      BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                                          BGFX_SAMPLER_V_CLAMP),
                bgfx::createTexture2D(std::uint16_t(width), std::uint16_t(height), false, 1,
                                      bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY)};
            require(bgfx::isValid(attachments[0]) && bgfx::isValid(attachments[1]),
                    "Could not allocate outline textures");
            edge_target_ = bgfx::createFrameBuffer(2, attachments, true);
            require(bgfx::isValid(edge_target_), "Could not allocate outline target");
        }
        auto id = RenderViews::edge_map;
        bgfx::setViewName(id, "Material edge map");
        bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
        bgfx::setViewFrameBuffer(id, edge_target_);
        bgfx::setViewRect(id, 0, 0, std::uint16_t(width), std::uint16_t(height));
        bgfx::setViewTransform(id, view, projection);
        bgfx::setViewClear(id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, 0x808080ff,
                           1.f, 0);
        bgfx::touch(id);
    }
    if (scene_)
        evaluate_material_animations(*scene_, materials_, playback.seconds, lighting.hour,
                                     playback.enabled && playback.materials);
    std::vector<LightSet> light_sets;
    if (scene_ && lighting.enabled && lighting.game &&
        lighting.context < scene_->lighting_contexts.size())
        light_sets = evaluate_lights(scene_->lighting_contexts[lighting.context], lighting.motion,
                                     lighting.hour);
    auto poses = scene_ ? evaluate_scene_poses(scene_->skeletons, player, playback.seconds,
                                               lighting.hour, playback.enabled && playback.skeletal)
                        : std::vector<std::vector<Matrix>>{};
    EnvironmentEffects effects;
    if (scene_ && lighting.context < scene_->lighting_contexts.size())
        effects = evaluate_environment_effects(scene_->lighting_contexts[lighting.context],
                                               lighting.motion, lighting.hour);
    bool particles_drawn = false;
    auto visible =
        scene_ ? evaluate_visibility(*scene_, playback.seconds, lighting.hour, visibility_enabled)
               : std::vector<bool>{};
    std::set<std::string> refraction_scopes;
    for (auto &material : materials_)
        if (material.screen_refraction)
            refraction_scopes.insert(material.resource_scope);
    bool refraction_started = false;
    if (scene_ && geometry_enabled)
        for (unsigned phase = 0; phase < 2; ++phase)
            for (std::size_t i = 0; i < draws_.size(); ++i) {
                if (!draw_visible(i) || !visible[i] ||
                    (scene_->draws[i].character &&
                     (!characters_enabled ||
                      (!conditional_characters && scene_->draws[i].conditional))))
                    continue;
                auto &d = draws_[i];
                auto &mat = materials_[scene_->draws[i].material];
                bool late = mat.screen_refraction ||
                            (mat.layer >= 4 && refraction_scopes.contains(mat.resource_scope));
                if (late != (phase == 1))
                    continue;
                bool missing = false;
                if (!refresh.bind(scene_->materials[scene_->draws[i].material]))
                    continue;
                if (late && !picking && !draw_wireframe && !refresh_active && !refraction_started) {
                    if (!bgfx::isValid(scene_copy_)) {
                        scene_copy_ = bgfx::createTexture2D(
                            std::uint16_t(width), std::uint16_t(height), false, 1,
                            bgfx::TextureFormat::RGBA8,
                            BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
                        require(bgfx::isValid(scene_copy_),
                                "Could not allocate scene refraction texture");
                    }
                    bgfx::TextureRegion source, destination;
                    source.init(bgfx::getTexture(target));
                    destination.init(scene_copy_);
                    bgfx::blit(RenderViews::scene_copy, destination, source);
                    view_id = RenderViews::refraction;
                    bgfx::setViewName(view_id, "Scene refraction");
                    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);
                    bgfx::setViewFrameBuffer(view_id, target);
                    bgfx::setViewRect(view_id, 0, 0, std::uint16_t(width), std::uint16_t(height));
                    bgfx::setViewClear(view_id, BGFX_CLEAR_NONE);
                    bgfx::setViewTransform(view_id, view, projection);
                    refraction_started = true;
                }
                auto &source_draw = scene_->draws[i];
                if (source_draw.player >= 0 &&
                    (!player.active || source_draw.player != int(player.appearance)))
                    continue;
                if (source_draw.sky_part >= 0 &&
                    (picking || !sky_enabled ||
                     lighting.context >= scene_->lighting_contexts.size() ||
                     !scene_->lighting_contexts[lighting.context].sky_enabled ||
                     (source_draw.sky_part == 0 && sky_type != 0) ||
                     (source_draw.sky_part == 1 && sky_type == 0)))
                    continue;
                if (source_draw.weather_mask && !picking && particles_enabled && !particles_drawn) {
                    particles_.render(view_id, scene_->weather_particles, weather_effect,
                                      playback.seconds, particle_origin, view);
                    particles_drawn = true;
                }
                if (source_draw.weather_mask &&
                    (picking || !particles_enabled ||
                     !(weather_effect < 32 && (source_draw.weather_mask & (1u << weather_effect)))))
                    continue;
                float skin[4] = {source_draw.skeleton >= 0 ? 1.f : 0.f,
                                 source_draw.weather_mask ? 1.f : 0.f, source_draw.weather_scale[0],
                                 source_draw.weather_scale[1]};
                if (source_draw.sky_part >= 0) {
                    skin[1] = -1;
                    skin[2] = scene_->lighting_contexts[lighting.context].sky_height;
                }
                bgfx::setUniform(skin_options_, skin);
                bgfx::setUniform(object_basis_, source_draw.object_basis.data(), 2);
                if (source_draw.skeleton >= 0) {
                    std::array<MaterialColor, 93> bones{}, normals{};
                    require(source_draw.palette.size() <= 31,
                            "Bone palette exceeds shader capacity");
                    for (unsigned j = 0; j < source_draw.palette.size(); ++j) {
                        require(std::size_t(source_draw.skeleton) < poses.size(),
                                "Mesh references a missing skeleton: " + source_draw.mesh);
                        require(
                            source_draw.palette[j] <
                                poses[std::size_t(source_draw.skeleton)].size(),
                            "Mesh " + source_draw.mesh + " references bone " +
                                std::to_string(source_draw.palette[j]) + " in a skeleton with " +
                                std::to_string(poses[std::size_t(source_draw.skeleton)].size()) +
                                " bones");
                        auto &matrix =
                            poses[std::size_t(source_draw.skeleton)][source_draw.palette[j]];
                        Matrix inverse{};
                        float determinant =
                            matrix[0] * (matrix[5] * matrix[10] - matrix[6] * matrix[9]) -
                            matrix[1] * (matrix[4] * matrix[10] - matrix[6] * matrix[8]) +
                            matrix[2] * (matrix[4] * matrix[9] - matrix[5] * matrix[8]);
                        if (std::abs(determinant) > 1e-12f)
                            inverse = pose_inverse(matrix);
                        for (unsigned r = 0; r < 3; ++r)
                            for (unsigned c = 0; c < 4; ++c) {
                                bones[j * 3 + r][c] = matrix[r * 4 + c];
                                normals[j * 3 + r][c] = c < 3 ? inverse[c * 4 + r] : 0;
                            }
                    }
                    bgfx::setUniform(bone_rows_, bones.data(), 93);
                    bgfx::setUniform(normal_rows_, normals.data(), 93);
                }
                FogSettings fog;
                if (fog_enabled && mat.fog_enabled && mat.fog_slot >= 0 && mat.fog_slot < 4)
                    fog = effects.fog[unsigned(mat.fog_slot)];
                float fp[4] = {fog.near_distance, fog.far_distance, fog.strength,
                               fog.enabled ? 1.f : 0.f};
                float fd[4] = {-view[2], -view[6], -view[10], -view[14]};
                bgfx::setUniform(fog_params_, fp);
                bgfx::setUniform(fog_color_, fog.color.data());
                bgfx::setUniform(fog_depth_, fd);
                float edge_options[4] = {0, 0, 0, -1};
                bgfx::setUniform(edge_options_, edge_options);
                bool selected =
                    !picking && !refresh_active && i < highlighted_.size() && highlighted_[i];
                float highlight[4] = {.55f, .55f, .55f, 0.f};
                bgfx::setUniform(selection_color_, highlight);
                auto id = unsigned(i + 1);
                float pick[4] = {float(id & 255) / 255, float((id >> 8) & 255) / 255,
                                 float((id >> 16) & 255) / 255,
                                 picking ? (mat.blend_state == 0x01010000 ? 1.f : 2.f) : 0.f};
                bgfx::setUniform(pick_color_, pick);
                std::array<MaterialColor, 3> rows_u{}, rows_v{}, texture_options{};
                std::array<MaterialColor, 6> projection_rows{};
                MaterialColor projection_options{}, projection_offsets{};
                for (unsigned slot = 0; slot < 2; ++slot) {
                    auto &input = mat.inputs[slot];
                    projection_options[slot] = input.source == 5 ? 1.f : 0.f;
                    for (unsigned row = 0; row < 3; ++row)
                        projection_rows[slot * 3 + row] = input.projection[row];
                    projection_offsets[slot * 2] = input.projection_offset[0];
                    projection_offsets[slot * 2 + 1] = input.projection_offset[1];
                }
                bgfx::setUniform(projection_rows_, projection_rows.data(), 6);
                bgfx::setUniform(projection_options_, projection_options.data());
                bgfx::setUniform(projection_offsets_, projection_offsets.data());
                for (unsigned unit = 0; unit < 3; ++unit) {
                    auto &input = mat.inputs[unit];
                    auto tex = textures_.find(mat.texture_inputs[unit]);
                    missing |= tex == textures_.end() && !mat.texture_inputs[unit].empty();
                    rows_u[unit] = input.row_u;
                    rows_u[unit][3] = float(input.source);
                    rows_v[unit] = input.row_v;
                    texture_options[unit][0] =
                        (input.min_filter != 0 && input.min_filter != 3) ? 1.f : 0.f;
                    if (auto source = scene_->textures.find(mat.texture_inputs[unit]);
                        source != scene_->textures.end()) {
                        auto &image = source->second;
                        texture_options[unit][1] =
                            image.authored_levels ? float(image.authored_levels - 1) : 32.f;
                        texture_options[unit][2] = float(image.width);
                        texture_options[unit][3] = float(image.height);
                    }
                    auto flags = wrap(input.wrap_u, true) | wrap(input.wrap_v, false);
                    if (input.mag_filter == 0)
                        flags |= BGFX_SAMPLER_MAG_POINT;
                    if (input.min_filter < 3)
                        flags |= BGFX_SAMPLER_MIN_POINT;
                    if (input.min_filter == 1 || input.min_filter == 4)
                        flags |= BGFX_SAMPLER_MIP_POINT;
                    if (input.source == 5) {
                        rows_u[unit] = {1, 0, 0, float(unit)};
                        rows_v[unit] = {0, 1, 0, 0};
                    }
                    bool refracted = unit == 0 && mat.screen_refraction;
                    if (refracted) {
                        rows_u[unit] = {1, 0, 0, 0};
                        rows_v[unit] = {0, 1, 0, 0};
                        texture_options[unit] = {0, 0, float(width), float(height)};
                        flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
                    }
                    auto texture = refracted ? (bgfx::isValid(scene_copy_) ? scene_copy_ : white_)
                                             : (tex == textures_.end() ? white_ : tex->second);
                    bgfx::setTexture(std::uint8_t(unit), samplers_[unit], texture, flags);
                }
                std::array<MaterialColor, 8> directions{}, light_colors{};
                MaterialColor ambient{};
                unsigned light_count = 0;
                bool has_game_lights = false;
                for (auto &set : light_sets)
                    if (int(set.index) == mat.light_set) {
                        has_game_lights = true;
                        for (auto &source : set.lights) {
                            if (source.type == 0) {
                                for (unsigned k = 0; k < 3; ++k)
                                    ambient[k] += source.color[k];
                            } else if (source.type == 1) {
                                require(light_count < 8,
                                        "More than eight directional lights in a light set");
                                for (unsigned k = 0; k < 3; ++k) {
                                    directions[light_count][k] = -source.direction[k];
                                    light_colors[light_count][k] = source.color[k];
                                }
                                ++light_count;
                            }
                        }
                    }
                if (!mat.fragment_lighting && !light_sets.empty()) {
                    has_game_lights = true;
                    light_count = 0;
                    ambient = {};
                }
                float game_light[4] = {has_game_lights ? 1.f : 0.f, float(light_count),
                                       float(lookup_height_), mat.fragment_lighting ? 1.f : 0.f};
                std::array<MaterialColor, 3> lookup_inputs{};
                for (unsigned k = 0; k < 3; ++k) {
                    auto &input = mat.reflection_inputs[k];
                    lookup_inputs[k] = {float(mat.reflection_tables[k]), input[0], input[1],
                                        input[2]};
                }
                bgfx::setUniform(game_light_, game_light);
                bgfx::setUniform(game_ambient_, ambient.data());
                bgfx::setUniform(game_directions_, directions.data(), 8);
                bgfx::setUniform(game_colors_, light_colors.data(), 8);
                bgfx::setUniform(lookup_inputs_, lookup_inputs.data(), 3);
                bgfx::setTexture(3, lookup_sampler_, lookup_texture_,
                                 BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
                bool bump_enabled = lighting.normal_maps && mat.bump.mode == 1 &&
                                    mat.bump.unsupported.empty() && mat.bump.texture < 3 &&
                                    textures_.contains(mat.texture_inputs[mat.bump.texture]) &&
                                    !mat.unsupported_mapping;
                float bump[4] = {bump_enabled ? 1.f : 0.f, float(mat.bump.texture),
                                 mat.bump.reconstruct_z ? 1.f : 0.f,
                                 bump_enabled && mat.object_space_normals ? 1.f : 0.f};
                bgfx::setUniform(bump_, bump);
                float light[4] = {lighting.ambient, lighting.strength, lighting.highlights,
                                  lighting.enabled ? 1.f : 0.f};
                float direction[4] = {lighting.direction[0], lighting.direction[1],
                                      lighting.direction[2],
                                      lighting.soft && !has_game_lights ? 1.f : 0.f};
                if (lighting.camera_relative && !has_game_lights)
                    for (unsigned k = 0; k < 3; ++k)
                        direction[k] = view[k * 4] * lighting.direction[0] +
                                       view[k * 4 + 1] * lighting.direction[1] +
                                       view[k * 4 + 2] * lighting.direction[2];
                std::array<MaterialColor, 5> surface = {mat.emission, mat.ambient, mat.diffuse,
                                                        mat.specular0, mat.specular1};
                float vertex_effect[4] = {
                    mat.screen_refraction ? (bgfx::getCaps()->originBottomLeft ? 2.f : 3.f)
                    : mat.height_tint     ? 1.f
                                          : 0.f,
                    mat.vertex_parameters[0], mat.vertex_parameters[1], mat.vertex_parameters[3]};
                if (source_draw.projected_shadow) {
                    vertex_effect[0] = 4;
                    for (unsigned k = 0; k < 3; ++k)
                        vertex_effect[k + 1] = source_draw.shadow_projection[k];
                }
                bgfx::setUniform(vertex_effect_, vertex_effect);
                float vertex_lighting[4] = {
                    mat.generated_lighting_color ? 1.f : 0.f, mat.lighting_channels[1] ? 1.f : 0.f,
                    mat.lighting_channels[2] ? 1.f : 0.f, mat.lighting_channels[3] ? 1.f : 0.f};
                bgfx::setUniform(vertex_lighting_, vertex_lighting);
                bgfx::setUniform(rim_phong_, mat.rim_phong.data());
                bgfx::setUniform(vertex_light_direction_,
                                 light_count ? directions[0].data() : direction);
                bgfx::setUniform(light_, light);
                bgfx::setUniform(light_direction_, direction);
                bgfx::setUniform(surface_, surface.data(), 5);
                bgfx::setUniform(texture_options_, texture_options.data(), 3);
                bool supported = mat.combiner.present && mat.combiner.unsupported.empty() &&
                                 !mat.unsupported_mapping && !missing;
                bool fallback = !raw_materials && !supported &&
                                (mat.texture_count > 1 || mat.unsupported_mapping || missing);
                bgfx::setUniform(uv_u_, rows_u.data(), 3);
                bgfx::setUniform(uv_v_, rows_v.data(), 3);
                std::array<std::array<MaterialColor, 6>, 7> values{};
                for (unsigned stage = 0; stage < 6; ++stage) {
                    auto &c = mat.combiner.stages[stage];
                    values[0][stage] = c.color_sources;
                    values[1][stage] = c.alpha_sources;
                    values[2][stage] = c.color_operands;
                    values[3][stage] = c.alpha_operands;
                    values[4][stage] = c.operation;
                    values[5][stage] = c.constant;
                    values[6][stage] = c.buffer_write;
                }
                for (unsigned field = 0; field < 7; ++field)
                    bgfx::setUniform(combiners_[field], values[field].data(), 6);
                bgfx::setUniform(buffer_, mat.combiner.buffer.data());
                float preview[4] = {supported ? 1.f : 0.f, vertex_colors ? 1.f : 0.f,
                                    float(mat.alpha_function), float(mat.alpha_reference) / 255.f};
                bgfx::setUniform(preview_, preview);
                float cut[4] = {cutaway ? 1.f : 0.f, cut_height, fallback ? 1.f : 0.f,
                                draw_wireframe && !picking ? 1.f : 0.f};
                bgfx::setUniform(cutaway_, cut);
                auto transform = source_draw.placement >= 0
                                     ? scene_->placement_transforms.at(source_draw.placement)
                                     : pose_identity();
                float model[16];
                for (unsigned r = 0; r < 4; ++r)
                    for (unsigned c = 0; c < 4; ++c)
                        model[c * 4 + r] = transform[r * 4 + c];
                bgfx::setTransform(model);
                bgfx::setVertexBuffer(0, d.vertices);
                if ((draw_wireframe && !picking) || selected) {
                    if (!bgfx::isValid(d.edges)) {
                        std::vector<std::uint32_t> pairs;
                        for (unsigned j = 0; j + 2 < source_draw.indices.size(); j += 3)
                            for (unsigned k = 0; k < 3; ++k) {
                                auto a = source_draw.indices[j + k],
                                     b = source_draw.indices[j + (k + 1) % 3];
                                if (a != b)
                                    pairs.push_back((std::uint32_t(std::min(a, b)) << 16) |
                                                    std::max(a, b));
                            }
                        std::sort(pairs.begin(), pairs.end());
                        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
                        if (pairs.empty())
                            continue;
                        std::vector<std::uint16_t> edges;
                        edges.reserve(pairs.size() * 2);
                        for (auto pair : pairs) {
                            edges.push_back(std::uint16_t(pair >> 16));
                            edges.push_back(std::uint16_t(pair));
                        }
                        d.edges = bgfx::createIndexBuffer(
                            bgfx::copy(edges.data(), narrow(edges.size() * 2)));
                        require(bgfx::isValid(d.edges), "Wireframe buffer allocation failed");
                        geometry_bytes += edges.size() * 2;
                    }
                }
                bgfx::setIndexBuffer(draw_wireframe && !picking ? d.edges : d.indices);
                auto rgba = mat.blend_color;
                rgba = ((rgba & 255) << 24) | ((rgba & 0xff00) << 8) | ((rgba >> 8) & 0xff00) |
                       (rgba >> 24);
                auto state = material_state(mat);
                if (fallback)
                    state = (state & (BGFX_STATE_CULL_MASK | BGFX_STATE_FRONT_CCW)) |
                            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                            BGFX_STATE_DEPTH_TEST_LEQUAL;
                if (picking)
                    state = (state & ~(BGFX_STATE_BLEND_MASK | BGFX_STATE_BLEND_EQUATION_MASK)) |
                            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
                if (source_draw.sky_part >= 0)
                    state &= ~(BGFX_STATE_DEPTH_TEST_MASK | BGFX_STATE_WRITE_Z);
                if (source_draw.weather_mask)
                    state &=
                        ~(BGFX_STATE_DEPTH_TEST_MASK | BGFX_STATE_WRITE_Z | BGFX_STATE_CULL_MASK);
                if (refresh_active)
                    state = (state & (BGFX_STATE_CULL_MASK | BGFX_STATE_FRONT_CCW)) |
                            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                            BGFX_STATE_DEPTH_TEST_LEQUAL;
                if (draw_wireframe && !picking)
                    state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                            BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_PT_LINES | BGFX_STATE_MSAA;
                if (source_draw.projected_shadow && !picking && !draw_wireframe) {
                    state &= ~BGFX_STATE_WRITE_Z;
                    if ((mat.depth_state & 1) && ((mat.depth_state >> 4) & 7) == 0)
                        state = (state & ~BGFX_STATE_DEPTH_TEST_MASK) | BGFX_STATE_DEPTH_TEST_NEVER;
                    else
                        state =
                            (state & ~BGFX_STATE_DEPTH_TEST_MASK) | BGFX_STATE_DEPTH_TEST_LEQUAL;
                }
                bgfx::setState(state, rgba);
                bgfx::setStencil(
                    source_draw.projected_shadow
                        ? (BGFX_STENCIL_TEST_NOTEQUAL | BGFX_STENCIL_FUNC_REF(128) |
                           BGFX_STENCIL_FUNC_RMASK(128) | BGFX_STENCIL_OP_FAIL_S_KEEP |
                           BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_REPLACE)
                    : refresh_active || (draw_wireframe && !picking) ? BGFX_STENCIL_NONE
                                                                     : material_stencil(mat));
                bool edge_draw =
                    draw_outlines && !source_draw.projected_shadow && source_draw.sky_part < 0 &&
                    !source_draw.weather_mask &&
                    (mat.edge_type != 2 || mat.id_edge_enabled || (mat.stencil_test & 1));
                bgfx::submit(view_id, program_, 0,
                             (selected || edge_draw) ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
                if (edge_draw) {
                    edge_options[0] = 1;
                    edge_options[1] = float(mat.edge_type);
                    edge_options[2] = float(std::min(mat.edge_id, 255u)) / 255.f;
                    edge_options[3] = float(mat.edge_alpha_mask);
                    bgfx::setUniform(edge_options_, edge_options);
                    auto edge_state =
                        state & ~(BGFX_STATE_BLEND_MASK | BGFX_STATE_BLEND_EQUATION_MASK |
                                  BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                    if (mat.edge_type != 2)
                        edge_state |= BGFX_STATE_WRITE_RGB;
                    if (mat.id_edge_enabled)
                        edge_state |= BGFX_STATE_WRITE_A;
                    if (mat.edge_type == 4 && !mat.id_edge_enabled)
                        edge_state |=
                            state & (BGFX_STATE_BLEND_MASK | BGFX_STATE_BLEND_EQUATION_MASK);
                    bgfx::setState(edge_state, rgba);
                    bgfx::submit(RenderViews::edge_map, program_, 0,
                                 selected ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
                }
                if (selected) {
                    edge_options[0] = 0;
                    bgfx::setUniform(edge_options_, edge_options);
                    cut[3] = 0;
                    highlight[3] = 1;
                    bgfx::setUniform(cutaway_, cut);
                    bgfx::setUniform(selection_color_, highlight);
                    bgfx::setIndexBuffer(d.edges);
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                   BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_PT_LINES |
                                   BGFX_STATE_MSAA);
                    bgfx::setStencil(BGFX_STENCIL_NONE);
                    bgfx::submit(RenderViews::selection_wire, program_);
                }
            }

    if (scene_)
        spatial.render(view_id, picking, unsigned(scene_->draws.size() + 1));
    if (!picking && scene_ && particles_enabled && !particles_drawn)
        particles_.render(view_id, scene_->weather_particles, weather_effect, playback.seconds,
                          particle_origin, view);
    if (draw_outlines)
        post_.outlines(target, bgfx::getTexture(edge_target_), width, height, outline_width);
    if (!picking && bloom_enabled) {
        auto mask = white_;
        std::array<float, 2> scale{1, 1};
        if (lighting.context < bloom_masks_.size() &&
            bgfx::isValid(bloom_masks_[lighting.context])) {
            mask = bloom_masks_[lighting.context];
            const auto &image = *scene_->lighting_contexts[lighting.context].bloom_mask;
            scale = {std::min(1.f, 400.f / image.width), std::min(1.f, 240.f / image.height)};
        }
        return post_.render(bgfx::getTexture(target), width, height, effects.bloom, mask, scale);
    }
    return bgfx::getTexture(target);
}
bool EnvironmentRenderer::request_pick(unsigned x, unsigned y, unsigned width, unsigned height,
                                       const float *view, const float *projection, bool colors,
                                       bool cutaway, float cut_height, bool raw_materials) {
    if (pick_pending_ || !ready() || x >= width || y >= height)
        return false;
    if (!bgfx::isValid(pick_readback_)) {
        pick_readback_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                               BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        require(bgfx::isValid(pick_readback_), "Surface selection readback is unavailable");
    }
    pick_refresh_ = refresh.active();
    auto texture =
        render(width, height, view, projection, colors, cutaway, cut_height, raw_materials, true);
    if (bgfx::getCaps()->originBottomLeft)
        y = height - 1 - y;
    bgfx::TextureRegion src, dst;
    src.init(texture, std::uint16_t(x), std::uint16_t(y), 1, 1);
    dst.init(pick_readback_);
    bgfx::blit(RenderViews::pick_readback, dst, src);
    pick_ready_ = bgfx::read(dst, pick_pixel_.data());
    pick_generation_ = scene_generation_;
    pick_pending_ = true;
    return true;
}
std::optional<int> EnvironmentRenderer::poll_pick(std::uint32_t frame) {
    if (!pick_pending_ || frame < pick_ready_)
        return {};
    pick_pending_ = false;
    picked_refresh_region_ = -1;
    if (pick_generation_ != scene_generation_)
        return {};
    if (pick_refresh_)
        picked_refresh_region_ = int(pick_pixel_[3]);
    auto id = unsigned(pick_pixel_[0]) | (unsigned(pick_pixel_[1]) << 8) |
              (unsigned(pick_pixel_[2]) << 16);
    return id && scene_ && id <= scene_->draws.size() + scene_->spatial.regions.size() ? int(id - 1)
                                                                                       : -1;
}

}
