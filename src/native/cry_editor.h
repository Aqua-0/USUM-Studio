#pragma once
#include "native/project_binding.h"
#include "audio/cry_document.h"
#include "assets/model_document.h"
#include "native/folder_picker.h"
#include <SDL3/SDL.h>
#include <functional>
#include <future>
namespace studio {
class CryEditor {
  public:
    explicit CryEditor(SDL_Window *window) : window_(window) {
    }
    ~CryEditor();
    void draw(const ModelDocument &model);
    void update(bool active);
    void request_leave(std::function<void()> action);

  private:
    ProjectBinding project_;
    void bind_project();
    enum class FileAction { Import, Save, Load, Export, Wave };
    void choose(FileAction action);
    void select(unsigned species, unsigned form);
    void refresh();
    void play(const MusicSamples &samples);
    void stop();
    void save(const std::filesystem::path &path);
    SDL_Window *window_;
    SDL_AudioStream *audio_ = nullptr;
    bool audio_initialized_ = false, playing_ = false;
    std::unique_ptr<CryDocument> document_;
    std::filesystem::path saved_path_, requested_dump_;
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    FileAction action_ = FileAction::Import;
    std::future<std::string> export_;
    std::function<void()> leave_;
    CryBinding binding_;
    unsigned species_ = 0, form_ = 0;
    int context_ = 0;
    Bytes original_, current_;
    MusicSamples original_audio_, current_audio_, imported_;
    std::vector<std::string> uses_;
    std::string error_, notice_, import_name_;
    float volume_ = .5f, trim_start_ = 0, trim_end_ = 0, gain_ = 1;
};
}
