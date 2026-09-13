#include "native/tutorial_widgets.h"
#include "native/cry_editor.h"
#include "audio/audio_document.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
namespace studio {
CryEditor::~CryEditor() {
    stop();
    if (audio_initialized_)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
void CryEditor::stop() {
    playing_ = false;
    if (audio_) {
        SDL_DestroyAudioStream(audio_);
        audio_ = nullptr;
    }
}
void CryEditor::play(const MusicSamples &samples) {
    stop();
    try {
        require(!samples.pcm.empty(), "No cry samples are available");
        if (!audio_initialized_) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                throw std::runtime_error(SDL_GetError());
            audio_initialized_ = true;
        }
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = int(samples.channels);
        spec.freq = int(samples.rate);
        audio_ =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        require(audio_ != nullptr, SDL_GetError());
        SDL_SetAudioStreamGain(audio_, volume_);
        if (!SDL_PutAudioStreamData(audio_, samples.pcm.data(), int(samples.pcm.size() * 2)) ||
            !SDL_FlushAudioStream(audio_) || !SDL_ResumeAudioStreamDevice(audio_))
            throw std::runtime_error(SDL_GetError());
        playing_ = true;
        error_.clear();
    } catch (const std::exception &e) {
        error_ = e.what();
        stop();
    }
}
void CryEditor::refresh() {
    stop();
    original_ = document_->library.archive.decoded(binding_.wave_archive);
    current_ = document_->current(binding_.wave_archive);
    original_audio_ = decode_cry(original_);
    current_audio_ = decode_cry(current_);
    uses_ = document_->library.uses(binding_.wave_archive);
}
void CryEditor::select(unsigned species, unsigned form) {
    species_ = species;
    form_ = form;
    original_.clear();
    current_.clear();
    original_audio_ = {};
    current_audio_ = {};
    uses_.clear();
    stop();
    try {
        binding_ = document_->library.binding(species, form, unsigned(context_));
        refresh();
        error_.clear();
    } catch (const std::exception &e) {
        error_ = e.what();
    }
}
void CryEditor::request_leave(std::function<void()> action) {
    if (project_store()) {
        try {
            save_editor_project();
            action();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
        return;
    }
    if (leave_)
        return;
    if (document_ && document_->dirty())
        leave_ = std::move(action);
    else
        action();
}
void CryEditor::save(const std::filesystem::path &path) {
    document_->save(path);
    saved_path_ = path;
    notice_ = "Cry project saved.";
    if (leave_) {
        auto action = std::move(leave_);
        leave_ = {};
        action();
    }
}
void CryEditor::choose(FileAction action) {
    if (action == FileAction::Save && project_store()) {
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
    if (action == FileAction::Export) {
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
    static const SDL_DialogFileFilter wav[] = {{"PCM or float WAV", "wav"}},
                                      project[] = {{"Cry replacement project", "usum-cry"}};
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
    if (action == FileAction::Save || action == FileAction::Wave)
        SDL_ShowSaveFileDialog(callback, owner, window_, action == FileAction::Wave ? wav : project,
                               1,
                               action == FileAction::Wave ? "cry.wav"
                               : saved_path_.empty()      ? nullptr
                                                          : saved_path_.string().c_str());
    else
        SDL_ShowOpenFileDialog(callback, owner, window_,
                               action == FileAction::Import ? wav : project, 1, nullptr, false);
}
void CryEditor::update(bool active) {
    if (!active)
        stop();
    else if (playing_ && audio_ && SDL_GetAudioStreamQueued(audio_) == 0 &&
             SDL_GetAudioStreamAvailable(audio_) == 0)
        playing_ = false;
    std::string path, dialog_error;
    {
        std::lock_guard lock(dialog_->mutex);
        if (dialog_->ready) {
            dialog_->ready = false;
            path = dialog_->path;
            dialog_error = dialog_->error;
        }
    }
    if (!dialog_error.empty())
        error_ = dialog_error;
    if (!path.empty() && document_)
        try {
            error_.clear();
            auto file = std::filesystem::u8path(path);
            if (action_ == FileAction::Wave) {
                if (file.extension().empty())
                    file += ".wav";
                require_audio_output(document_->dump, file);
                write_file_atomic(file, export_audio_wav(current_audio_));
                notice_ = "Current cry clip exported as WAV.";
            } else if (action_ == FileAction::Import) {
                require(std::filesystem::file_size(file) <= 128 * 1024 * 1024,
                        "Import a WAV smaller than 128 MiB");
                imported_ = import_cry_wav(read_file(file));
                import_name_ = file.filename().string();
                trim_start_ = 0;
                trim_end_ = float(double(imported_.frames()) / imported_.rate);
                gain_ = 1;
                notice_ = "WAV loaded. Review trim and gain, then Apply replacement.";
            } else if (action_ == FileAction::Save) {
                if (file.extension().empty())
                    file += ".usum-cry";
                save(file);
            } else if (action_ == FileAction::Load) {
                document_->load(file);
                project_.imported();
                saved_path_ = file;
                select(species_, form_);
                notice_ = "Cry project opened.";
            } else {
                auto snapshot = *document_;
                export_ = std::async(std::launch::async, [snapshot = std::move(snapshot), file] {
                    snapshot.export_to(file);
                    return "Exported " + (file / CryProfile::archive).string();
                });
            }
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    if (export_.valid() && export_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try {
            notice_ = export_.get();
            error_.clear();
        } catch (const std::exception &e) {
            error_ = e.what();
        }
    if (leave_ && !ImGui::IsPopupOpen("Unsaved cry replacements"))
        ImGui::OpenPopup("Unsaved cry replacements");
    if (ImGui::BeginPopupModal("Unsaved cry replacements", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!leave_)
            ImGui::CloseCurrentPopup();
        else {
            ImGui::TextUnformatted("Save the cry replacements before continuing?");
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            bool pending;
            {
                std::lock_guard lock(dialog_->mutex);
                pending = dialog_->pending;
            }
            ImGui::BeginDisabled(pending);
            if (studio::TutorialWidgets::Button("cry_editor", "Save cry project")) {
                try {
                    if (saved_path_.empty())
                        choose(FileAction::Save);
                    else
                        save(saved_path_);
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            }
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("cry_editor", "Discard cry changes")) {
                document_->discard();
                select(species_, form_);
                auto action = std::move(leave_);
                leave_ = {};
                ImGui::CloseCurrentPopup();
                action();
            }
            ImGui::SameLine();
            if (studio::TutorialWidgets::Button("cry_editor", "Cancel")) {
                leave_ = {};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }
}
void CryEditor::draw(const ModelDocument &model) {
    if (!document_ || document_->dump != model.dump) {
        auto dump = model.dump;
        if (requested_dump_ != dump) {
            requested_dump_ = dump;
            request_leave([this, dump] {
                stop();
                try {
                    document_ = std::make_unique<CryDocument>(dump);
                    bind_project();
                    saved_path_.clear();
                    species_ = 0;
                    error_.clear();
                } catch (const std::exception &e) {
                    error_ = e.what();
                }
            });
        }
        if (!document_ || document_->dump != model.dump) {
            ImGui::TextWrapped("%s", error_.empty()
                                         ? "Load this Pokemon's cry source to continue. Previous "
                                           "replacements are kept until you save or discard them."
                                         : error_.c_str());
            if (studio::TutorialWidgets::Button("cry_editor", "Load cry source"))
                requested_dump_.clear();
            return;
        }
    }
    if (species_ != model.pokemon.species || form_ != model.pokemon.form)
        select(model.pokemon.species, model.pokemon.form);
    ImGui::TextUnformatted("Pokemon cries");
    ImGui::TextWrapped("Preview the source clip. Game sequences can change its pitch and timing; "
                       "export preserves those sequences.");
    bool pending;
    {
        std::lock_guard lock(dialog_->mutex);
        pending = dialog_->pending;
    }
    bool busy = pending || export_.valid() || bool(leave_);
    ImGui::BeginDisabled(busy);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##cry-context", cry_contexts[context_])) {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(cry_contexts[i], context_ == i)) {
                context_ = i;
                select(species_, form_);
            }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(current_.empty());
    if (studio::TutorialWidgets::Button("cry_editor", "Play original clip"))
        play(original_audio_);
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("cry_editor", "Stop clip"))
        stop();
    if (current_ != original_ && studio::TutorialWidgets::Button("cry_editor", "Play edited clip"))
        play(current_audio_);
    ImGui::SetNextItemWidth(-85);
    if (ImGui::SliderFloat("Volume", &volume_, 0, 1, "%.2f") && audio_)
        SDL_SetAudioStreamGain(audio_, volume_);
    if (playing_)
        ImGui::TextDisabled("Playing cry clip...");
    if (!current_audio_.pcm.empty()) {
        ImGui::Text("%.2f s | %u Hz | %zu bytes",
                    double(current_audio_.frames()) / current_audio_.rate, current_audio_.rate,
                    current_.size());
        std::vector<float> peaks;
        auto step = std::max<std::size_t>(1, current_audio_.frames() / 256);
        for (std::size_t i = 0; i < current_audio_.frames(); i += step) {
            int peak = 0;
            for (auto k = i; k < std::min(i + step, current_audio_.frames()); ++k)
                if (std::abs(int(current_audio_.pcm[k])) > std::abs(peak))
                    peak = current_audio_.pcm[k];
            peaks.push_back(float(peak) / 32768);
        }
        ImGui::PlotLines("##cry-waveform", peaks.data(), int(peaks.size()), 0, nullptr, -1, 1,
                         ImVec2(-1, 70));
    }
    ImGui::TextWrapped(
        "Replacing this sample affects %zu mapped uses, including shared contexts and forms.",
        uses_.size());
    if (studio::TutorialWidgets::TreeNode("cry_editor", "Shared sample uses")) {
        for (auto &use : uses_)
            ImGui::TextWrapped("%s", use.c_str());
        ImGui::TextWrapped("Forms without a separate cry mapping also use their base cry.");
        ImGui::TreePop();
    }
    if (studio::TutorialWidgets::Button("cry_editor", "Import WAV..."))
        choose(FileAction::Import);
    if (!imported_.pcm.empty()) {
        ImGui::TextWrapped("Input: %s", import_name_.c_str());
        ImGui::TextDisabled("Input length: %.2f s", double(imported_.frames()) / imported_.rate);
        ImGui::SetNextItemWidth(-85);
        ImGui::DragFloat("Trim start", &trim_start_, .01f, 0,
                         float(double(imported_.frames()) / imported_.rate), "%.2f s");
        ImGui::SetNextItemWidth(-85);
        ImGui::DragFloat("Trim end", &trim_end_, .01f, 0,
                         float(double(imported_.frames()) / imported_.rate), "%.2f s");
        ImGui::SetNextItemWidth(-85);
        ImGui::SliderFloat("Gain", &gain_, 0, 4, "%.2fx");
        ImGui::TextWrapped(
            "Replacement: mono 16 kHz DSP ADPCM. Trim and gain take effect when you apply.");
        if (studio::TutorialWidgets::Button("cry_editor", "Apply replacement")) {
            try {
                auto prepared = prepare_cry(imported_, trim_start_, trim_end_, gain_);
                document_->replace(binding_.wave_archive, encode_cry(original_, prepared));
                refresh();
                error_.clear();
                notice_ = "Replacement applied. Save the cry project or export game files.";
            } catch (const std::exception &e) {
                error_ = e.what();
            }
        }
    }
    if (studio::TutorialWidgets::Button("cry_editor", "Reset this sample")) {
        document_->reset(binding_.wave_archive);
        select(species_, form_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("cry_editor", "Undo cry")) {
        if (document_->undo())
            select(species_, form_);
    }
    ImGui::SameLine();
    if (studio::TutorialWidgets::Button("cry_editor", "Redo cry")) {
        if (document_->redo())
            select(species_, form_);
    }
    ImGui::Separator();
    ImGui::Text("%zu replacements%s", document_->size(), document_->dirty() ? " | Unsaved" : "");
    if (studio::TutorialWidgets::Button("cry_editor",
                                        project_store() ? "Save Project" : "Save cry project..."))
        choose(FileAction::Save);
    if (studio::TutorialWidgets::Button("cry_editor", "Open cry project..."))
        request_leave([this] {
            choose(FileAction::Load);
        });
    ImGui::BeginDisabled(!document_->size());
    if (studio::TutorialWidgets::Button("cry_editor", "Export cry override..."))
        choose(FileAction::Export);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(current_audio_.pcm.empty());
    if (studio::TutorialWidgets::Button("cry_editor", "Export current cry WAV..."))
        choose(FileAction::Wave);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (export_.valid())
        ImGui::TextDisabled("Exporting cry archive...");
    if (!notice_.empty())
        ImGui::TextWrapped("%s", notice_.c_str());
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
}
void CryEditor::bind_project() {
    if (!document_)
        return;
    project_.bind(
        "cries", "cries", "Pokemon cries", "",
        [this] {
            return document_ && document_->dirty();
        },
        [this] {
            require(!export_.valid(), "Wait for cry export before saving");
            return project_encode_file([copy = *document_](const auto &path) mutable {
                copy.save(path);
            });
        },
        [this] {
            document_->mark_saved();
        });
    project_.ready([this] {
        if (document_) {
            require(!export_.valid(), "Wait for cry export before saving");
        }
    });
    if (auto file = project_.document(); !file.empty()) {
        document_->load(file);
    }
}
}
