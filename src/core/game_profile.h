#pragma once
#include "core/binary.h"
namespace studio {
enum class GameTarget { UltraMoon, UltraSun };
GameTarget detect_game_target(const std::filesystem::path &dump);
const char *game_target_name(GameTarget target);
const char *game_target_id(GameTarget target);
struct GameProfile {
    static constexpr const char *moon_field_archive = "romfs/a/0/8/3";
    static constexpr const char *sun_field_archive = "romfs/a/0/8/2";
    static constexpr const char *moon_field_menu_archive = "romfs/a/1/0/2";
    static constexpr const char *sun_field_menu_archive = "romfs/a/1/0/1";
    static bool is_field_archive(const std::filesystem::path &path) {
        return path == moon_field_archive || path == sun_field_archive;
    }
    static const char *field_archive(const std::filesystem::path &dump);
    static std::filesystem::path image_archive(const std::filesystem::path &dump,
                                               const std::filesystem::path &path);
};
}
