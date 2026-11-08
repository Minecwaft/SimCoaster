#pragma once
#include "simcoaster/sound_catalog.hpp"
#include <limits>

namespace simcoaster::sound_detail {
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t cursor{};
    std::size_t base_offset{};

    [[noreturn]] void fail(SoundFailure reason, std::size_t at) const {
        throw SoundError{ reason, base_offset + at, {} };
    }

    std::span<const std::byte> take(std::size_t count) {
        if (count > bytes.size() - cursor)
            fail(SoundFailure::truncated, cursor);
        const auto result = bytes.subspan(cursor, count);
        cursor += count;
        return result;
    }

    template <class T>
    T read() {
        const auto input = take(save::value_width<T>);
        T result{};
        save::detail::decode<T>(std::span<const std::byte, save::value_width<T>>{ input.data(), input.size() }, result);
        return result;
    }

    template <class T>
    std::vector<T> array(std::size_t count) {
        if (count > (bytes.size() - cursor) / save::value_width<T>)
            fail(SoundFailure::count, cursor);
        std::vector<T> result;
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            result.push_back(read<T>());
        return result;
    }

    std::string string(std::size_t extent) {
        const auto begin = cursor;
        const auto data = take(extent);
        const auto end = std::ranges::find(data, std::byte{});
        if (end == data.end())
            fail(SoundFailure::string, begin);
        return { reinterpret_cast<const char*>(data.data()), static_cast<std::size_t>(end - data.begin()) };
    }
};

struct Writer {
    std::vector<std::byte> bytes;

    template <class T>
    void write(const T& value) {
        const auto encoded = save::detail::encode(value);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }

    void append(std::span<const std::byte> data) {
        bytes.insert(bytes.end(), data.begin(), data.end());
    }

    std::uint32_t size_word() const {
        if (bytes.size() > std::numeric_limits<std::uint32_t>::max())
            throw SoundError{ SoundFailure::count, bytes.size(), {} };
        return static_cast<std::uint32_t>(bytes.size());
    }
};
} //namespace simcoaster::sound_detail
