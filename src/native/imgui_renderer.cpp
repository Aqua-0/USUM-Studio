#include "native/imgui_renderer.h"
#include "core/binary.h"
#include <bgfx/embedded_shader.h>
#include <bx/math.h>
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <imgui/vs_ocornut_imgui.bin.h>
#include <imgui/fs_ocornut_imgui.bin.h>
namespace studio {
namespace {
constexpr std::uint64_t opaque_image_bit = 0x10000u, nearest_image_bit = 0x20000u,
                        texture_handle_mask = 0xffffu;
const bgfx::EmbeddedShader shaders[] = {BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
                                        BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
                                        BGFX_EMBEDDED_SHADER_END()};
}
ImGuiRenderer::ImGuiRenderer() {
    auto type = bgfx::getRendererType();
    program_ =
        bgfx::createProgram(bgfx::createEmbeddedShader(shaders, type, "vs_ocornut_imgui"),
                            bgfx::createEmbeddedShader(shaders, type, "fs_ocornut_imgui"), true);
    require(bgfx::isValid(program_), "Cannot create UI shaders");
    sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    upload_font();
    auto &io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendRendererName = "USUMStudio bgfx";
}
bool ImGuiRenderer::preview_3ds = false;
bool ImGuiRenderer::preview_native_size = false;
ViewportResolution ImGuiRenderer::viewport_resolution(float width, float height) {
    auto scale = ImGui::GetIO().DisplayFramebufferScale;
    auto resolution =
        ViewportResolution::fit(width, height, scale.x, scale.y, preview_3ds, preview_native_size);
    if (preview_3ds) {
        auto origin = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            origin, {origin.x + std::max(width, 1.f), origin.y + std::max(height, 1.f)},
            IM_COL32(0, 0, 0, 255));
        ImGui::SetCursorScreenPos({origin.x + resolution.offset_x, origin.y + resolution.offset_y});
    }
    return resolution;
}
std::uint64_t ImGuiRenderer::image_id(bgfx::TextureHandle texture, bool alpha_blend, bool nearest) {
    return std::uint64_t(texture.idx + 1) | (alpha_blend ? 0u : opaque_image_bit) |
           (nearest ? nearest_image_bit : 0u);
}
void ImGuiRenderer::upload_font() {
    if (bgfx::isValid(font_))
        bgfx::destroy(font_);
    auto &io = ImGui::GetIO();
    unsigned char *pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    font_ = bgfx::createTexture2D(
        std::uint16_t(w), std::uint16_t(h), false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(pixels, std::uint32_t(w * h * 4)));
    require(bgfx::isValid(font_), "Cannot upload UI font");
    io.Fonts->SetTexID(ImTextureID(image_id(font_)));
}
ImGuiRenderer::~ImGuiRenderer() {
    ImGui::GetIO().Fonts->SetTexID(0);
    bgfx::destroy(font_);
    bgfx::destroy(program_);
    bgfx::destroy(sampler_);
}
void ImGuiRenderer::render(ImDrawData *data) {
    auto width = int(data->DisplaySize.x * data->FramebufferScale.x),
         height = int(data->DisplaySize.y * data->FramebufferScale.y);
    if (width <= 0 || height <= 0)
        return;
    constexpr bgfx::ViewId view = 255;
    float projection[16];
    bx::mtxOrtho(projection, data->DisplayPos.x, data->DisplayPos.x + data->DisplaySize.x,
                 data->DisplayPos.y + data->DisplaySize.y, data->DisplayPos.y, 0, 100, 0,
                 bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewName(view, "Editor UI");
    bgfx::setViewRect(view, 0, 0, std::uint16_t(width), std::uint16_t(height));
    bgfx::setViewTransform(view, nullptr, projection);
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0x111820ff);
    bgfx::touch(view);
    for (auto list : data->CmdLists) {
        bgfx::TransientVertexBuffer vb;
        bgfx::TransientIndexBuffer ib;
        auto nv = std::uint32_t(list->VtxBuffer.Size), ni = std::uint32_t(list->IdxBuffer.Size);
        if (bgfx::getAvailTransientVertexBuffer(nv, layout_) != nv ||
            bgfx::getAvailTransientIndexBuffer(ni) != ni)
            throw std::runtime_error("UI transient buffer capacity exceeded");
        bgfx::allocTransientVertexBuffer(&vb, nv, layout_);
        bgfx::allocTransientIndexBuffer(&ib, ni);
        std::memcpy(vb.data, list->VtxBuffer.Data, nv * sizeof(ImDrawVert));
        std::memcpy(ib.data, list->IdxBuffer.Data, ni * sizeof(ImDrawIdx));
        for (auto &cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback != ImDrawCallback_ResetRenderState)
                    cmd.UserCallback(list, &cmd);
                continue;
            }
            float x0 = (cmd.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x,
                  y0 = (cmd.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y,
                  x1 = (cmd.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x,
                  y1 = (cmd.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y;
            x0 = std::clamp(x0, 0.f, float(width));
            y0 = std::clamp(y0, 0.f, float(height));
            x1 = std::clamp(x1, 0.f, float(width));
            y1 = std::clamp(y1, 0.f, float(height));
            if (x1 <= x0 || y1 <= y0)
                continue;
            bgfx::setScissor(std::uint16_t(x0), std::uint16_t(y0), std::uint16_t(x1 - x0),
                             std::uint16_t(y1 - y0));
            auto id = cmd.GetTexID();
            auto state = BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA;
            if (!(id & opaque_image_bit))
                state |= BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                                                    BGFX_STATE_BLEND_INV_SRC_ALPHA);
            bgfx::setState(state);
            bgfx::setTexture(
                0, sampler_, bgfx::TextureHandle{std::uint16_t((id & texture_handle_mask) - 1)},
                id & nearest_image_bit
                    ? BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT |
                          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
                    : UINT32_MAX);
            bgfx::setVertexBuffer(0, &vb, cmd.VtxOffset, nv - cmd.VtxOffset);
            bgfx::setIndexBuffer(&ib, cmd.IdxOffset, cmd.ElemCount);
            bgfx::submit(view, program_);
        }
    }
}
}
