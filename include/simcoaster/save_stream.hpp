#pragma once
#include "save_schema.hpp"
#include "stream.hpp"

namespace simcoaster::save {
//These codecs commit a value only after its complete transfer. World entity
//fields deliberately use transfer_value's different partial-read policy.
template <class T>
    requires supported_value<T>
bool read(Stream& stream, T& value) {
    std::array<std::byte, value_width<T>> bytes{};
    if (stream.read(bytes) != static_cast<std::int32_t>(bytes.size()))
        return false;
    detail::decode(std::span<const std::byte, value_width<T>>{ bytes }, value);
    return true;
}

template <class T>
    requires supported_value<T>
bool write(Stream& stream, const T& value) {
    const auto bytes = detail::encode(value);
    return stream.write(bytes) == static_cast<std::int32_t>(bytes.size());
}
} //namespace simcoaster::save
