#pragma once
#include "save_schema.hpp"
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace simcoaster {
enum class SaveLanguage : std::uint8_t {
    english,
    spanish,
    italian,
    swedish,
    german,
    french,
    japanese
};
enum class SaveLoadMode : std::int32_t {
    header_only = 0,
    recording_only = 1,
    full = 2
};
enum class SaveContainerFailure {
    io,
    short_header,
    invalid_stamp,
    stamp_mismatch,
    future_version,
    invalid_language,
    locale_mismatch,
    missing_definition,
    signature_mismatch,
    short_payload,
    bad_magic,
    empty_payload,
    invalid_window,
    invalid_compressed_stream,
    size_mismatch,
    too_large,
};

struct SaveContainerError {
    SaveContainerFailure reason;
    std::size_t offset;
};

std::string_view save_container_failure_name(SaveContainerFailure);

//These annotations describe wire order, not serialized name headers. The
//native record's unused410-byte memory gap has no place in this host value.
struct SavePublicationHeader {
    [[=save::Field { "unknown_words_0", 0 }]] std::array<std::uint16_t, 16> unknown_words_0 {};
    [[=save::Field { "unknown_words_1", 1 }]] std::array<std::uint16_t, 128> unknown_words_1 {};
    [[=save::Field { "unknown_dwords", 2 }]] std::array<std::uint32_t, 3> unknown_dwords {};
    [[=save::Field { "unknown_words_2", 3 }]] std::array<std::uint16_t, 16> unknown_words_2 {};
    [[=save::Field { "unknown_words_3", 4 }]] std::array<std::uint16_t, 16> unknown_words_3 {};
    [[=save::Field { "strAuthorEmail", 5 }]] std::array<char16_t, 50> author_email {};
    [[=save::Field { "strParkDescription", 6 }]] std::array<char16_t, 50> park_description {};
    [[=save::Field { "wParkDatePublished_d", 7 }]] std::uint16_t published_day {};
    [[=save::Field { "wParkDatePublished_m", 8 }]] std::uint16_t published_month {};
    [[=save::Field { "wParkDatePublished_y", 9 }]] std::uint16_t published_year {};
};

static_assert(save::record_width<SavePublicationHeader> == 570);

struct SaveContentDefinition {
    std::uint32_t downloaded_ride_number;
    bool is_preview_ride;
};

using SaveDefinitionMask = std::array<std::uint8_t, 32>;
SaveDefinitionMask make_save_definition_mask(std::span<const SaveContentDefinition>, bool exclude_preview);
bool validate_save_definition_mask_compatibility(const SaveDefinitionMask& saved, const SaveDefinitionMask& available);

struct ParkSaveHeader {
    std::int32_t version = 500;
    std::optional<std::string> version_stamp;
    SaveLanguage language = SaveLanguage::english;
    SaveDefinitionMask definitions {};
    std::optional<SavePublicationHeader> publication;
};

struct SaveCompatibility {
    //Missing local Ver.txt deliberately bypasses the native stamp comparison.
    std::optional<std::string_view> local_version_stamp;
    std::span<const SaveContentDefinition> definitions;
    SaveLoadMode mode = SaveLoadMode::full;
};

struct ParkSaveHeaderResult {
    ParkSaveHeader header;
    std::size_t payload_offset;
};

struct ParkSaveContainer {
    ParkSaveHeader header;
    std::vector<std::byte> sections;
};

struct ParkSaveBodyPrefix {
    std::uint32_t loaded_published_park;
    std::span<const std::byte> action_recording, world_and_later_sections;
};

//This layer validates the outer file and decodes the complete section buffer.
//Nonzero modes leave subsystem restoration to the caller. Prefix parsing
//returns borrowed spans at the action-recording/World boundary without scanning
//for marker bytes inside entity data.
std::expected<ParkSaveHeaderResult, SaveContainerError> read_park_save_header(std::span<const std::byte>, const SaveCompatibility&);
std::expected<std::vector<std::byte>, SaveContainerError> decode_bilz_save_payload(std::span<const std::byte>);
std::expected<std::vector<std::byte>, SaveContainerError> encode_bilz_save_payload(std::span<const std::byte>);
std::expected<ParkSaveContainer, SaveContainerError> decode_park_save(std::span<const std::byte>, const SaveCompatibility&);
std::expected<std::vector<std::byte>, SaveContainerError> encode_park_save(const ParkSaveHeader&, std::span<const std::byte> sections);
std::expected<ParkSaveBodyPrefix, SaveContainerError> read_park_save_body_prefix(std::span<const std::byte>);
std::expected<ParkSaveContainer, SaveContainerError> load_park_save_file(const std::filesystem::path&, const SaveCompatibility&);
std::expected<void, SaveContainerError> write_park_save_file(const std::filesystem::path&, const ParkSaveHeader&, std::span<const std::byte> sections);
} //namespace simcoaster
