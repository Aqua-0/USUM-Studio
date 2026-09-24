#include "native/ambient_sound_preview.h"
#include <imgui.h>
#include "audio/script_sound.h"
#include <algorithm>
#include <chrono>
namespace studio {
AmbientSoundPreview::~AmbientSoundPreview() {
    stop();
    if (initialized_)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
void AmbientSoundPreview::stop() {
    playing_ = false;
    cursor_ = 0;
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}
void AmbientSoundPreview::reset() {
    stop();
    discard_ = true;
    clips_.clear();
    samples_ = {};
    error_.clear();
    sound_ = 0;
}
void AmbientSoundPreview::play() {
    stop();
    try {
        samples_ = decode_sound_wave(clips_.at(selected_).wave);
        require(!samples_.pcm.empty(), "Sound sample is empty");
        if (!initialized_) {
            require(SDL_InitSubSystem(SDL_INIT_AUDIO), SDL_GetError());
            initialized_ = true;
        }
        SDL_AudioSpec spec{SDL_AUDIO_S16, int(samples_.channels), int(samples_.rate)};
        stream_ =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        require(stream_ != nullptr, SDL_GetError());
        require(SDL_SetAudioStreamGain(stream_, volume_), SDL_GetError());
        require(SDL_ResumeAudioStreamDevice(stream_), SDL_GetError());
        playing_ = true;
        error_.clear();
    } catch (const std::exception &e) {
        error_ = e.what();
        stop();
    }
}
void AmbientSoundPreview::update(bool active) {
    if (!active) {
        stop();
        discard_ = true;
    }
    if (loading_.valid() &&
        loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto clips = loading_.get();
            if (!discard_) {
                clips_ = std::move(clips);
                selected_ = 0;
            }
        } catch (const std::exception &e) {
            if (!discard_)
                error_ = e.what();
        }
    }
    if (!playing_ || !stream_)
        return;
    auto queued = SDL_GetAudioStreamQueued(stream_);
    if (queued < 0) {
        error_ = SDL_GetError();
        stop();
        return;
    }
    auto target = std::size_t(samples_.rate) * samples_.channels * 2 / 4;
    while (std::size_t(queued) < target) {
        if (cursor_ == samples_.frames()) {
            if (loop_)
                cursor_ = samples_.looping ? samples_.loop_start : 0;
            else {
                SDL_FlushAudioStream(stream_);
                if (!queued)
                    stop();
                break;
            }
        }
        auto frames = std::min<std::size_t>(4096, samples_.frames() - cursor_);
        if (!frames) {
            stop();
            break;
        }
        auto bytes = int(frames * samples_.channels * 2);
        if (!SDL_PutAudioStreamData(stream_, samples_.pcm.data() + cursor_ * samples_.channels,
                                    bytes)) {
            error_ = SDL_GetError();
            stop();
            return;
        }
        cursor_ += frames;
        queued += bytes;
    }
}
void AmbientSoundPreview::draw(const std::filesystem::path &dump, unsigned sound,
                               bool script_reference,
                               std::shared_ptr<const AudioDocument> library) {
    ImGui::PushID(int(sound));
    bool selected = sound_ == sound && dump_ == dump;
    if (!selected)
        reset();
    ImGui::BeginDisabled(loading_.valid());
    if ((!selected || clips_.empty()) && ImGui::Button("Preview samples")) {
        reset();
        dump_ = dump;
        sound_ = sound;
        discard_ = false;
        loading_ = std::async(std::launch::async, [dump, sound, script_reference, library] {
            auto audio = library ? library : std::make_shared<const AudioDocument>(dump);
            return script_reference ? script_sound_samples(*audio, sound)
                                    : ambient_sound_samples(*audio, sound);
        });
        selected = true;
    }
    ImGui::EndDisabled();
    if (selected) {
        if (loading_.valid())
            ImGui::TextDisabled("Loading sound samples...");
        if (!clips_.empty()) {
            ImGui::TextWrapped("Bank sample preview. Sequence timing, layering and spatial "
                               "attenuation are not simulated.");
            if (clips_.size() > 1 && ImGui::BeginCombo("Sample", clips_[selected_].label.c_str())) {
                for (std::size_t i = 0; i < clips_.size(); ++i)
                    if (ImGui::Selectable(clips_[i].label.c_str(), i == selected_)) {
                        stop();
                        selected_ = i;
                    }
                ImGui::EndCombo();
            }
            if (ImGui::Button(playing_ ? "Stop" : "Play")) {
                if (playing_)
                    stop();
                else
                    play();
            }
            ImGui::SameLine();
            ImGui::Checkbox("Repeat sample", &loop_);
            if (ImGui::SliderFloat("Preview volume", &volume_, 0, 1, "%.2f") && stream_)
                SDL_SetAudioStreamGain(stream_, volume_);
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
    }
    ImGui::PopID();
}
}
