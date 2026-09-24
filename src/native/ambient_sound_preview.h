#pragma once
#include "audio/ambient_sound.h"
#include <SDL3/SDL.h>
#include <future>
namespace studio {
class AmbientSoundPreview {
  public:
    ~AmbientSoundPreview();
    void reset();
    void draw(const std::filesystem::path &dump, unsigned sound, bool script_reference = false,
              std::shared_ptr<const AudioDocument> library = {});
    void update(bool active);

  private:
    void stop();
    void play();
    std::filesystem::path dump_;
    unsigned sound_ = 0;
    std::size_t selected_ = 0, cursor_ = 0;
    bool initialized_ = false, playing_ = false, loop_ = false, discard_ = false;
    float volume_ = .5f;
    SDL_AudioStream *stream_ = nullptr;
    std::future<std::vector<AmbientSample>> loading_;
    std::vector<AmbientSample> clips_;
    MusicSamples samples_;
    std::string error_;
};
}
