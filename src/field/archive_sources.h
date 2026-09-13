#pragma once
#include "field/area.h"
namespace studio {
struct ArchiveSources {
    std::filesystem::path field, terrain, pokemon;
    bool operator==(const ArchiveSources &) const = default;
    std::filesystem::path resolve(const std::filesystem::path &dump,
                                  const std::filesystem::path &archive) const {
        const auto *selected = GameProfile::is_field_archive(archive)      ? &field
                               : archive == TargetProfile::terrain_archive ? &terrain
                               : archive == TargetProfile::pokemon_archive ? &pokemon
                                                                           : nullptr;
        return selected && !selected->empty()
                   ? *selected
                   : dump / (GameProfile::is_field_archive(archive)
                                 ? std::filesystem::path(GameProfile::field_archive(dump))
                                 : archive);
    }
};
}
