#include "core/game_profile.h"
#include "core/resource_source.h"
#include <fstream>
#include <optional>
namespace studio {
namespace {
std::optional<GameTarget> header_target(const std::filesystem::path &dump) {
    for (auto name : {"HeaderNCCH0.bin", "HeaderNCCH.bin"}) {
        std::ifstream file(resource_source(dump / name).original, std::ios::binary);
        if (!file)
            continue;
        Bytes header(512);
        file.read(reinterpret_cast<char *>(header.data()), 512);
        if (file.gcount() != 512 || text(slice(header, 256, 4)) != "NCCH")
            continue;
        auto product = text(slice(header, 336, 9));
        if (product == "CTR-P-A2A")
            return GameTarget::UltraSun;
        if (product == "CTR-P-A2B")
            return GameTarget::UltraMoon;
        require(product != "CTR-P-BND" && product != "CTR-P-BNE",
                "Sun and Moon dumps are not supported yet. Choose Ultra Sun or Ultra Moon.");
    }
    return {};
}
bool populated(const std::filesystem::path &path) {
    auto source = resource_source(path);
    return !source.members.empty() || (std::filesystem::is_regular_file(source.original) &&
                                       std::filesystem::file_size(source.original) > 0);
}
}
GameTarget detect_game_target(const std::filesystem::path &dump) {
    if (auto target = header_target(dump))
        return *target;
    const bool sun = populated(dump / GameProfile::sun_field_archive),
               moon = populated(dump / GameProfile::moon_field_archive);
    require(!sun || !moon, "Both field archives are populated. Retain the extracted game header to "
                           "identify the target.");
    return sun ? GameTarget::UltraSun : GameTarget::UltraMoon;
}
const char *game_target_name(GameTarget target) {
    return target == GameTarget::UltraSun ? "Ultra Sun" : "Ultra Moon";
}
const char *game_target_id(GameTarget target) {
    return target == GameTarget::UltraSun ? "ultra-sun" : "ultra-moon";
}
const char *GameProfile::external_resource_name(const std::filesystem::path &path) {
    const auto name = path.generic_string();
    if (name == personal_archive)
        return "Pokémon personal data";
    if (name == "romfs/a/0/1/1")
        return "Moves";
    if (name == "romfs/a/0/1/2")
        return "Egg moves";
    if (name == "romfs/a/0/1/3")
        return "Learnsets";
    if (name == "romfs/a/0/1/4")
        return "Evolutions";
    if (name == "romfs/a/0/3/0")
        return "Game text";
    if (name == "romfs/a/1/0/5")
        return "Trainer classes";
    if (name == "romfs/a/1/0/6")
        return "Trainer settings";
    if (name == "romfs/a/1/0/7")
        return "Trainer teams";
    return "Game resource";
}
const char *GameProfile::field_archive(const std::filesystem::path &dump) {
    return detect_game_target(dump) == GameTarget::UltraSun ? sun_field_archive
                                                            : moon_field_archive;
}
std::filesystem::path GameProfile::image_archive(const std::filesystem::path &dump,
                                                 const std::filesystem::path &path) {
    return path == moon_field_menu_archive && detect_game_target(dump) == GameTarget::UltraSun
               ? std::filesystem::path(sun_field_menu_archive)
               : path;
}
}
