#include "audio/wave_import.h"
#include "inspector_selector.h"
#include "native/tutorial_widgets.h"
#include "native/audio_editor.h"
#include "native/theme.h"
#include <algorithm>
#include <cmath>
#include <cctype>
namespace studio {
AudioEditor::~AudioEditor() {
    stop();
    if (initialized_)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
void AudioEditor::stop() {
    playing_ = false;
    cursor_ = 0;
    if (audio_) {
        SDL_DestroyAudioStream(audio_);
        audio_ = nullptr;
    }
}
void AudioEditor::play() {
    try {
        if (samples_.pcm.empty())
            return;
        if (!initialized_) {
            require(SDL_InitSubSystem(SDL_INIT_AUDIO), SDL_GetError());
            initialized_ = true;
        }
        if (!audio_) {
            SDL_AudioSpec spec{SDL_AUDIO_S16, int(samples_.channels), int(samples_.rate)};
            audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr,
                                               nullptr);
            require(audio_, SDL_GetError());
        }
        SDL_SetAudioStreamGain(audio_, volume_);
        require(SDL_ResumeAudioStreamDevice(audio_), SDL_GetError());
        playing_ = true;
    } catch (const std::exception &e) {
        error_ = e.what();
        stop();
    }
}
void AudioEditor::select(std::size_t index) {
    stop();
    selected_ = index;
    samples_ = {};
    waveform_.clear();
    error_.clear();
    try {
        auto bytes = document_->current(index);
        bool effect = document_->tracks.at(index).effect;
        decoding_ = std::async(std::launch::async, [bytes = std::move(bytes), effect] {
            return effect ? decode_sound_wave(bytes) : decode_music(bytes);
        });
    } catch (const std::exception &e) {
        error_ = e.what();
    }
}
void AudioEditor::scan(const std::filesystem::path &dump) {
    stop();
    dump_ = dump;
    category_ = 0;
    selected_ = 0;
    document_.reset();
    pokemon_.clear();
    samples_ = {};
    waveform_.clear();
    saved_.clear();
    error_.clear();
    scan_ = std::async(std::launch::async, [dump] {
        return std::make_unique<AudioDocument>(dump);
    });
    pokemon_job_ = std::async(std::launch::async, [dump] {
        return load_pokemon_catalog(dump);
    });
}
void AudioEditor::request_leave(std::function<void()> action) {
    if (project_store()) {
        try {
            save_editor_project();
            action();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
        return;
    }
    bool pending;
    {
        std::lock_guard lock(dialog_->mutex);
        pending = dialog_->pending;
    }
    if (pending || exporting_.valid() || encoding_.valid()) {
        notice_ = "Finish the audio file operation before continuing.";
        return;
    }
    if (document_ && document_->dirty())
        leave_ = std::move(action);
    else
        action();
}
void AudioEditor::choose(Action action) {
    if (action == Action::Save && project_store()) {
        try {
            save_editor_project();
            notice_ = "Project saved. Stage Project builds the overlay.";
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
        return;
    }
    action_ = action;
    if (action == Action::Export) {
        choose_folder(window_, dialog_, nullptr);
        return;
    }
    {
        std::lock_guard lock(dialog_->mutex);
        dialog_->pending = true;
        dialog_->ready = false;
        dialog_->path.clear();
        dialog_->error.clear();
    }
    auto *owner = new std::shared_ptr<FolderSelection>(dialog_);
    static SDL_DialogFileFilter native[] = {{"Game music stream", "bcstm"}},
                                wav[] = {{"Wave audio", "wav"}},
                                project[] = {{"Audio replacement project", "usum-audio"}};
    auto callback = [](void *data, const char *const *files, int) {
        std::unique_ptr<std::shared_ptr<FolderSelection>> owner(
            static_cast<std::shared_ptr<FolderSelection> *>(data));
        auto &state = **owner;
        std::lock_guard lock(state.mutex);
        if (!files)
            state.error = SDL_GetError();
        else if (files[0])
            state.path = files[0];
        state.pending = false;
        state.ready = true;
    };
    if (action == Action::Import || action == Action::Load)
        SDL_ShowOpenFileDialog(callback, owner, window_,
                               action == Action::Import
                                   ? (document_->tracks.at(selected_).effect ? wav : native)
                                   : project,
                               1, nullptr, false);
    else
        SDL_ShowSaveFileDialog(callback, owner, window_, action == Action::Wave ? wav : project, 1,
                               action == Action::Wave ? "audio.wav" : "audio.usum-audio");
}
void AudioEditor::update(bool active) {
    if (!active && playing_) {
        playing_ = false;
        if (audio_)
            SDL_PauseAudioStreamDevice(audio_);
    }
    if (scan_.valid() && scan_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            document_ = scan_.get();
            bind_project();
            notice_ = "Audio library ready.";
            if (!document_->tracks.empty())
                select(0);
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    if (pokemon_job_.valid() &&
        pokemon_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            pokemon_ = pokemon_job_.get();
            cry_ = 0;
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    if (decoding_.valid() &&
        decoding_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            samples_ = decoding_.get();
            waveform_.clear();
            auto stride = std::max<std::size_t>(1, samples_.frames() / 1000);
            for (std::size_t i = 0; i < samples_.frames(); i += stride) {
                int peak = 0;
                for (auto k = i; k < std::min(i + stride, samples_.frames()); ++k)
                    for (unsigned c = 0; c < samples_.channels; ++c) {
                        int value = samples_.pcm[k * samples_.channels + c];
                        if (std::abs(value) > std::abs(peak))
                            peak = value;
                    }
                waveform_.push_back(peak / 32768.f);
            }
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    if (encoding_.valid() &&
        encoding_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            document_->replace(selected_, encoding_.get());
            select(selected_);
            notice_ = "Sound sample replacement applied.";
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    if (playing_ && audio_)
        try {
            auto queued = SDL_GetAudioStreamQueued(audio_);
            require(queued >= 0, SDL_GetError());
            while (queued < int(samples_.rate * samples_.channels / 2)) {
                if (cursor_ == samples_.frames()) {
                    if (loop_ && samples_.looping)
                        cursor_ = samples_.loop_start;
                    else {
                        SDL_FlushAudioStream(audio_);
                        if (!queued)
                            playing_ = false;
                        break;
                    }
                }
                auto frames = std::min<std::size_t>(4096, samples_.frames() - cursor_);
                int bytes = int(frames * samples_.channels * 2);
                require(SDL_PutAudioStreamData(
                            audio_, samples_.pcm.data() + cursor_ * samples_.channels, bytes),
                        SDL_GetError());
                cursor_ += frames;
                queued += bytes;
            }
        } catch (const std::exception &e) {
            error_ = e.what();
            stop();
        }
    std::string path;
    {
        std::lock_guard lock(dialog_->mutex);
        if (dialog_->ready) {
            dialog_->ready = false;
            path = dialog_->path;
            if (!dialog_->error.empty())
                error_ = dialog_->error;
        }
    }
    if (!path.empty() && document_)
        try {
            auto file = std::filesystem::u8path(path);
            if (action_ == Action::Import) {
                require(std::filesystem::file_size(file) <= 512ull * 1024 * 1024,
                        "Stream import exceeds the decoder's supported size");
                if (document_->tracks.at(selected_).effect) {
                    auto input = import_audio_wav(read_file(file));
                    auto original = document_->current(selected_);
                    encoding_ = std::async(std::launch::async, [original = std::move(original),
                                                                input = std::move(input)] {
                        return replace_sound_wave(original, input);
                    });
                } else {
                    document_->replace(selected_, read_file(file));
                    select(selected_);
                    notice_ = "Stream replacement applied.";
                }
            } else if (action_ == Action::Wave) {
                if (file.extension().empty())
                    file += ".wav";
                require_audio_output(dump_, file);
                write_file_atomic(file, export_audio_wav(samples_));
                notice_ = "WAV exported.";
            } else if (action_ == Action::Save) {
                if (file.extension().empty())
                    file += ".usum-audio";
                document_->save(file);
                saved_ = file;
                notice_ = "Audio project saved.";
                if (leave_) {
                    auto action = std::move(leave_);
                    leave_ = {};
                    action();
                }
            } else if (action_ == Action::Load) {
                document_->load(file);
                project_.imported();
                saved_ = file;
                select(selected_);
                notice_ = "Audio project opened.";
            } else {
                auto snapshot = *document_;
                exporting_ = std::async(std::launch::async, [snapshot = std::move(snapshot), file] {
                    snapshot.export_to(file);
                    return "Audio override exported to " + file.string();
                });
            }
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    if (exporting_.valid() &&
        exporting_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            notice_ = exporting_.get();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    if (leave_ && !ImGui::IsPopupOpen("Unsaved audio replacements"))
        ImGui::OpenPopup("Unsaved audio replacements");
    if (ImGui::BeginPopupModal("Unsaved audio replacements", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save the audio project before continuing?");
        bool pending;
        {
            std::lock_guard lock(dialog_->mutex);
            pending = dialog_->pending;
        }
        ImGui::BeginDisabled(pending);
        if (studio::TutorialWidgets::Button("audio_editor", "Save project"))
            choose(Action::Save);
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("audio_editor", "Discard")) {
            document_->discard();
            auto action = std::move(leave_);
            leave_ = {};
            ImGui::CloseCurrentPopup();
            action();
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("audio_editor", "Cancel")) {
            leave_ = {};
            ImGui::CloseCurrentPopup();
        }
        if (!leave_)
            ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndPopup();
    }
}
void AudioEditor::draw(const std::filesystem::path &dump) {
    bool pending;
    {
        std::lock_guard lock(dialog_->mutex);
        pending = dialog_->pending;
    }
    bool busy = scan_.valid() || pokemon_job_.valid() || decoding_.valid() || encoding_.valid() ||
                exporting_.valid() || pending || bool(leave_);
    ImGui::Begin("Audio library");
    ImGui::BeginDisabled(busy || dump.empty());
    if (primary_button(document_ ? "Reload audio library" : "Load audio library"))
        request_leave([this, dump] {
            scan(dump);
        });
    ImGui::EndDisabled();
    if (dump != dump_ && document_)
        ImGui::TextWrapped("Dump changed. Reload the audio library to use it.");
    ImGui::BeginDisabled(busy);
    static const char *categories[] = {"Music streams", "Pokemon cries", "Sound effects"};
    InspectorSelectorStyle category_style("Choose audio category");
    if (ImGui::Combo("##audio-category", &category_, categories, 3)) {
        stop();
        search_[0] = 0;
        if (category_ != 1 && document_) {
            auto it =
                std::find_if(document_->tracks.begin(), document_->tracks.end(), [&](auto &t) {
                    return t.effect == (category_ == 2);
                });
            if (it != document_->tracks.end())
                select(std::size_t(it - document_->tracks.begin()));
        }
    }
    category_style.end();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##audio-search", "Search audio", search_, sizeof(search_));
    std::string query = search_;
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    ImGui::BeginChild("Audio entries");
    auto count = category_ == 1 ? pokemon_.size() : document_ ? document_->tracks.size() : 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (category_ != 1 && document_->tracks[i].effect != (category_ == 2))
            continue;
        auto label = category_ == 1
                         ? pokemon_[i].label
                         : document_->tracks[i].label + (document_->edited(i) ? " *" : "");
        auto lower = label;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        if (lower.find(query) == std::string::npos)
            continue;
        ImGui::PushID(int(i));
        if (ImGui::Selectable(label.c_str(), i == (category_ == 1 ? cry_ : selected_))) {
            if (category_ == 1)
                cry_ = i;
            else
                try {
                    select(i);
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndDisabled();
    ImGui::End();
    ImGui::Begin("Audio studio");
    if (category_ == 1) {
        if (cry_ < pokemon_.size()) {
            ModelDocument model;
            model.dump = dump_;
            model.pokemon = pokemon_[cry_];
            cries_.draw(model);
        } else
            ImGui::TextUnformatted("Load the library to browse Pokemon cries.");
    } else if (document_ && !document_->tracks.empty()) {
        ImGui::TextWrapped("%s", document_->tracks[selected_].label.c_str());
        ImGui::BeginDisabled(busy || samples_.pcm.empty());
        if (primary_button(playing_ ? "Pause" : "Play")) {
            if (playing_) {
                playing_ = false;
                SDL_PauseAudioStreamDevice(audio_);
            } else {
                if (cursor_ >= samples_.frames())
                    cursor_ = 0;
                play();
            }
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("audio_editor", "Stop"))
            stop();
        ImGui::SameLine();
        studio::TutorialWidgets::Checkbox("audio_editor", "Loop", &loop_);
        ImGui::SetNextItemWidth(180);
        if (ImGui::SliderFloat("Volume", &volume_, 0, 1) && audio_)
            SDL_SetAudioStreamGain(audio_, volume_);
        ImGui::PlotLines("##audio-waveform", waveform_.data(), int(waveform_.size()), 0, nullptr,
                         -1, 1, {-1, 180});
        if (samples_.rate) {
            float position = float(cursor_) / samples_.rate;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##audio-position", &position, 0,
                                   float(samples_.frames()) / samples_.rate, "%.2f s")) {
                cursor_ = std::min(samples_.frames(), std::size_t(position * samples_.rate));
                if (audio_)
                    SDL_ClearAudioStream(audio_);
            }
            ImGui::Text("%u Hz | %u channels | %.2f seconds", samples_.rate, samples_.channels,
                        double(samples_.frames()) / samples_.rate);
        }
        ImGui::EndDisabled();
        ImGui::End();
        ImGui::Begin("Audio inspector");
        ImGui::BeginDisabled(busy);
        if (studio::TutorialWidgets::Button("audio_editor", "Undo")) {
            if (document_->undo())
                select(selected_);
        }
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("audio_editor", "Redo")) {
            if (document_->redo())
                select(selected_);
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(busy);
        if (studio::TutorialWidgets::Button(
                "audio_editor", project_store() ? "Save Project" : "Save audio project..."))
            choose(Action::Save);
        if (studio::TutorialWidgets::Button("audio_editor", "Open audio project..."))
            request_leave([this] {
                choose(Action::Load);
            });
        ImGui::BeginDisabled(!document_->size());
        if (primary_button("Export audio override..."))
            choose(Action::Export);
        ImGui::EndDisabled();
        ImGui::Text("%zu replacements%s", document_->size(),
                    document_->dirty() ? " | Unsaved" : "");
        ImGui::EndDisabled();
        ImGui::SeparatorText("Edit track");
        ImGui::BeginDisabled(busy || samples_.pcm.empty());
        if (studio::TutorialWidgets::Button("audio_editor", "Export WAV..."))
            choose(Action::Wave);
        ImGui::SameLine();
        if (studio::TutorialWidgets::Button("audio_editor", document_->tracks[selected_].effect
                                                                ? "Import WAV..."
                                                                : "Import BCSTM..."))
            choose(Action::Import);
        if (ImGui::CollapsingHeader("Replacement requirements")) {
            if (document_->tracks[selected_].effect)
                ImGui::TextWrapped(
                    "Replace the underlying sample. Game sequences may change pitch and "
                    "timing. WAV is converted to the native rate and channels; shorter "
                    "clips are padded. Keep the existing duration and loop points.");
            else
                ImGui::TextWrapped(
                    "Music replacements retain duration, channels, sample rate and loop "
                    "points. Export WAV for external editing, then encode a matching "
                    "BCSTM. WAV import is available for Pokemon cries.");
        }

        if (studio::TutorialWidgets::Button("audio_editor", "Reset track")) {
            document_->reset(selected_);
            select(selected_);
        }
        ImGui::EndDisabled();
    } else
        ImGui::TextUnformatted("Load the audio library to begin.");
    ImGui::End();
    ImGui::Begin("Audio inspector");
    if (ImGui::CollapsingHeader("Source details")) {
        ImGui::TextWrapped("Dump: %s", dump_.string().c_str());
        if (document_ && category_ != 1 && selected_ < document_->tracks.size()) {
            auto &track = document_->tracks[selected_];
            ImGui::TextWrapped("Source: romfs/data/sound/%s", track.file.c_str());
            if (track.effect)
                ImGui::Text("Embedded sample offset: %zu", track.offset);
            ImGui::Text("Frames: %zu", samples_.frames());
            if (samples_.looping)
                ImGui::Text("Loop starts at sample %u", samples_.loop_start);
            ImGui::TextWrapped(track.effect
                                   ? "Referenced by %zu sounds through their instrument banks:"
                                   : "Referenced by %zu sound entries:",
                               track.sounds.size());
            std::string ids;
            for (auto id : track.sounds) {
                if (!ids.empty())
                    ids += ", ";
                ids += std::to_string(id);
            }
            ImGui::TextWrapped("%s", ids.c_str());
        }
    }
    ImGui::Separator();
    if (busy)
        ImGui::TextUnformatted("Processing audio...");
    ImGui::TextWrapped("%s", notice_.c_str());
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::End();
}
void AudioEditor::bind_project() {
    if (!document_)
        return;
    project_.bind(
        "audio", "audio", "Music and sound effects", "",
        [this] {
            return document_ && document_->dirty();
        },
        [this] {
            require(!encoding_.valid() && !exporting_.valid(),
                    "Wait for audio processing before saving");
            return project_encode_file([copy = *document_](const auto &path) mutable {
                copy.save(path);
            });
        },
        [this] {
            document_->mark_saved();
        });
    project_.ready([this] {
        if (document_) {
            require(!encoding_.valid() && !exporting_.valid(),
                    "Wait for audio processing before saving");
        }
    });
    if (auto file = project_.document(); !file.empty()) {
        document_->load(file);
    }
}
}
