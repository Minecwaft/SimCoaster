#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace simcoaster {
struct Stream {
    virtual ~Stream() = default;
    virtual std::int32_t read(std::span<std::byte>) = 0;
    virtual std::int32_t write(std::span<const std::byte>) = 0;
};

struct PositionedStream : Stream {
    virtual std::int64_t position() const = 0;
};

//Owned byte storage for container codecs and staged publication. The cursor
//follows the existing stream contract; seeking beyond the buffer is
//unsupported.
class MemoryByteStream final : public PositionedStream {
public:
    std::vector<std::byte> bytes;
    std::size_t cursor {};
    std::int64_t position() const override;
    std::int32_t read(std::span<std::byte>) override;
    std::int32_t write(std::span<const std::byte>) override;
};
} //namespace simcoaster
