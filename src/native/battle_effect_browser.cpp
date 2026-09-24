#include "native/tutorial_widgets.h"
#include "native/model_workspace.h"
#include "native/imgui_renderer.h"
#include "formats/texture_codec.h"
#include "scene/material.h"
#include <algorithm>
#include <cctype>
#include <sstream>
namespace studio {
namespace {
std::string label(const EffectResource &resource) {
    auto result = std::to_string(resource.member) + ":" + std::to_string(resource.subfile);
    for (auto child : resource.path)
        result += "/" + std::to_string(child);
    return result + "  " + resource.name;
}
std::string lowercase(std::string value) {
    for (auto &c : value)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
}
void ModelWorkspace::refresh_effects() {
    if (effects_job_.valid() || job_.valid() || catalog_job_.valid() || library_job_.valid())
        return;
    effects_.clear();
    effect_selected_ = -1;
    effect_info_.clear();
    effect_environment_.reset();
    effect_camera_.reset();
    effect_camera_preview_ = false;
    if (bgfx::isValid(effect_texture_))
        bgfx::destroy(effect_texture_);
    effect_texture_ = BGFX_INVALID_HANDLE;
    if (dump_.empty()) {
        status_ = "Open a project to browse battle effects";
        return;
    }
    cancel_ = false;
    error_.clear();
    status_ = "Reading battle effects...";
    auto dump = dump_;
    effects_job_ = std::async(std::launch::async, [this, dump] {
        return load_effect_catalog(dump, &cancel_);
    });
}
void ModelWorkspace::preview_effect(const EffectResource &resource) {
    error_.clear();
    effect_info_.clear();
    if (bgfx::isValid(effect_texture_))
        bgfx::destroy(effect_texture_);
    effect_texture_ = BGFX_INVALID_HANDLE;
    try {
        require(resource.error.empty(), resource.error);
        if (resource.kind == EffectResourceKind::Pack ||
            resource.kind == EffectResourceKind::Model ||
            resource.kind == EffectResourceKind::Particle) {
            auto dump = dump_;
            cancel_ = false;
            effect_camera_preview_ = false;
            job_ = std::async(std::launch::async, [this, dump, resource] {
                if (resource.kind == EffectResourceKind::Particle)
                    return load_effect_particles(dump, resource);
                return load_effect_model(dump, resource.member, resource.subfile,
                                         resource.kind == EffectResourceKind::Model
                                             ? resource.path
                                             : std::vector<std::size_t>{},
                                         {}, &cancel_);
            });
            status_ = "Opening " + resource.name;
            return;
        }
        auto data = read_effect_resource(dump_, resource);
        std::ostringstream info;
        if (resource.kind == EffectResourceKind::Texture) {
            auto image = decode_field_texture(data);
            effect_width_ = image.width;
            effect_height_ = image.height;
            effect_texture_ = bgfx::createTexture2D(
                image.width, image.height, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                bgfx::copy(image.rgba.data(), narrow(image.rgba.size())));
            info << image.width << " x " << image.height << " | " << image.authored_levels
                 << " stored levels";
        } else if (resource.kind == EffectResourceKind::Motion) {
            auto skeletal = decode_skeletal_motion(data);
            auto material = decode_material_motion(data);
            auto visibility = decode_visibility_motion(data);
            info << std::max({skeletal.frames, material.frames, visibility.clock.frames})
                 << " frames\n"
                 << skeletal.tracks.size() << " bone tracks | " << material.tracks.size()
                 << " material tracks | " << visibility.tracks.size() << " visibility tracks";
            info << "\nSections:";
            auto count = u32(data, 4);
            require(count <= 64, "Invalid motion section count");
            for (unsigned i = 0; i < count; ++i)
                info << ' ' << u32(data, 8 + i * 12);
        } else if (resource.kind == EffectResourceKind::Shader) {
            info << decode_material_shader(data).name
                 << "\nShader settings are used when opening the containing model pack.";
        } else if (resource.kind == EffectResourceKind::Environment) {
            info << "Textures: " << u16(data, 20) << " | Light sets: " << u16(data, 22)
                 << " | Cameras: " << u16(data, 24);
        } else
            info << "No preview decoder for this resource.";
        effect_info_ = info.str();
    } catch (const std::exception &e) {
        error_ = e.what();
    }
}
void ModelWorkspace::browse_effects(bool busy) {
    ImGui::BeginDisabled(busy || dump_.empty());
    if (TutorialWidgets::Button("battle_effect_browser", "Refresh effects"))
        refresh_effects();
    ImGui::EndDisabled();
    if (busy)
        ImGui::TextWrapped("%s", status_.c_str());
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::SetNextItemWidth(-1);
    bool searched = ImGui::InputTextWithHint("##effect-search", "Search name or entry:subfile",
                                             search_, sizeof(search_));
    TutorialWidgets::item("battle_effect_browser", "Search effects", searched);
    static const char *types[] = {
        "All resources", "Model packs", "Models",  "Textures",
        "Motions",       "Particles",   "Shaders", "Cameras / environments",
        "Unrecognized"};
    ImGui::SetNextItemWidth(-1);
    bool filtered = ImGui::Combo("##effect-type", &effect_kind_, types, 9);
    TutorialWidgets::item("battle_effect_browser", "Resource type", filtered);
    auto query = lowercase(search_);
    std::vector<std::size_t> visible;
    for (std::size_t i = 0; i < effects_.size(); ++i)
        if ((!effect_kind_ || int(effects_[i].kind) == effect_kind_ - 1) &&
            (query.empty() || lowercase(label(effects_[i])).find(query) != std::string::npos))
            visible.push_back(i);
    ImGui::TextDisabled("%zu resources", visible.size());
    ImGui::BeginDisabled(busy);
    if (ImGui::BeginChild("Effect resources",
                          {0, std::max(120.f, ImGui::GetContentRegionAvail().y * .48f)},
                          ImGuiChildFlags_Borders)) {
        ImGuiListClipper clip;
        clip.Begin(int(visible.size()));
        while (clip.Step())
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                auto index = visible[std::size_t(row)];
                ImGui::PushID(int(index));
                if (ImGui::Selectable(label(effects_[index]).c_str(),
                                      effect_selected_ == int(index)) &&
                    !busy) {
                    effect_selected_ = int(index);
                    preview_effect(effects_[index]);
                    busy = job_.valid();
                }
                ImGui::PopID();
            }
    }
    ImGui::EndChild();
    ImGui::EndDisabled();
    if (effect_selected_ < 0 || std::size_t(effect_selected_) >= effects_.size())
        return;
    const auto &resource = effects_[std::size_t(effect_selected_)];
    ImGui::TextWrapped("%s", resource.name.c_str());
    ImGui::TextDisabled("%s | %zu bytes", effect_resource_kind(resource.kind), resource.bytes);
    if (!effect_info_.empty())
        ImGui::TextWrapped("%s", effect_info_.c_str());
    if (bgfx::isValid(effect_texture_)) {
        float scale =
            std::min(1.f, std::min(ImGui::GetContentRegionAvail().x / float(effect_width_),
                                   240.f / float(effect_height_)));
        ImGui::Image(ImTextureID(ImGuiRenderer::image_id(effect_texture_)),
                     {float(effect_width_) * scale, float(effect_height_) * scale});
    }
    ImGui::BeginDisabled(busy);
    if (resource.kind == EffectResourceKind::Motion) {
        bool attach =
            document_ && document_->battle_effect &&
            std::none_of(document_->sources.begin(), document_->sources.end(), [&](const auto &s) {
                return s.member == resource.member && s.subfile != resource.subfile;
            });
        if (attach)
            attach = std::none_of(
                document_->resources.begin(), document_->resources.end(), [&](const auto &link) {
                    const auto &source = document_->sources.at(link.source);
                    return source.member == resource.member && source.subfile == resource.subfile &&
                           link.path == resource.path;
                });
        ImGui::BeginDisabled(!attach);
        if (TutorialWidgets::Button("battle_effect_browser", "Attach motion to preview model")) {
            auto model = *document_;
            auto dump = dump_;
            model.effect_motions.push_back({resource.member, resource.subfile, resource.path});
            const auto &link = model.resources.at(model.material_resources.front());
            auto path = link.path;
            job_ = std::async(std::launch::async, [model, dump, path] {
                const auto &source = model.sources.front();
                auto result = load_effect_model(dump, source.member, source.subfile, path,
                                                model.effect_motions);
                result.select_motion(int(result.motions.size()) - 1);
                return result;
            });
        }
        ImGui::EndDisabled();
        if (!attach)
            ImGui::TextWrapped(
                "Open an effect model first. Already loaded motions are available in the viewport. "
                "Each model session can use one subfile per archive entry.");
        if (effect_environment_ && TutorialWidgets::Button("battle_effect_browser",
                                                           "Pair with selected camera environment"))
            try {
                effect_camera_ =
                    decode_battle_camera(read_effect_resource(dump_, *effect_environment_),
                                         read_effect_resource(dump_, resource));
                effect_camera_frame_ = 0;
                effect_camera_preview_ = true;
                error_.clear();
            } catch (const std::exception &e) {
                error_ = e.what();
            }
    }
    if (resource.kind == EffectResourceKind::Environment &&
        TutorialWidgets::Button("battle_effect_browser", "Use as camera environment")) {
        effect_environment_ = resource;
        effect_camera_.reset();
        effect_camera_preview_ = false;
    }
    ImGui::EndDisabled();
    if (effect_environment_)
        ImGui::TextWrapped("Camera environment: %s. Select a matching camera motion to preview it.",
                           label(*effect_environment_).c_str());
    if (effect_camera_) {
        TutorialWidgets::Checkbox("battle_effect_browser", "Preview camera in viewport",
                                  &effect_camera_preview_);
        ImGui::SliderFloat("Camera frame", &effect_camera_frame_, 0, effect_camera_->frames,
                           "%.0f");
        if (!document_)
            ImGui::TextWrapped("Open an effect model to view the camera against geometry.");
    }
    ImGui::TextWrapped("Preview individual resources here; open a model and Send to Studio to edit "
                       "it. Effect sequences are not played.");
}
}
