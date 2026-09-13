#pragma once
#include "core/binary.h"
#include <map>
namespace studio {
struct MusicTrack {
    std::string file;
    float volume = 1;
};
struct MusicSamples {
    unsigned rate = 0, channels = 0, loop_start = 0;
    bool looping = false;
    std::vector<std::int16_t> pcm;
    std::size_t frames() const {
        return channels ? pcm.size() / channels : 0;
    }
};
MusicSamples decode_music(View bytes);
std::map<unsigned, MusicTrack> read_music_catalog(const std::filesystem::path &dump);
unsigned map_music_sound(unsigned middle_id);
}
