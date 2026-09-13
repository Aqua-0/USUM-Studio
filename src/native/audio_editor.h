#pragma once
#include "native/project_binding.h"
#include "audio/audio_document.h"
#include "native/cry_editor.h"
namespace studio {
class AudioEditor {
  public:
    AudioEditor(SDL_Window *window, CryEditor &cries) : window_(window), cries_(cries) {
    }
    ~AudioEditor();
    bool cries_active() const {
        return category_ == 1;
    }
    void draw(const std::filesystem::path &dump);
    void update(bool active);
    void request_leave(std::function<void()> action);

  private:
    ProjectBinding project_;
    void bind_project();
    enum class Action { Import, Wave, Save, Load, Export };
    void choose(Action action);
    void select(std::size_t index);
    void play();
    void stop();
    void scan(const std::filesystem::path &dump);
    SDL_Window *window_;
    CryEditor &cries_;
    SDL_AudioStream *audio_ = nullptr;
    bool initialized_ = false, playing_ = false, loop_ = true;
    std::unique_ptr<AudioDocument> document_;
    std::future<std::unique_ptr<AudioDocument>> scan_;
    std::future<MusicSamples> decoding_;
    std::future<Bytes> encoding_;
    std::future<std::string> exporting_;
    std::vector<PokemonEntry> pokemon_;
    std::future<std::vector<PokemonEntry>> pokemon_job_;
    std::filesystem::path dump_, saved_;
    std::size_t selected_ = 0, cry_ = 0, cursor_ = 0;
    int category_ = 0;
    float volume_ = .5f;
    char search_[160]{};
    MusicSamples samples_;
    std::vector<float> waveform_;
    std::shared_ptr<FolderSelection> dialog_ = std::make_shared<FolderSelection>();
    Action action_ = Action::Import;
    std::function<void()> leave_;
    std::string error_, notice_ = "Load the audio library from the selected dump.";
};
}
