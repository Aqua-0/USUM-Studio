#pragma once
#include "assets/clothing_profile.h"
#include "assets/model_library.h"
namespace studio {
using ClothingColors = std::array<std::vector<std::array<float, 3>>, 7>;
ClothingColors decode_clothing_palette(View bytes);
Bytes replace_clothing_color(View bytes, unsigned section, unsigned index,
                             const std::array<float, 3> &rgb);
struct ClothingPalette {
    std::filesystem::path source;
    Bytes original, current, saved;
    std::vector<Bytes> history;
    std::size_t cursor = 0;
    void load(const std::filesystem::path &path);
    void commit();
    void undo();
    void redo();
    void reset();
    bool dirty() const {
        return current != saved;
    }
    void export_archive(const std::filesystem::path &output) const;
};
struct ClothingSelection {
    std::filesystem::path color_archive;
    Bytes palette;
    unsigned profile = 0, skin = 1, hair = 0, eyes = 4, lip = 0;
    std::array<int, ClothingProfile::parts> items = ClothingProfile::defaults[0];
    std::array<std::filesystem::path, ClothingProfile::parts> archives;
};
struct ClothingItem {
    unsigned item = 0;
    std::string name;
};
struct ClothingCatalog {
    std::array<std::vector<ClothingItem>, ClothingProfile::parts> parts;
    ClothingColors colors;
    std::vector<std::string> diagnostics;
};
std::filesystem::path clothing_archive(const std::filesystem::path &dump,
                                       const ClothingSelection &selection, unsigned part);
ClothingCatalog load_clothing_catalog(const std::filesystem::path &dump,
                                      const ClothingSelection &selection,
                                      std::atomic_bool *cancel = nullptr);
ModelDocument load_clothing(const std::filesystem::path &dump, const ClothingSelection &selection,
                            int isolated_part = -1, std::atomic_bool *cancel = nullptr);
}
