#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace simcoaster {
struct UiOptionVolume {
    std::uint8_t enabled {};
    std::array<std::byte, 3> reserved {};
    std::int32_t value {};
};

//45A130's exact semantic option set. Native byte toggles are retained as
//bytes; consumers test nonzero. Volume values/default enable flags come from
//Data/sound.sam, as459520 does, rather than guessed host mixer defaults.
struct UiPreferences {
    UiOptionVolume sfx_volume, music_volume, speech_volume, movie_volume;
    std::uint8_t advisor_on = 1, tutorial_on = 1, tooltips_on = 1, confirm_delete_on = 1, rmb_scroll_on = 0, rmb_cancel_on = 1, isometric_on = 0, animate_menus = 1;
    std::int16_t game_speed_percent = 100;
    std::uint8_t totd = 1;
    float minimum_zoom = 40, maximum_zoom = 300, fps_clip = 10;
    std::int32_t music_enabled = 1;
    //Initialized by459520 but omitted from45A130's59-byte wire block.
    std::uint8_t allow_skirts = 1;
    std::int32_t sound_quality {};
};
enum class UiPreferencesOrigin {
    paired_profile,
    native_defaults_profile_absent,
    native_defaults_unassociated_park
};

struct UiPreferencesLoad {
    UiPreferences options;
    UiPreferencesOrigin origin = UiPreferencesOrigin::native_defaults_unassociated_park;
    std::filesystem::path profile_path;
    std::int32_t saved_source_index {};
    std::uint32_t profile_version {};
    std::size_t option_offset {};
    //Other profile state remains byte-exact and owned. Importing preferences
    //does not pretend to restore missions, theme progression or shares state.
    std::vector<std::byte> profile_bytes;
    std::vector<std::string> theme_sources;
};

struct UiPreferencesError {
    std::filesystem::path path;
    std::size_t offset {};
    std::string reason;
};

//Native7451E0 pairing: save/users/<user>/<theme>/<name>.TPWS maps to
//save/users/<user>/<name>.GMS; TPWI maps toGMI. Missing paired metadata uses
//initialized native defaults. Found but malformed/unreadable data is an error;
//there is no search for a different user's or newer unrelated profile.
std::expected<UiPreferencesLoad, UiPreferencesError> load_ui_preferences(const std::filesystem::path& game_root, const std::filesystem::path& park_save_path);
std::expected<UiPreferences, UiPreferencesError> load_native_ui_preferences(const std::filesystem::path& game_root);
} //namespace simcoaster
