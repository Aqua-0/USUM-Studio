#pragma once
#include "audio/audio_document.h"
namespace studio {
struct AmbientSample {
    std::string label;
    Bytes wave;
};
unsigned ambient_sound_index(unsigned reference);
std::vector<AmbientSample> ambient_sound_samples(const AudioDocument &audio, unsigned reference);
}
