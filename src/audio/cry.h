#pragma once
#include "audio/music.h"
#include "formats/archive.h"
#include <array>
namespace studio {
struct CryProfile {
    static constexpr const char *archive = "romfs/a/0/7/3";
    static constexpr unsigned table_offset = 0x4d45e8, table_rows = 1067, species_max = 807,
                              wave_limit = 40000;
};
inline constexpr const char *cry_contexts[] = {"Normal", "Battle", "Fainting", "Starter selection"};
struct CryBinding {
    unsigned sequence = 0, wave_archive = 0;
};
class CryLibrary {
  public:
    explicit CryLibrary(const std::filesystem::path &dump);
    CryBinding binding(unsigned species, unsigned form, unsigned context) const;
    std::vector<std::string> uses(unsigned wave_archive) const;
    Archive archive;

  private:
    std::vector<std::array<unsigned, 5>> rows_;
    unsigned forms(unsigned species) const;
};
MusicSamples decode_cry(View archive);
Bytes encode_cry(View original, const MusicSamples &mono);
MusicSamples import_cry_wav(View bytes);
MusicSamples prepare_cry(const MusicSamples &source, float start, float end, float gain);
}
