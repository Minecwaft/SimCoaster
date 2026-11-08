#include "simcoaster/ui_preferences.hpp"
#include "simcoaster/file_platform.hpp"
#include "simcoaster/save_schema.hpp"
#include "simcoaster/world_sound_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>

namespace simcoaster {
namespace {
struct DecodeFailure {
    std::size_t offset;
    std::string reason;
};

struct Reader {
    std::span<const std::byte> bytes;
    std::size_t position {};

    [[noreturn]] void fail(std::string reason) const {
        throw DecodeFailure { position, std::move(reason) };
    }

    std::span<const std::byte> take(std::size_t count) {
        if (count > bytes.size() - position) {
            fail("Truncated ThemeGlobalSaveInfo profile");
        }
        auto result = bytes.subspan(position, count);
        position += count;
        return result;
    }

    template <class T>
    T scalar() {
        constexpr auto width = save::value_width<T>;
        const auto raw = take(width);
        T value {};
        save::detail::decode(std::span<const std::byte, width> { raw.data(), width }, value);
        return value;
    }

    void marker(std::string_view expected) {
        const auto start = position;
        const auto actual = take(expected.size());
        if (!std::ranges::equal(actual, std::as_bytes(std::span { expected.data(), expected.size() }))) {
            throw DecodeFailure { start, "Expected profile section " + std::string { expected } };
        }
    }

    void records(std::int32_t count, std::size_t width, std::string_view description) {
        if (count < 0 || static_cast<std::size_t>(count) > (bytes.size() - position) / width) {
            fail("Invalid " + std::string { description } + " count");
        }
        (void)take(static_cast<std::size_t>(count) * width);
    }
};

std::string lower(std::string text) {
    for (auto& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch + ('a' - 'A'));
        }
    }
    return text;
}

std::expected<std::vector<std::byte>, UiPreferencesError> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return std::unexpected(UiPreferencesError { path, 0, "Cannot open preferences resource" });
    }
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return std::unexpected(UiPreferencesError { path, 0, "Cannot determine preferences resource size" });
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return std::unexpected(UiPreferencesError { path, 0, "Cannot read complete preferences resource" });
    }
    return bytes;
}

std::expected<UiPreferences, UiPreferencesError> native_defaults(const std::filesystem::path& root) {
    //459520 copies these SAM defaults before applying any profile. The source
    //global ConfigState has static zero initialization, including pair padding.
    const auto path = root / "Data/sound.sam";
    auto bytes = read_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    WorldSoundBalance sound {};
    const auto parsed = sam::parse_record<WorldSoundBalance, true>(sound, std::string_view { reinterpret_cast<const char*>(bytes->data()), bytes->size() });
    if (!parsed) {
        return std::unexpected(UiPreferencesError { path, parsed.error().offset, "Invalid native sound defaults SAM" });
    }
    UiPreferences options;
    const auto enabled = static_cast<std::uint8_t>(sound.info.sound_on != 0);
    options.sfx_volume = { enabled, {}, sound.volumes.sfx };
    options.music_volume = { enabled, {}, sound.volumes.music };
    options.speech_volume = { enabled, {}, sound.volumes.speech };
    options.movie_volume = { enabled, {}, sound.volumes.movie };
    options.sound_quality = sound.info.default_quality;
    return options;
}

std::optional<std::filesystem::path> paired_profile(const std::filesystem::path& root, const std::filesystem::path& save_path) {
    const auto extension = lower(save_path.extension().native_encoded_string());
    if (extension == ".gms" || extension == ".gmi") {
        return save_path;
    }
    if (extension != ".tpws" && extension != ".tpwi") {
        return std::nullopt;
    }
    //Native7451E0 uses Windows paths. libstdc++'s lexically_relative compares
    //spelling, so a different drive/directory case incorrectly lost the user's
    //profile and silently applied defaults. Compare whole path components with
    //Windows ordinal casing, without requiring the park itself to exist.
    auto prefix = root;
    auto source = save_path;
    prefix.make_preferred();
    source.make_preferred();
    auto part = source.begin();
    for (const auto& component : prefix) {
        //A normalized root ending in separator or"/.." has a final empty
        //component. It denotes the same directory, not another path level.
        if (component.empty()) {
            continue;
        }
        if (part == source.end()) {
            return std::nullopt;
        }
        const auto& a = component.native();
        const auto& b = part->native();
        if (a.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || b.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || !file_platform::ordinal_equal(a, b)) {
            return std::nullopt;
        }
        ++part;
    }
    std::vector<std::filesystem::path> parts(part, source.end());
    if (parts.size() != 5 || lower(parts[0].native_encoded_string()) != "save" || lower(parts[1].native_encoded_string()) != "users") {
        return std::nullopt;
    }
    //7451E0 uses the selected profile root, not the theme directory. Source
    //names may differ between saves; neither directory enumeration nor a
    //newest-file heuristic belongs to this named-save load contract.
    auto metadata = root / parts[0] / parts[1] / parts[2] / parts[4];
    metadata.replace_extension(extension == ".tpwi" ? ".GMI" : ".GMS");
    return metadata;
}

UiOptionVolume volume(Reader& input) {
    UiOptionVolume result;
    result.enabled = input.scalar<std::uint8_t>();
    const auto reserved = input.take(result.reserved.size());
    std::ranges::copy(reserved, result.reserved.begin());
    result.value = input.scalar<std::int32_t>();
    return result;
}

void decode_profile(UiPreferencesLoad& result) {
    Reader input { result.profile_bytes };
    result.saved_source_index = input.scalar<std::int32_t>();
    result.profile_version = input.scalar<std::uint32_t>();
    //74D110 requires version>=12. The called body/record/options/tail transfer
    //routines do not branch on that version; ordinary files have writer12.
    if (result.profile_version < 12) {
        input.fail("Unsupported ThemeGlobalSaveInfo version below12");
    }
    (void)input.scalar<std::uint32_t>();           //golden-ticket balance
    (void)input.take(8);                           //global park awards
    input.records(256, 4 * 4, "CDK registration"); //74B1F0: four raw DWORDs
    (void)input.scalar<std::uint32_t>();           //CDK count
    (void)input.scalar<std::uint32_t>();           //CDK iteration index
    (void)input.take(12);                          //language/startup/event latch plus nine Advisor gates
    (void)input.take(3 * 4);                       //mission, promotion and the third manager scalar
    (void)input.take(15);                          //ability activation bytes; excludes following padding
    (void)input.take(2);                           //post-Balance latches
    input.records(5, 48, "mission objective");
    input.marker("TATS");
    (void)input.take(15 * 4); //lobby-area availability
    input.marker("AMTA");
    const auto theme_count = input.scalar<std::int32_t>();
    //74FEF0's fixed body excludes all native pointers/padding: six bytes,
    //four validity/value pairs, eight award/score pairs,33 interleaved UTF16
    //pairs, then the17-byte availability/ordinal/progression tail.
    constexpr std::size_t theme_width = 6 + 4 * (1 + 4) + 8 * (1 + 4) + 33 * (2 + 2) + 17;
    static_assert(theme_width == 215);
    if (theme_count < 0 || static_cast<std::size_t>(theme_count) > (input.bytes.size() - input.position) / (4 + theme_width)) {
        input.fail("Invalid theme-map count");
    }
    std::set<std::string> unique_sources;
    for (std::int32_t theme = 0; theme < theme_count; ++theme) {
        const auto length = input.scalar<std::uint32_t>();
        const auto raw = input.take(length);
        std::string source { reinterpret_cast<const char*>(raw.data()), raw.size() };
        if (source.find('\0') != source.npos) {
            input.fail("Embedded NUL in theme source key");
        }
        if (!unique_sources.insert(source).second) {
            input.fail("Duplicate theme source key");
        }
        result.theme_sources.push_back(std::move(source));
        (void)input.take(theme_width);
    }
    input.marker("MEHT");
    result.option_offset = input.position;
    auto& options = result.options;
    options.sfx_volume = volume(input);
    options.music_volume = volume(input);
    options.speech_volume = volume(input);
    options.movie_volume = volume(input);
    options.advisor_on = input.scalar<std::uint8_t>();
    options.tutorial_on = input.scalar<std::uint8_t>();
    options.tooltips_on = input.scalar<std::uint8_t>();
    options.confirm_delete_on = input.scalar<std::uint8_t>();
    options.rmb_scroll_on = input.scalar<std::uint8_t>();
    options.rmb_cancel_on = input.scalar<std::uint8_t>();
    options.isometric_on = input.scalar<std::uint8_t>();
    options.animate_menus = input.scalar<std::uint8_t>();
    options.game_speed_percent = input.scalar<std::int16_t>();
    options.totd = input.scalar<std::uint8_t>();
    options.minimum_zoom = input.scalar<float>();
    options.maximum_zoom = input.scalar<float>();
    options.fps_clip = input.scalar<float>();
    options.music_enabled = input.scalar<std::int32_t>();
    if (input.position - result.option_offset != 59) {
        input.fail("Internal native option width mismatch");
    }
    if (!std::isfinite(options.minimum_zoom) || !std::isfinite(options.maximum_zoom) || !std::isfinite(options.fps_clip)) {
        input.fail("Non-finite saved camera/frame options");
    }

    //Walk the remaining native framing to EOF so an options-shaped prefix of
    //a truncated/corrupt profile cannot silently become valid preferences.
    //Payloads stay opaque in profile_bytes; this does not restore those owners.
    (void)input.take(2); //GMI suffix and end-of-game bytes
    (void)input.take(4); //first-hour time
    input.records(644, 20, "Advisor history");
    (void)input.take(4 + 92); //TipOfTheDay count and seen bytes,592720
    input.marker("DTOT");
    input.records(3, 5 * 4 + 2 * 8,
                  "shares report");       //present flag does not shorten a row
    (void)input.take(2 * 4 + 31 * 2 + 4); //ownership, company title and share value
    input.marker("RAHS");
    const auto spam_count = input.scalar<std::int32_t>();
    input.records(spam_count, 1, "spam scheduler entry");
    (void)input.take(1 + 3 * 4); //special text, year/day/sends counters
    input.marker("MAPS");
    const auto ambient_count = input.scalar<std::int32_t>();
    input.records(ambient_count, 16, "ambient sound handle");
    if (input.position != input.bytes.size()) {
        input.fail("Trailing bytes after native profile framing");
    }
}
} //namespace

std::expected<UiPreferences, UiPreferencesError> load_native_ui_preferences(const std::filesystem::path& root) {
    return native_defaults(root);
}

std::expected<UiPreferencesLoad, UiPreferencesError> load_ui_preferences(const std::filesystem::path& game_root, const std::filesystem::path& park_save_path) {
    std::error_code error;
    const auto root = std::filesystem::absolute(game_root, error).lexically_normal();
    if (error) {
        return std::unexpected(UiPreferencesError { game_root, 0, "Cannot resolve game root: " + error.message() });
    }
    const auto save_path = park_save_path.is_absolute() ? park_save_path.lexically_normal() : (root / park_save_path).lexically_normal();
    auto defaults = native_defaults(root);
    if (!defaults) {
        return std::unexpected(defaults.error());
    }
    UiPreferencesLoad result;
    result.options = *defaults;
    const auto metadata = paired_profile(root, save_path);
    if (!metadata) {
        return result;
    }
    result.profile_path = *metadata;
    const auto exists = std::filesystem::exists(*metadata, error);
    if (error) {
        return std::unexpected(UiPreferencesError { *metadata, 0, "Cannot inspect profile: " + error.message() });
    }
    if (!exists) {
        result.origin = UiPreferencesOrigin::native_defaults_profile_absent;
        return result;
    }
    auto bytes = read_file(*metadata);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    result.profile_bytes = std::move(*bytes);
    try {
        decode_profile(result);
    } catch (const DecodeFailure& failure) {
        return std::unexpected(UiPreferencesError { *metadata, failure.offset, failure.reason });
    }
    result.origin = UiPreferencesOrigin::paired_profile;
    return result;
}
} //namespace simcoaster
