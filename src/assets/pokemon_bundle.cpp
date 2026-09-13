#include "assets/pokemon_bundle.h"
#include "formats/compression.h"
#include "core/resource_source.h"
#include "core/digest.h"
#include <algorithm>
#include <sstream>
namespace studio {
unsigned pokemon_species_rows(const Archive &archive) {
    require(archive.size() > 1 && (archive.size() - 1) % TargetProfile::pokemon_stride == 0,
            "Unsupported Pokemon archive layout");
    auto count = (archive.size() - 1) / TargetProfile::pokemon_stride;
    auto table = archive.decoded(0);
    require(table.size() >= count * 2 && (table.size() - count * 2) % 4 == 0,
            "Invalid Pokemon management table");
    return unsigned((table.size() - count * 2) / 4);
}
PokemonBundle clone_pokemon_bundle(const Archive &archive, const PokemonEntry &selected,
                                   unsigned species, bool add_form) {
    auto table = archive.decoded(0);
    auto rows = pokemon_species_rows(archive);
    auto count = (archive.size() - 1) / TargetProfile::pokemon_stride;
    auto catalog = decode_pokemon_catalog(table, archive.size(), {});
    auto found = std::find_if(catalog.begin(), catalog.end(), [&](auto &e) {
        return e.species == selected.species && e.form == selected.form &&
               e.female == selected.female;
    });
    require(found != catalog.end(), "Donor Pokemon is missing");
    auto donor = *found;
    PokemonBundle result;
    result.species = species;
    Bytes management(table.begin(), table.begin() + rows * 4),
        flags(table.begin() + rows * 4, table.end());
    std::size_t first = count;
    if (add_form) {
        require(species > 0 && species <= rows, "Choose an existing species for the new form");
        auto row = (species - 1) * 4;
        auto source = u16(management, row);
        unsigned variants = management[row + 2];
        require((management[row + 3] & 1) && variants > 0 && variants < 255,
                "This species cannot receive another form");
        for (unsigned variant = 0; variant < variants; ++variant) {
            for (unsigned slot = 0; slot < TargetProfile::pokemon_stride; ++slot)
                result.members[1 + (count + variant) * TargetProfile::pokemon_stride + slot] =
                    archive.raw(1 + (source + variant) * TargetProfile::pokemon_stride + slot);
            flags.push_back(table[rows * 4 + (source + variant) * 2]);
            flags.push_back(table[rows * 4 + (source + variant) * 2 + 1]);
        }
        count += variants;
        result.form = variants - unsigned(bool(management[row + 3] & 2));
        management[row + 2] = std::uint8_t(variants + 1);
        management[row + 3] |= 4;
    } else {
        require(species == rows + 1, "New species IDs must follow the last management row");
        management.resize(management.size() + 4);
        management[(species - 1) * 4 + 2] = 1;
        management[(species - 1) * 4 + 3] = 1;
    }
    require(first <= 65535 && 1 + (count + 1) * TargetProfile::pokemon_stride <= 65535,
            "Pokemon bundle exceeds the archive's native entry capacity");
    put16(management, (species - 1) * 4, std::uint16_t(first));
    result.data_index = count;
    flags.push_back(donor.flags & ~7u);
    flags.push_back(0);
    append(management, flags);
    auto raw = archive.raw(0);
    result.members[0] =
        !raw.empty() && (raw[0] == 0x10 || raw[0] == 0x11) ? compress(management) : management;
    for (unsigned slot = 0; slot < TargetProfile::pokemon_stride; ++slot) {
        auto member = slot == 0   ? donor.model_member
                      : slot <= 3 ? donor.texture_member + slot
                      : slot <= 7 ? donor.motion_member + slot
                                  : 1 + donor.data_index * TargetProfile::pokemon_stride + slot;
        result.members[1 + count * TargetProfile::pokemon_stride + slot] = archive.raw(member);
    }
    auto checked =
        decode_pokemon_catalog(management, 1 + (count + 1) * TargetProfile::pokemon_stride, {});
    auto entry = std::find_if(checked.begin(), checked.end(), [&](auto &e) {
        return e.species == species && e.form == result.form && !e.female;
    });
    require(entry != checked.end() && entry->data_index == count &&
                entry->model_member == 1 + count * TargetProfile::pokemon_stride &&
                entry->texture_member == entry->model_member &&
                entry->motion_member == entry->model_member,
            "New bundle management did not round-trip");
    return result;
}
void save_pokemon_bundle(const Archive &source, const PokemonBundle &bundle,
                         const std::filesystem::path &directory) {
    require(!std::filesystem::exists(directory), "Choose a new bundle folder");
    std::filesystem::create_directories(directory);
    try {
        {
            MemberCapture capture(directory / "members");
            std::map<std::pair<std::size_t, unsigned>, Bytes> members;
            for (auto &[index, bytes] : bundle.members)
                members[{index, 0}] = bytes;
            source.export_appended(directory / "pokemon.garc", members);
        }
        Archive check(directory / "pokemon.garc");
        for (auto &[member, raw] : bundle.members)
            require(check.raw(member) == raw, "New bundle did not round-trip");
        std::ostringstream info;
        info << "USUMSTUDIO_POKEMON_BUNDLE 1\nspecies " << bundle.species << "\nform "
             << bundle.form << "\ndata_index " << bundle.data_index << "\narchive pokemon.garc\n";
        auto text = info.str();
        write_file_atomic(directory / "bundle.usum-pokemon",
                          View(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        throw;
    }
}
}
