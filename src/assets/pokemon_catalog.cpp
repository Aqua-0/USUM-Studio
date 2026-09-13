#include "assets/pokemon_catalog.h"
#include "field/map_catalog.h"
namespace studio {
std::vector<PokemonEntry> decode_pokemon_catalog(View b, std::size_t members,
                                                 const std::vector<std::string> &names) {
    require(members > 1 && (members - 1) % TargetProfile::pokemon_stride == 0,
            "Unsupported Pokemon archive layout");
    auto data_count = (members - 1) / TargetProfile::pokemon_stride;
    require(b.size() >= data_count * 2 && (b.size() - data_count * 2) % 4 == 0,
            "Invalid Pokemon management table");
    auto species_count = (b.size() - data_count * 2) / 4;
    require(species_count > 0 && species_count <= 4096, "Invalid Pokemon species count");
    std::vector<PokemonEntry> result;
    for (unsigned species = 1; species <= species_count; ++species) {
        auto p = (species - 1) * 4;
        unsigned first = u16(b, p), count = b[p + 2], flags = b[p + 3];
        require(first + count <= data_count, "Pokemon model range exceeds archive");
        if (!(flags & 1))
            continue;
        for (unsigned variant = 0; variant < count; ++variant) {
            PokemonEntry e;
            e.species = species;
            e.data_index = first + variant;
            e.female = (flags & 2) && variant == 1;
            e.form = variant == 0 || e.female ? 0 : variant - unsigned(bool(flags & 2));
            e.name = species < names.size() && !names[species].empty()
                         ? names[species]
                         : "Species " + std::to_string(species);
            e.label = std::to_string(species) + " - " + e.name +
                      (e.female ? " (female)"
                       : e.form ? " (form " + std::to_string(e.form) + ")"
                                : "");
            auto f = species_count * 4 + e.data_index * 2;
            e.flags = b[f];
            unsigned shared_form = b[f + 1],
                     relative =
                         (flags & 4) && shared_form ? shared_form + unsigned(bool(flags & 2)) : 0;
            require(relative < count, "Invalid shared Pokemon form");
            unsigned shared = first + relative;
            e.model_member =
                1 + TargetProfile::pokemon_stride * ((e.flags & 4) ? shared : e.data_index);
            e.texture_member =
                1 + TargetProfile::pokemon_stride * ((e.flags & 2) ? shared : e.data_index);
            e.motion_member =
                1 + TargetProfile::pokemon_stride * ((e.flags & 1) ? shared : e.data_index);
            auto override = TargetProfile::pokemon_motion_override;
            if (species == override[0] && e.form == override[1]) {
                auto variant = override[2] + unsigned(bool(flags & 2));
                require(variant < count, "Missing motion-sharing variant");
                e.motion_member = 1 + TargetProfile::pokemon_stride * (first + variant);
            }
            result.push_back(std::move(e));
        }
    }
    return result;
}
std::vector<PokemonEntry> load_pokemon_catalog(const std::filesystem::path &dump,
                                               const ArchiveSources &archives) {
    Archive archive(archives.resolve(dump, TargetProfile::pokemon_archive));
    std::vector<std::string> names;
    try {
        names = decode_location_text(Archive(dump / TargetProfile::location_text_archive)
                                         .decoded(TargetProfile::pokemon_names_member));
    } catch (const std::exception &) {
    }
    return decode_pokemon_catalog(archive.decoded(0), archive.size(), names);
}
}
