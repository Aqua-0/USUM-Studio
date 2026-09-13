#pragma once
#include "audio/music.h"
#include "audio/sound_wave.h"
#include <memory>
namespace studio {
struct AudioSource {
    std::string file;
    std::vector<unsigned> sounds;
    std::string label, key;
    std::size_t offset = 0, size = 0;
    bool effect = false;
};
class AudioDocument {
  public:
    explicit AudioDocument(const std::filesystem::path &source);
    std::filesystem::path dump;
    std::vector<AudioSource> tracks;
    Bytes current(std::size_t index) const;
    void replace(std::size_t index, Bytes bytes);
    void reset(std::size_t index);
    bool undo();
    bool redo();
    void discard();
    void mark_saved() {
        saved_ = edits_;
    }
    bool dirty() const {
        return edits_ != saved_;
    }
    std::size_t size() const {
        return edits_.size();
    }
    bool edited(std::size_t index) const {
        return edits_.contains(index);
    }
    void save(const std::filesystem::path &path);
    void load(const std::filesystem::path &path);
    void export_to(const std::filesystem::path &folder) const;

  private:
    struct Edit {
        std::string hash;
        Bytes bytes;
        bool operator==(const Edit &) const = default;
    };
    using State = std::map<std::size_t, Edit>;
    State edits_, saved_;
    std::vector<State> undo_, redo_;
    Bytes original(std::size_t index) const;
    std::shared_ptr<const Bytes> effects_;
    std::string effects_hash_;
};
void require_audio_output(const std::filesystem::path &dump, const std::filesystem::path &output);
Bytes export_audio_wav(const MusicSamples &samples);
}
