#pragma once
#include "save_schema.hpp"
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace simcoaster {
enum class SoundFailure {
    io,
    missing,
    truncated,
    signature,
    count,
    string,
    index,
    sample_header,
    sample_format,
    mpeg,
    pcm,
    selection,
    reference,
    configuration
};

struct SoundError {
    SoundFailure reason;
    std::size_t offset{};
    std::string resource;
    std::optional<std::uint32_t> sample_token{};
};

std::string describe_sound_error(const SoundError&);

//Explicit file layouts. Pointer-shaped words are serialized placeholders,
//never addresses in the reconstructed process. Runtime graphs use indices.
namespace sound_wire {
struct MapHeader {
    [[=save::Field{ "format_tag", 0 }]] std::array<std::uint32_t, 4> tag{};
    [[=save::Field{ "revision", 1 }]] std::uint32_t revision{};
};

struct ArrayHeader {
    [[=save::Field{ "storage_placeholder", 0 }]] std::uint32_t storage{};
    [[=save::Field{ "count", 1 }]] std::uint32_t count{};
};

struct BankRecord {
    [[=save::Field{ "runtime_word", 0 }]] std::uint32_t runtime_word{};
    [[=save::Field{ "runtime_byte", 1 }]] std::uint8_t runtime_byte{};
    [[=save::Field{ "name_placeholder", 2 }]] std::uint32_t name{};
    [[=save::Field{ "source_flags", 3 }]] std::uint8_t flags{};
    [[=save::Field{ "opaque_00A", 4 }]] std::uint8_t opaque_00A{};
};

struct Group {
    [[=save::Field{ "descriptor_count", 0 }]] std::uint32_t count{};
    [[=save::Field{ "descriptors_placeholder", 1 }]] std::uint32_t descriptors{};
    [[=save::Field{ "channel_enable_mask", 2 }]] std::uint8_t channel_mask{};
    [[=save::Field{ "opaque_009", 3 }]] std::uint8_t opaque_009{};
    [[=save::Field{ "opaque_00A", 4 }]] std::uint16_t opaque_00A{};
    [[=save::Field{ "opaque_00C", 5 }]] std::array<std::uint32_t, 3> opaque_00C{};
};

struct Descriptor {
    [[=save::Field{ "request_key", 0 }]] std::uint32_t key{};
    [[=save::Field{ "selection_count", 1 }]] std::uint32_t count{};
    [[=save::Field{ "selections_placeholder", 2 }]] std::uint32_t selections{};
    [[=save::Field{ "priority", 3 }]] std::uint32_t priority{};
    [[=save::Field{ "source_flags", 4 }]] std::uint16_t flags{};
    [[=save::Field{ "initial_key_word", 5 }]] std::uint16_t initial_key{};
};

struct Selection {
    [[=save::Field{ "sample_count", 0 }]] std::uint16_t sample_count{};
    [[=save::Field{ "unfiltered_sample_count", 1 }]] std::uint16_t unfiltered_count{};
    [[=save::Field{ "branch_count", 2 }]] std::uint32_t branch_count{};
    [[=save::Field{ "samples_placeholder", 3 }]] std::uint32_t samples{};
    [[=save::Field{ "minimum_level", 4 }]] std::uint8_t minimum_level{};
    [[=save::Field{ "maximum_level", 5 }]] std::uint8_t maximum_level{};
    [[=save::Field{ "minimum_frequency_parameter", 6 }]] std::int8_t minimum_frequency{};
    [[=save::Field{ "maximum_frequency_parameter", 7 }]] std::int8_t maximum_frequency{};
    [[=save::Field{ "minimum_clock_interval", 8 }]] std::uint16_t minimum_interval{};
    [[=save::Field{ "maximum_clock_interval", 9 }]] std::uint16_t maximum_interval{};
    [[=save::Field{ "opaque_014", 10 }]] std::uint16_t opaque_014{};
    [[=save::Field{ "first_key_id", 11 }]] std::uint16_t first_key{};
    [[=save::Field{ "first_key_mask", 12 }]] std::uint16_t first_mask{};
    [[=save::Field{ "second_key_id", 13 }]] std::uint16_t second_key{};
    [[=save::Field{ "second_key_mask", 14 }]] std::uint16_t second_mask{};
    [[=save::Field{ "selection_weight", 15 }]] std::uint32_t weight{};
    [[=save::Field{ "request_scalar", 16 }]] float request_scalar{};
    [[=save::Field{ "branches_placeholder", 17 }]] std::uint32_t branches{};
};

struct Sample {
    [[=save::Field{ "one_based_sample_token", 0 }]] std::uint32_t token{};
    [[=save::Field{ "cumulative_weight", 1 }]] std::uint32_t cumulative_weight{};
    [[=save::Field{ "clock_offset_ms", 2 }]] std::uint32_t clock_offset_ms{};
    [[=save::Field{ "one_based_bank_reference", 3 }]] std::uint16_t bank_reference{};
    [[=save::Field{ "opaque_00E", 4 }]] std::uint16_t opaque_00E{};
};

struct Branch {
    [[=save::Field{ "one_based_selection_index", 0 }]] std::uint32_t selection{};
    [[=save::Field{ "opaque_004", 1 }]] std::uint16_t opaque_004{};
    [[=save::Field{ "minimum_value", 2 }]] std::uint8_t minimum{};
    [[=save::Field{ "maximum_value", 3 }]] std::uint8_t maximum{};
};

struct SampleHeader {
    [[=save::Field{ "header_bytes", 0 }]] std::uint32_t header_bytes{};
    [[=save::Field{ "stored_bytes", 1 }]] std::int32_t stored_bytes{};
    [[=save::Field{ "source_name", 2 }]] std::array<char, 16> source_name{};
    [[=save::Field{ "sample_rate", 3 }]] std::uint16_t sample_rate{};
    [[=save::Field{ "bits_per_sample", 4 }]] std::uint8_t bits_per_sample{};
    [[=save::Field{ "format_flags", 5 }]] std::uint8_t format_flags{};
    [[=save::Field{ "source_word", 6 }]] std::uint32_t source_word{};
    //Decoded byte length when loop_end is zero; loop start otherwise.
    [[=save::Field{ "extent_or_loop_start", 7 }]] std::uint32_t extent{};
    [[=save::Field{ "loop_end", 8 }]] std::uint32_t loop_end{};
};

static_assert(save::value_width<MapHeader> == 20);
static_assert(save::value_width<ArrayHeader> == 8);
static_assert(save::value_width<BankRecord> == 11);
static_assert(save::value_width<Group> == 24);
static_assert(save::value_width<Descriptor> == 20);
static_assert(save::value_width<Selection> == 42);
static_assert(save::value_width<Sample> == 16);
static_assert(save::value_width<Branch> == 8);
static_assert(save::value_width<SampleHeader> == 40);
} //namespace sound_wire

enum class SoundBankFlags : std::uint8_t {
    mapped = 1,
    streamed = 2,
    soundfont = 4,
    global_root = 32,
    droppable = 64
};

struct SoundBankSource {
    std::string name; //Basename selected by native8A7740.
    std::string original_name;
    std::uint8_t flags{}, opaque_00A{};
};

struct SoundBankMap {
    std::uint32_t revision{};
    std::vector<SoundBankSource> sources;
};

struct SoundSelection {
    mutable sound_wire::Selection parameters;
    mutable std::vector<sound_wire::Sample> samples;
    std::vector<sound_wire::Branch> branches; //One-based indices within descriptor.
};

struct SoundDescriptor {
    sound_wire::Descriptor parameters;
    mutable std::vector<SoundSelection> selections;
};

struct SoundCatalogGroup {
    sound_wire::Group parameters;
    std::vector<SoundDescriptor> descriptors;
    const SoundDescriptor* find(std::uint32_t key) const;
};

struct SoundEffectMap {
    std::uint32_t revision{};
    std::vector<SoundCatalogGroup> groups;
};

std::expected<SoundBankMap, SoundError> parse_sound_bank_map(std::span<const std::byte>);
std::expected<SoundEffectMap, SoundError> parse_sound_effect_map(std::span<const std::byte>);

struct SoundCatalogReferences {
    std::vector<std::string> paths;
    std::size_t bytes_consumed{};
};

std::expected<SoundCatalogReferences, SoundError> parse_sound_catalog_references(std::span<const std::byte>);

struct SoundEventNames {
    std::string catalog_path;
    std::map<std::string, std::uint32_t, std::less<>> keys;
};

std::expected<SoundEventNames, SoundError> parse_sound_event_names(std::string_view);

struct SoundSampleData {
    sound_wire::SampleHeader header;
    std::size_t header_offset{}, payload_offset{};
    std::optional<SoundError> validation_error{};
};

struct SoundSampleBank {
    std::vector<std::byte> bytes;
    std::vector<SoundSampleData> samples;
    //The native resource API's token is one-based. Zero never denotes sample0.
    const SoundSampleData* find(std::uint32_t token) const;
    std::expected<const SoundSampleData*, SoundError> sample(std::uint32_t token) const;
    std::span<const std::byte> payload(const SoundSampleData&) const;
};

std::expected<SoundSampleBank, SoundError> parse_sound_sample_bank(std::vector<std::byte>);

struct SoundPcmSample {
    std::uint32_t sample_rate{}, channels{};
    std::vector<float> interleaved;

    std::uint64_t frames() const {
        return channels ? interleaved.size() / channels : 0;
    }
};

//Owned x64 decode. MPEG layers I/II/III use minimp3; the original x87/QMixer
//ABI and hardware SoundFont path are not dependencies of this interface.
std::expected<SoundPcmSample, SoundError> decode_sound_sample(const SoundSampleBank&, std::uint32_t token);
} //namespace simcoaster
