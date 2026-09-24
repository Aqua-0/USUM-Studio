#include "audio/ambient_sound.h"
#include "audio/ambient_sound_profile.h"
#include <algorithm>
namespace studio {
unsigned ambient_sound_index(unsigned reference) {
    auto group = reference >> 16, index = reference & 65535;
    for (auto range : ambient_sound_ranges)
        if (group == range.group && index >= range.first && index - range.first < range.count)
            return range.sound + index - range.first;
    throw std::runtime_error("No verified ambient sound mapping for this sound ID");
}
std::vector<AmbientSample> ambient_sound_samples(const AudioDocument &audio, unsigned reference) {
    auto sound = ambient_sound_index(reference);
    std::vector<AmbientSample> result;
    for (std::size_t i = 0; i < audio.tracks.size(); ++i) {
        auto &track = audio.tracks[i];
        if (track.effect &&
            std::find(track.sounds.begin(), track.sounds.end(), sound) != track.sounds.end())
            result.push_back(
                {"Bank sample " + std::to_string(result.size() + 1), audio.current(i)});
    }
    require(!result.empty(), "No embedded bank samples were resolved for sound " +
                                 std::to_string(reference) + " (archive sound " +
                                 std::to_string(sound) + ")");
    return result;
}
}
