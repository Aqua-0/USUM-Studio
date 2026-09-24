#pragma once
#include "audio/ambient_sound.h"
namespace studio {
struct ScriptSoundChoice {
    unsigned reference, archive_sound, samples;
    std::string label;
};
unsigned script_sound_index(unsigned reference);
std::vector<ScriptSoundChoice> script_sound_choices(const AudioDocument &audio);
std::vector<AmbientSample> script_sound_samples(const AudioDocument &audio, unsigned reference);
}
