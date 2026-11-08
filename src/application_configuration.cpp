#include "simcoaster/application_configuration.hpp"
#include "simcoaster/save_schema.hpp"
#include <bit>
#include <fstream>
#include <stdexcept>

namespace simcoaster {
namespace {
struct MovieWire {
    [[=save::Field{ "enabled", 0 }]] std::uint8_t enabled{};
    [[=save::Field{ "reserved", 1 }]] std::array<std::byte, 3> reserved{};
    [[=save::Field{ "value", 2 }]] std::int32_t value{};
};

struct ConfigurationWire {
    [[=save::Field{ "version", 0 }]] std::uint32_t version{};
    [[=save::Field{ "renderer", 1 }]] std::uint32_t renderer{};
    [[=save::Field{ "resolution", 2 }]] std::uint32_t resolution{};
    [[=save::Field{ "hardware_profile", 3 }]] std::uint32_t hardware_profile{};
    [[=save::Field{ "video_device", 4 }]] std::uint32_t video_device{};
    [[=save::Field{ "MovieVolume", 5 }]] MovieWire movie;
    [[=save::Field{ "SoundQuality", 6 }]] std::int32_t sound_quality{};
};

static_assert(save::record_width<ConfigurationWire> == 32);
} //namespace

std::expected<ApplicationConfiguration, ApplicationConfigurationError>
decode_application_configuration(std::span<const std::byte> bytes) {
    if (bytes.size() != save::record_width<ConfigurationWire>)
        return std::unexpected(
            ApplicationConfigurationError{ std::min(bytes.size(), save::record_width<ConfigurationWire>),
                                           "Config.tcf must contain its complete32-byte native record" });
    ConfigurationWire wire;
    save::detail::decode(std::span<const std::byte, 32>{ bytes.data(), 32 }, wire);
    //45B310 checks nonzero, not equality with this writer's version1.
    if (!wire.version)
        return std::unexpected(ApplicationConfigurationError{ 0, "Zero Config.tcf version" });
    return ApplicationConfiguration{ wire.version,      wire.renderer,
                                     wire.resolution,   wire.hardware_profile,
                                     wire.video_device, { wire.movie.enabled, wire.movie.reserved, wire.movie.value },
                                     wire.sound_quality };
}

std::array<std::byte, 32> encode_application_configuration(const ApplicationConfiguration& settings) {
    return save::detail::encode(ConfigurationWire{
        1,
        settings.renderer_backend,
        settings.resolution,
        settings.hardware_profile,
        settings.video_device,
        { settings.movie_volume.enabled, settings.movie_volume.reserved, settings.movie_volume.value },
        settings.sound_quality });
}

std::expected<ApplicationHardwareProfile, ApplicationConfigurationError>
load_application_hardware_profile(const std::filesystem::path& root, std::uint32_t index) {
    constexpr std::array names{ "low.sam", "med.sam", "high.sam" };
    if (index >= names.size())
        return std::unexpected(
            ApplicationConfigurationError{ 0, "Unknown native hardware profile " + std::to_string(index) });
    const auto path = root / "Data" / names[index];
    std::ifstream input{ path, std::ios::binary | std::ios::ate };
    if (!input)
        return std::unexpected(
            ApplicationConfigurationError{ 0, "Cannot open hardware profile " + path.native_encoded_string() });
    const auto length = input.tellg();
    if (length < 0)
        return std::unexpected(
            ApplicationConfigurationError{ 0, "Cannot size hardware profile " + path.native_encoded_string() });
    std::string text(static_cast<std::size_t>(length), '\0');
    input.seekg(0);
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input)
        return std::unexpected(
            ApplicationConfigurationError{ 0,
                                           "Cannot read complete hardware profile " + path.native_encoded_string() });
    ApplicationHardwareProfile profile;
    const auto parsed = sam::parse_record(profile, text);
    if (!parsed)
        return std::unexpected(
            ApplicationConfigurationError{ parsed.error().offset,
                                           "Invalid hardware profile " + path.native_encoded_string() });
    profile.profile_index = index;
    return profile;
}

std::array<std::uint32_t, 2> application_resolution(std::uint32_t selector) {
    match (selector) {
        case 1 => return { 640, 480 };
        case 2 => return { 800, 600 };
        case 3 => return { 1024, 768 };
        case _ => throw std::out_of_range("Unknown native Options resolution selector");
    }
}

std::uint16_t application_movie_volume(const UiOptionVolume& volume) {
    if (!volume.enabled)
        return 0;
    //Define the original signed multiply's wrap without C++ signed overflow.
    const auto product = static_cast<std::uint32_t>(volume.value) * 1023u;
    const auto scaled = std::bit_cast<std::int32_t>(product) / 100;
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(scaled) & 1023u);
}
} //namespace simcoaster
