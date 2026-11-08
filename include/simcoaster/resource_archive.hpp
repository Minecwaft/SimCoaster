#pragma once
#include "asset_error.hpp"
#include "save_schema.hpp"
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>

namespace simcoaster {
std::string_view asset_failure_name(AssetFailure);

enum class DwfbEntryFlags : std::uint32_t {
    none = 0,
    unsupported = 1,
    bilz = 2,
    lz10fb = 4
};

struct DwfbHeader {
    [[=save::Field { "signature", 0 }]] std::array<char, 4> signature {};
    [[=save::Field { "version", 1 }]] std::uint32_t version {};
    [[=save::Field { "reserved_008", 2 }]] std::array<std::byte, 64> reserved {};
    [[=save::Field { "entry_count", 3 }]] std::uint32_t entry_count {};
    [[=save::Field { "name_block_offset", 4 }]] std::uint32_t name_block_offset {};
    [[=save::Field { "name_block_size", 5 }]] std::uint32_t name_block_size {};
    [[=save::Field { "reserved_054", 6 }]] std::array<std::byte, 4> reserved_tail {};
};

struct DwfbDirectoryRow {
    [[=save::Field { "row_key", 0 }]] std::uint32_t key {};
    [[=save::Field { "name_offset", 1 }]] std::uint32_t name_offset {};
    [[=save::Field { "name_length", 2 }]] std::uint32_t name_length {};
    [[=save::Field { "payload_offset", 3 }]] std::uint32_t payload_offset {};
    [[=save::Field { "stored_payload_size", 4 }]] std::uint32_t stored_size {};
    [[=save::Field { "flags", 5 }]] DwfbEntryFlags flags {};
    [[=save::Field { "transform_buffer_capacity", 6 }]] std::uint32_t decoded_capacity {};
    [[=save::Field { "unresolved_01C", 7 }]] std::array<std::byte, 12> unresolved_tail {};
};

static_assert(save::record_width<DwfbHeader> == 88);
static_assert(save::record_width<DwfbDirectoryRow> == 40);

struct Lz10FbDecoded {
    std::vector<std::byte> bytes;
    std::size_t input_consumed;
};

//The original decoder tests only bit0 of the first tag byte. It accepts an
//optional three-byte header field and ignores trailing input after the final
//token. Here both cursors, history distance and the measured output are
//checked.
std::expected<Lz10FbDecoded, AssetError> decode_lz10fb_payload(std::span<const std::byte>, std::size_t capacity = std::numeric_limits<std::size_t>::max());

struct DwfbEntry {
    std::string path;
    DwfbDirectoryRow row;
    std::size_t row_offset;

    std::uint32_t effective_size() const {
        return row.flags == DwfbEntryFlags::none ? row.stored_size : row.decoded_capacity;
    }
};

struct DwfbArchive {
private:
    std::vector<DwfbEntry> entries_;
    std::vector<std::size_t> entry_lookup_;

public:
    DwfbHeader header;
    std::vector<std::byte> bytes;
    DwfbArchive() = default;
    DwfbArchive(DwfbHeader, std::vector<std::byte>, std::vector<DwfbEntry>);

    //Native directory order and row indices remain authoritative. The lookup
    //index contains positions, so archive copies and moves remain independent.
    std::span<const DwfbEntry> entries() const {
        return entries_;
    }

    std::optional<std::size_t> find_entry(std::string_view path) const;
    std::expected<std::vector<std::byte>, AssetError> read_entry(std::size_t index) const;
};

std::expected<DwfbArchive, AssetError> parse_dwfb_archive(std::vector<std::byte>);
std::expected<DwfbArchive, AssetError> load_dwfb_archive(const std::filesystem::path&);
} //namespace simcoaster
