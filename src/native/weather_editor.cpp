#include "native/tutorial_widgets.h"
#include "native/weather_editor.h"
#include "field/area.h"
#include <imgui.h>
#include <algorithm>
namespace studio {
namespace {
std::string weather_label(const LightingContext &context, unsigned kind) {
    static constexpr const char *names[]{"Clear", "Rain",     "Fog",          "Thunderstorm",
                                         "Snow",  "Blizzard", "Diamond dust", "Sandstorm"};
    for (auto &weather : context.weather)
        if (weather.kind == kind)
            return std::string(weather.effect < std::size(names) ? names[weather.effect]
                                                                 : "Weather") +
                   " / profile " + std::to_string(kind);
    return "Unavailable / profile " + std::to_string(kind);
}
void select_weather(const WeatherProfile &weather, EnvironmentRenderer &renderer, int &preview) {
    preview = int(weather.kind);
    renderer.lighting.motion = weather.motion;
    renderer.weather_effect = weather.effect;
    renderer.sky_type = weather.sky;
}
}
void WeatherEditor::load(const std::filesystem::path &dump) {
    if (dump_ == dump && document_)
        return;
    document_.reset();
    dump_ = dump;
    error_.clear();
    try {
        document_ = std::make_unique<WeatherDocument>(
            Archive(dump / TargetProfile::zone_archive).decoded(0));
        project_.bind(
            "weather", "weather/zones", "Zone weather schedules", "",
            [this] {
                return document_ && document_->dirty();
            },
            [this] {
                return project_text(document_->serialize());
            },
            [this] {
                document_->mark_saved();
            });
        if (auto file = project_.document(); !file.empty()) {
            document_->restore(text(read_file(file)));
            project_.restored();
        }
    } catch (const std::exception &e) {
        document_.reset();
        error_ = e.what();
    }
}
void WeatherEditor::update(Environment &scene, EnvironmentRenderer &renderer, int &preview) {
    if (!follow_time_ || renderer.lighting.context >= scene.lighting_contexts.size())
        return;
    auto &context = scene.lighting_contexts[renderer.lighting.context];
    if (document_)
        context.schedule = document_->schedule(context.zone);
    if (auto *weather = weather_at_time(context, renderer.lighting.hour))
        select_weather(*weather, renderer, preview);
    else {
        preview = -1;
        renderer.lighting.motion = 0;
        renderer.weather_effect = 0;
        renderer.sky_type = 0;
    }
}
void WeatherEditor::draw(LightingContext &context, EnvironmentRenderer &renderer, int &preview) {
    studio::TutorialWidgets::Checkbox("weather_editor", "Weather follows time", &follow_time_);
    if (document_)
        context.schedule = document_->schedule(context.zone);
    auto period = weather_period(renderer.lighting.hour);
    auto kind = context.schedule[period];
    if (follow_time_) {
        ImGui::TextWrapped("%s: %s", weather_period_name(period),
                           weather_label(context, kind).c_str());
        if (auto *weather = weather_at_time(context, renderer.lighting.hour))
            select_weather(*weather, renderer, preview);
    } else {
        auto label = preview < 0 ? std::string("Custom environment variant")
                                 : weather_label(context, unsigned(preview));
        bool weather_combo_open = ImGui::BeginCombo("Preview weather", label.c_str());
        if (!weather_combo_open)
            TutorialWidgets::item("weather_editor", "Preview weather");
        if (weather_combo_open) {
            for (auto &weather : context.weather)
                if (ImGui::Selectable(weather_label(context, weather.kind).c_str(),
                                      preview == int(weather.kind)))
                    select_weather(weather, renderer, preview);
            ImGui::EndCombo();
        }
        int motion = int(renderer.lighting.motion);
        if (context.motions.size() > 1 &&
            ImGui::SliderInt("Environment variant", &motion, 0, int(context.motions.size()) - 1)) {
            preview = -1;
            renderer.lighting.motion = unsigned(motion);
            renderer.weather_effect = 0;
            renderer.sky_type = 0;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Preview uses in-game time. Saved-game and scripted weather overrides "
                          "are not simulated.");
    if (studio::TutorialWidgets::TreeNode("weather_editor", "Edit weather schedule")) {
        ImGui::Text("Zone %u", context.zone);
        if (document_) {
            auto values = document_->schedule(context.zone);
            ImGui::BeginDisabled(!project_store());
            for (unsigned slot = 0; slot < values.size(); ++slot) {
                ImGui::PushID(int(slot));
                ImGui::Text("%s%s", weather_period_name(slot), slot == period ? " (now)" : "");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##weather", weather_label(context, values[slot]).c_str())) {
                    for (auto &weather : context.weather)
                        if (ImGui::Selectable(weather_label(context, weather.kind).c_str(),
                                              weather.kind == values[slot]))
                            document_->set(context.zone, slot, weather.kind);
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
            ImGui::BeginDisabled(!document_->can_undo());
            if (studio::TutorialWidgets::Button("weather_editor", "Undo##weather"))
                document_->undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!document_->can_redo());
            if (studio::TutorialWidgets::Button("weather_editor", "Redo##weather"))
                document_->redo();
            ImGui::EndDisabled();
            if (studio::TutorialWidgets::Button("weather_editor", "Reset zone schedule"))
                document_->reset(context.zone);
            if (studio::TutorialWidgets::Button("weather_editor", "Save Project##weather")) {
                try {
                    save_editor_project();
                    error_.clear();
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            if (!project_store())
                ImGui::TextWrapped("Open an editor project to save weather schedules.");
            else
                ImGui::TextUnformatted(document_->dirty() ? "Unsaved weather changes"
                                                          : "No unsaved weather changes");
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::TreePop();
    }
}
}
