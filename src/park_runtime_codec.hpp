#pragma once
#include "simcoaster/save_stream.hpp"
#include "park_runtime_state.hpp"

namespace simcoaster::park_detail {
using save::read;
using save::write;

inline bool marker(Stream& stream, std::uint32_t expected) {
    std::uint32_t actual {};
    return read(stream, actual) && actual == expected;
}

inline void ensure(bool okay, std::string_view message) {
    if (!okay) {
        throw std::runtime_error(std::string { message });
    }
}

inline bool restore_sound_snapshot(MemoryByteStream& stream, SoundSnapshot& target) {
    SoundSnapshot result;
    std::uint32_t size {};
    if (!read(stream, result.backend_present) || !read(stream, result.feature_flags) || !read(stream, size) || size > stream.bytes.size() - stream.cursor) {
        return false;
    }
    result.global_state.resize(size);
    if (stream.read(result.global_state) != static_cast<std::int32_t>(size)) {
        return false;
    }
    if (result.backend_present) {
        const auto begin = stream.cursor;
        const auto parsed = parse_sound_backend_snapshot_prefix(std::span<const std::byte> { stream.bytes }.subspan(begin));
        if (!parsed) {
            return false;
        }
        stream.cursor += parsed->bytes_consumed;
        result.backend_state.assign(stream.bytes.begin() + static_cast<std::ptrdiff_t>(begin), stream.bytes.begin() + static_cast<std::ptrdiff_t>(stream.cursor));
    }
    target = std::move(result);
    return true;
}
} //namespace simcoaster::park_detail
