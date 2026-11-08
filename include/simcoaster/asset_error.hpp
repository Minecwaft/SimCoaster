#pragma once
#include <cstddef>

namespace simcoaster {
enum class AssetFailure {
    io,
    short_header,
    bad_magic,
    unsupported_version,
    directory_bounds,
    name_block_bounds,
    name_bounds,
    unterminated_name,
    payload_bounds,
    unsupported_flag,
    entry_index,
    truncated_packet,
    invalid_distance,
    output_overflow,
    size_mismatch,
    bilz,
};

struct AssetError {
    AssetFailure reason;
    std::size_t offset;
};
} //namespace simcoaster
