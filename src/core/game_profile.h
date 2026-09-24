#pragma once
#include "core/binary.h"
#include <array>
namespace studio {
enum class GameTarget { UltraMoon, UltraSun };
GameTarget detect_game_target(const std::filesystem::path &dump);
const char *game_target_name(GameTarget target);
const char *game_target_id(GameTarget target);
struct DialogueLanguage {
    const char *name, *archive;
};
struct GameProfile {
    static constexpr unsigned dialogue_fallback_language = 2;
    static constexpr std::array<DialogueLanguage, 10> dialogue_languages{{
        {"Japanese (kana)", "romfs/a/0/4/0"},
        {"Japanese (kanji)", "romfs/a/0/4/1"},
        {"English", "romfs/a/0/4/2"},
        {"French", "romfs/a/0/4/3"},
        {"Italian", "romfs/a/0/4/4"},
        {"German", "romfs/a/0/4/5"},
        {"Spanish", "romfs/a/0/4/6"},
        {"Korean", "romfs/a/0/4/7"},
        {"Chinese (Simplified)", "romfs/a/0/4/8"},
        {"Chinese (Traditional)", "romfs/a/0/4/9"},
    }};
    static constexpr const char *personal_archive = "romfs/a/0/1/7";
    static constexpr const char *moon_field_archive = "romfs/a/0/8/3";
    static constexpr const char *sun_field_archive = "romfs/a/0/8/2";
    static constexpr const char *moon_field_menu_archive = "romfs/a/1/0/2";
    static constexpr const char *sun_field_menu_archive = "romfs/a/1/0/1";
    static bool is_field_archive(const std::filesystem::path &path) {
        return path == moon_field_archive || path == sun_field_archive;
    }
    static const char *external_resource_name(const std::filesystem::path &path);
    static const char *field_archive(const std::filesystem::path &dump);
    static std::filesystem::path image_archive(const std::filesystem::path &dump,
                                               const std::filesystem::path &path);
};
}
