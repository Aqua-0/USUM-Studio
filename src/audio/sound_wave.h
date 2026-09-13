#pragma once
#include "audio/music.h"
namespace studio {
MusicSamples decode_sound_wave(View bytes);
void validate_sound_wave_replacement(View original, View replacement);
Bytes replace_sound_wave(View original, const MusicSamples &input);
}
