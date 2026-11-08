#include "simcoaster/stream.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster {
std::int64_t MemoryByteStream::position() const {
    return static_cast<std::int64_t>(cursor);
}

std::int32_t MemoryByteStream::read(std::span<std::byte> out) {
    const auto count = std::min(out.size(), bytes.size() - cursor);
    if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return 0;
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), count, out.begin());
    cursor += count;
    return static_cast<std::int32_t>(count);
}

std::int32_t MemoryByteStream::write(std::span<const std::byte> in) {
    if (in.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) || in.size() > bytes.max_size() - cursor) {
        return 0;
    }
    bytes.resize(std::max(bytes.size(), cursor + in.size()));
    std::copy(in.begin(), in.end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor));
    cursor += in.size();
    return static_cast<std::int32_t>(in.size());
}
} //namespace simcoaster
