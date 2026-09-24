#include "audio/script_sound.h"
#include "audio/script_sound_profile.h"
#include <algorithm>
namespace studio {
unsigned script_sound_index(unsigned reference) {
    auto group = reference >> 16, index = reference & 65535;
    for (auto range : script_sound_ranges)
        if (group == range.group && index >= range.first && index - range.first < range.count)
            return range.sound + index - range.first;
    throw std::runtime_error("No verified sound-effect mapping for this script ID");
}
std::vector<ScriptSoundChoice> script_sound_choices(const AudioDocument &audio) {
    std::map<unsigned, unsigned> counts;
    for (const auto &track : audio.tracks)
        if (track.effect)
            for (auto sound : track.sounds)
                ++counts[sound];
    std::vector<ScriptSoundChoice> result;
    for (auto range : script_sound_ranges)
        for (unsigned i = 0; i < range.count; ++i) {
            auto sound = range.sound + i;
            if (!counts.contains(sound))
                continue;
            auto reference = (range.group << 16) | (range.first + i);
            auto label = reference == 327694 ? std::string("Dialogue chime / sound 327694")
                                             : "Sound " + std::to_string(reference);
            label += " / group " + std::to_string(range.group) + " / entry " +
                     std::to_string(range.first + i) + " / archive " + std::to_string(sound);
            result.push_back({reference, sound, counts.at(sound), std::move(label)});
        }
    return result;
}
std::vector<AmbientSample> script_sound_samples(const AudioDocument &audio, unsigned reference) {
    auto sound = script_sound_index(reference);
    std::vector<AmbientSample> result;
    for (std::size_t i = 0; i < audio.tracks.size(); ++i) {
        const auto &track = audio.tracks[i];
        if (track.effect &&
            std::find(track.sounds.begin(), track.sounds.end(), sound) != track.sounds.end())
            result.push_back({track.label, audio.current(i)});
    }
    require(!result.empty(), "No bank samples found for this script sound ID");
    return result;
}
}
