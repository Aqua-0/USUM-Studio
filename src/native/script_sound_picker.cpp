#include "native/script_sound_picker.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
namespace studio {
void ScriptSoundPicker::reset() {
    preview_.reset();
    audio_.reset();
    choices_.clear();
    error_.clear();
    source_.clear();
    drawn_ = false;
}
void ScriptSoundPicker::update(bool active) {
    preview_.update(active && drawn_);
    drawn_ = false;
    if (loading_.valid() &&
        loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto audio = loading_.get();
            if (loading_source_ == source_) {
                audio_ = std::move(audio);
                choices_ = script_sound_choices(*audio_);
            }
        } catch (const std::exception &e) {
            if (loading_source_ == source_)
                error_ = e.what();
        }
    }
}
bool ScriptSoundPicker::draw(const std::filesystem::path &source, unsigned &reference) {
    if (source != source_) {
        reset();
        source_ = source;
    }
    drawn_ = true;
    bool changed = false;
    if (ImGui::Button("Browse sounds...")) {
        candidate_ = reference;
        search_[0] = 0;
        ImGui::OpenPopup("Choose conversation sound");
    }
    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopup("Choose conversation sound")) {
        if (!audio_ && !loading_.valid() && error_.empty()) {
            loading_source_ = source_;
            loading_ = std::async(std::launch::async, [source] {
                return std::make_shared<const AudioDocument>(source);
            });
        }
        ImGui::TextWrapped("Choose a script sound ID. Preview plays bank samples; the game's "
                           "sequence may change pitch, timing and layering.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Sound search",
                                 "Search ID, group, archive index or dialogue chime", search_,
                                 sizeof(search_));
        std::string query(search_);
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        if (loading_.valid())
            ImGui::TextDisabled("Loading audio library...");
        if (!error_.empty()) {
            ImGui::TextWrapped("%s", error_.c_str());
            if (ImGui::Button("Retry"))
                error_.clear();
        }
        if (ImGui::BeginChild("Sound choices", ImVec2(0, 260), ImGuiChildFlags_Borders)) {
            for (const auto &choice : choices_) {
                auto label = choice.label;
                std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
                    return char(std::tolower(c));
                });
                if (!query.empty() && label.find(query) == std::string::npos)
                    continue;
                if (ImGui::Selectable(choice.label.c_str(), candidate_ == choice.reference)) {
                    candidate_ = choice.reference;
                    preview_.reset();
                }
            }
        }
        ImGui::EndChild();
        auto found = std::find_if(choices_.begin(), choices_.end(), [&](const auto &c) {
            return c.reference == candidate_;
        });
        if (found != choices_.end()) {
            ImGui::Text("Script ID %u / archive sound %u / %u bank samples", found->reference,
                        found->archive_sound, found->samples);
            preview_.draw(source_, candidate_, true, audio_);
            if (ImGui::Button("Use sound")) {
                reference = candidate_;
                changed = true;
                preview_.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel")) {
            preview_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        try {
            auto index = script_sound_index(reference);
            ImGui::TextDisabled("Archive sound %u%s", index,
                                reference == 327694 ? " / dialogue chime" : "");
            preview_.draw(source_, reference, true, audio_);
        } catch (const std::exception &e) {
            preview_.reset();
            ImGui::TextWrapped("%s. You can keep the raw ID, but it cannot be previewed here.",
                               e.what());
        }
    }
    return changed;
}
}
