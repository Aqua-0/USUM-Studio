#include "native/tutorial_widgets.h"
#include "native/map_music.h"
#include "field/area.h"
#include "formats/archive.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
namespace studio {
MapMusic::~MapMusic() {
    if (audio_)
        SDL_DestroyAudioStream(audio_);
}
void MapMusic::stop() {
    playing_ = false;
    cursor_ = 0;
    discard_ = true;
    if (audio_) {
        SDL_PauseAudioStreamDevice(audio_);
        SDL_ClearAudioStream(audio_);
    }
}
void MapMusic::select() {
    stop();
    samples_ = {};
    track_.clear();
    error_.clear();
    try {
        require(zone_ >= 0, "This field area has no assigned map music");
        auto id = u32(zones_, std::size_t(zone_) * 84 + (night_ ? 4 : 0));
        require(id != 0, "No base music is assigned to this map");
        auto found = catalog_.find(map_music_sound(id));
        require(found != catalog_.end(), "This map's music is not an external stream");
        track_ = found->second.file;
        track_volume_ = found->second.volume;
    } catch (const std::exception &e) {
        error_ = e.what();
    }
}
void MapMusic::play() {
    try {
        error_.clear();
        if (samples_.pcm.empty()) {
            if (pending_.valid())
                return;
            discard_ = false;
            auto path = dump_ / "romfs/data/sound" / std::filesystem::u8path(track_);
            pending_ = std::async(std::launch::async, [path] {
                return decode_music(read_file(path));
            });
            return;
        }
        if (!audio_) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                throw std::runtime_error(SDL_GetError());
            SDL_AudioSpec spec{};
            spec.format = SDL_AUDIO_S16;
            spec.channels = int(samples_.channels);
            spec.freq = int(samples_.rate);
            audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr,
                                               nullptr);
            require(audio_ != nullptr, SDL_GetError());
        }
        SDL_SetAudioStreamGain(audio_, volume_ * track_volume_);
        if (!SDL_ResumeAudioStreamDevice(audio_))
            throw std::runtime_error(SDL_GetError());
        playing_ = true;
        discard_ = false;
    } catch (const std::exception &e) {
        error_ = e.what();
        playing_ = false;
    }
}
void MapMusic::update(bool active) {
    if (!active && playing_) {
        playing_ = false;
        if (audio_)
            SDL_PauseAudioStreamDevice(audio_);
    }
    if (pending_.valid() &&
        pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto decoded = pending_.get();
            if (!discard_) {
                if (audio_) {
                    SDL_DestroyAudioStream(audio_);
                    audio_ = nullptr;
                }
                samples_ = std::move(decoded);
                cursor_ = 0;
                if (active)
                    play();
            }
        } catch (const std::exception &e) {
            if (!discard_)
                error_ = e.what();
        }
    }
    if (!playing_ || !audio_)
        return;
    auto queued = SDL_GetAudioStreamQueued(audio_);
    if (queued < 0) {
        error_ = SDL_GetError();
        stop();
        return;
    }
    auto target = std::size_t(samples_.rate) * samples_.channels * 2 / 4;
    while (std::size_t(queued) < target) {
        if (cursor_ == samples_.frames()) {
            if (loop_ && samples_.looping)
                cursor_ = samples_.loop_start;
            else {
                if (queued == 0) {
                    SDL_FlushAudioStream(audio_);
                    playing_ = false;
                }
                break;
            }
        }
        auto frames = std::min<std::size_t>(4096, samples_.frames() - cursor_);
        auto bytes = int(frames * samples_.channels * 2);
        if (!SDL_PutAudioStreamData(audio_, samples_.pcm.data() + cursor_ * samples_.channels,
                                    bytes)) {
            error_ = SDL_GetError();
            stop();
            return;
        }
        cursor_ += frames;
        queued += bytes;
    }
}
void MapMusic::draw(const Environment *scene, const std::filesystem::path &dump, int zone) {
    if (scene != scene_ || dump != dump_) {
        stop();
        scene_ = scene;
        dump_ = dump;
        locations_ = scene ? scene->locations : std::vector<MapLocation>{};
        zone_ = zone;
        if (std::none_of(locations_.begin(), locations_.end(), [&](const auto &l) {
                return l.zone == zone_;
            }))
            zone_ = locations_.empty() ? -1 : locations_.front().zone;
        catalog_.clear();
        zones_.clear();
        track_.clear();
        samples_ = {};
        error_.clear();
        if (scene)
            try {
                catalog_ = read_music_catalog(dump);
                zones_ = Archive(dump / TargetProfile::zone_archive).decoded(0);
                select();
            } catch (const std::exception &e) {
                error_ = e.what();
            }
    }
    if (!studio::TutorialWidgets::CollapsingHeader("map_music", "Map music"))
        return;
    if (!scene) {
        ImGui::TextDisabled("Load a map to preview its music.");
        return;
    }
    std::string label = "Unassigned";
    for (auto &l : locations_)
        if (l.zone == zone_)
            label = l.name + " (zone " + std::to_string(l.zone) + ")";
    if (ImGui::BeginCombo("Music zone", label.c_str())) {
        for (auto &l : locations_) {
            auto name = l.name + " (zone " + std::to_string(l.zone) + ")";
            if (ImGui::Selectable(name.c_str(), l.zone == zone_)) {
                zone_ = l.zone;
                select();
            }
        }
        ImGui::EndCombo();
    }
    bool changed = false;
    changed |= studio::TutorialWidgets::RadioButton("map_music", "Day music", !night_);
    if (changed)
        night_ = false;
    ImGui::SameLine();
    if (studio::TutorialWidgets::RadioButton("map_music", "Night music", night_)) {
        night_ = true;
        changed = true;
    }
    if (changed)
        select();
    if (!track_.empty())
        ImGui::TextWrapped("%s", track_.c_str());
    bool loading = pending_.valid();
    ImGui::BeginDisabled(track_.empty() || loading);
    if (studio::TutorialWidgets::Button("map_music", playing_ ? "Pause music" : "Play music")) {
        if (playing_) {
            playing_ = false;
            SDL_PauseAudioStreamDevice(audio_);
        } else {
            if (cursor_ == samples_.frames())
                cursor_ = 0;
            play();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("map_music", "Stop music"))
        stop();
    if (loading)
        ImGui::TextDisabled("Decoding music...");
    ImGui::SetNextItemWidth(-90);
    if (ImGui::SliderFloat("Volume", &volume_, 0, 1, "%.2f") && audio_)
        SDL_SetAudioStreamGain(audio_, volume_ * track_volume_);
    studio::TutorialWidgets::Checkbox("map_music", "Use track loop", &loop_);
    if (!samples_.pcm.empty()) {
        ImGui::TextDisabled("%.1f seconds | %u Hz | %s", double(samples_.frames()) / samples_.rate,
                            samples_.rate, samples_.looping ? "Looping track" : "One-shot track");
    }
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::TextWrapped(
        "Base map music preview. Story events and ride overrides are not simulated.");
}
}
