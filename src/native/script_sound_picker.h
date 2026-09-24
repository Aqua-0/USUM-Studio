#pragma once
#include "audio/script_sound.h"
#include "native/ambient_sound_preview.h"
namespace studio {
class ScriptSoundPicker {
  public:
    bool draw(const std::filesystem::path &source, unsigned &reference);
    void update(bool active);
    void reset();

  private:
    AmbientSoundPreview preview_;
    std::shared_ptr<const AudioDocument> audio_;
    std::future<std::shared_ptr<const AudioDocument>> loading_;
    std::filesystem::path source_, loading_source_;
    std::vector<ScriptSoundChoice> choices_;
    std::string error_;
    char search_[128]{};
    unsigned candidate_ = 327694;
    bool drawn_ = false;
};
}
