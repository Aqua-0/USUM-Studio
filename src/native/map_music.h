#pragma once
#include "audio/music.h"
#include "scene/environment.h"
#include <SDL3/SDL.h>
#include <future>
namespace studio {
class MapMusic {
  public:
    ~MapMusic();
    void update(bool maps_active);
    void draw(const Environment *scene, const std::filesystem::path &dump, int zone);

  private:
    void stop();
    void play();
    void select();
    const Environment *scene_ = nullptr;
    std::filesystem::path dump_;
    std::map<unsigned, MusicTrack> catalog_;
    Bytes zones_;
    std::vector<MapLocation> locations_;
    int zone_ = -1;
    bool night_ = false, playing_ = false, loop_ = true;
    float volume_ = .5f;
    std::string error_, track_;
    float track_volume_ = 1;
    std::future<MusicSamples> pending_;
    MusicSamples samples_;
    SDL_AudioStream *audio_ = nullptr;
    std::size_t cursor_ = 0;
    bool discard_ = false;
};
}
