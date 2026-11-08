#include "simcoaster/resource_archive.hpp"
#include "simcoaster/save_container.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>

namespace simcoaster {
namespace {
auto failure(AssetFailure reason, std::size_t offset) {
    return std::unexpected(AssetError { reason, offset });
}

bool contains(std::size_t size, std::size_t offset, std::size_t length) {
    return offset <= size && length <= size - offset;
}

std::string normalize(std::string_view path) {
    std::string result { path };
    for (char& character : result) {
        if (character == '/') {
            character = '\\';
        } else if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return result;
}

template <class T>
T read(std::span<const std::byte> bytes, std::size_t offset) {
    T value {};
    save::detail::decode<T>(std::span<const std::byte, save::value_width<T>> { bytes.data() + offset, save::value_width<T> }, value);
    return value;
}
} //namespace

std::string_view asset_failure_name(AssetFailure reason) {
    return match (reason) -> std::string_view {
        case AssetFailure::io                  => "asset file I/O failed";
        case AssetFailure::short_header        => "incomplete asset header";
        case AssetFailure::bad_magic           => "invalid DWFB signature";
        case AssetFailure::unsupported_version => "unsupported DWFB version";
        case AssetFailure::directory_bounds    => "DWFB directory is outside the archive";
        case AssetFailure::name_block_bounds   => "DWFB name block is outside the archive";
        case AssetFailure::name_bounds         => "DWFB entry name is outside its name block";
        case AssetFailure::unterminated_name   => "DWFB entry name has no terminator";
        case AssetFailure::payload_bounds      => "DWFB payload is outside the archive";
        case AssetFailure::unsupported_flag    => "DWFB entry uses unsupported flag1";
        case AssetFailure::entry_index         => "DWFB entry index is outside the directory";
        case AssetFailure::truncated_packet    => "incomplete LZ packet";
        case AssetFailure::invalid_distance    => "LZ back-reference precedes output";
        case AssetFailure::output_overflow     => "LZ output exceeds declared capacity";
        case AssetFailure::size_mismatch       => "decoded asset size differs from its declaration";
        case AssetFailure::bilz                => "invalid BILZ asset payload";
    };
}

std::expected<Lz10FbDecoded, AssetError> decode_lz10fb_payload(std::span<const std::byte> input, std::size_t capacity) {
    if (input.size() < 2) {
        return failure(AssetFailure::short_header, input.size());
    }
    const auto byte = [&](std::size_t offset) {
        return std::to_integer<std::uint32_t>(input[offset]);
    };
    const std::size_t size_offset = (byte(0) & 1u) ? 5 : 2;
    if (!contains(input.size(), size_offset, 3)) {
        return failure(AssetFailure::short_header, input.size());
    }
    const std::size_t size = (byte(size_offset) << 16) + (byte(size_offset + 1) << 8) + byte(size_offset + 2);
    if (size > capacity) {
        return failure(AssetFailure::output_overflow, size_offset);
    }
    Lz10FbDecoded result { std::vector<std::byte>(size), size_offset + 3 };
    std::size_t output = 0;
    const auto literals = [&](std::size_t count) -> std::expected<void, AssetError> {
        if (!contains(input.size(), result.input_consumed, count)) {
            return failure(AssetFailure::truncated_packet, result.input_consumed);
        }
        if (count > size - output) {
            return failure(AssetFailure::output_overflow, result.input_consumed);
        }
        std::ranges::copy(input.subspan(result.input_consumed, count), result.bytes.begin() + static_cast<std::ptrdiff_t>(output));
        result.input_consumed += count;
        output += count;
        return {};
    };
    for (;;) {
        const auto packet = result.input_consumed;
        if (packet == input.size()) {
            return failure(AssetFailure::truncated_packet, packet);
        }
        const auto token = byte(result.input_consumed++);
        std::size_t prefix = 0, distance = 0, length = 0;
        if ((token & 0x80) == 0) {
            if (!contains(input.size(), result.input_consumed, 1)) {
                return failure(AssetFailure::truncated_packet, packet);
            }
            prefix = token & 3;
            distance = 1 + byte(result.input_consumed++) + 8 * (token & 0x60);
            length = 3 + ((token >> 2) & 7);
        } else if ((token & 0x40) == 0) {
            if (!contains(input.size(), result.input_consumed, 2)) {
                return failure(AssetFailure::truncated_packet, packet);
            }
            const auto first = byte(result.input_consumed++), second = byte(result.input_consumed++);
            prefix = first >> 6;
            distance = 1 + ((first & 0x3f) << 8) + second;
            length = 4 + (token & 0x3f);
        } else if ((token & 0x20) == 0) {
            if (!contains(input.size(), result.input_consumed, 3)) {
                return failure(AssetFailure::truncated_packet, packet);
            }
            const auto first = byte(result.input_consumed++), second = byte(result.input_consumed++), third = byte(result.input_consumed++);
            prefix = token & 3;
            distance = 1 + (first << 8) + second + 4096 * (token & 0x10);
            length = 5 + third + ((token & 0x0c) << 6);
        } else {
            const auto count = 4 * (token & 0x1f) + 4;
            const bool terminal = count > 0x70;
            if (auto copied = literals(terminal ? token & 3 : count); !copied) {
                return std::unexpected(copied.error());
            }
            if (!terminal) {
                continue;
            }
            if (output != size) {
                return failure(AssetFailure::size_mismatch, packet);
            }
            return result;
        }
        if (auto copied = literals(prefix); !copied) {
            return std::unexpected(copied.error());
        }
        if (distance > output) {
            return failure(AssetFailure::invalid_distance, packet);
        }
        if (length > size - output) {
            return failure(AssetFailure::output_overflow, packet);
        }
        //Each copy reads initialized history and ends before its destination.
        //Grow the available repeated prefix, retaining forward overlap semantics.
        const auto start = output;
        for (std::size_t copied = 0; copied < length;) {
            const auto count = std::min(length - copied, distance + copied);
            std::memcpy(result.bytes.data() + start + copied, result.bytes.data() + start - distance, count);
            copied += count;
        }
        output += length;
    }
}

DwfbArchive::DwfbArchive(DwfbHeader metadata, std::vector<std::byte> payload, std::vector<DwfbEntry> directory) : entries_(std::move(directory)), entry_lookup_(entries_.size()), header(metadata), bytes(std::move(payload)) {
    for (std::size_t i = 0; i < entry_lookup_.size(); ++i) {
        entry_lookup_[i] = i;
    }
    std::ranges::sort(entry_lookup_, [&](std::size_t a, std::size_t b) {
        const auto order = entries_[a].path.compare(entries_[b].path);
        return order < 0 || (order == 0 && a < b);
    });
}

std::optional<std::size_t> DwfbArchive::find_entry(std::string_view path) const {
    const auto key = normalize(path);
    const auto found = std::ranges::lower_bound(entry_lookup_, key, {}, [&](std::size_t index) -> const std::string& {
        return entries_[index].path;
    });
    if (found != entry_lookup_.end() && entries_[*found].path == key) {
        return *found;
    }
    return std::nullopt;
}

std::expected<std::vector<std::byte>, AssetError> DwfbArchive::read_entry(std::size_t index) const {
    if (index >= entries_.size()) {
        return failure(AssetFailure::entry_index, index);
    }
    const auto& entry = entries_[index];
    const auto& row = entry.row;
    const auto flags = std::to_underlying(row.flags);
    if (!contains(bytes.size(), row.payload_offset, row.stored_size)) {
        return failure(AssetFailure::payload_bounds, row.payload_offset);
    }
    if (flags & std::to_underlying(DwfbEntryFlags::unsupported)) {
        return failure(AssetFailure::unsupported_flag, entry.row_offset + 20);
    }
    const auto source = std::span { bytes }.subspan(row.payload_offset, row.stored_size);
    //Native selection rejects mask1, then selects BILZ before LZ. Higher bits are ignored.
    if (flags & std::to_underlying(DwfbEntryFlags::bilz)) {
        auto output = decode_bilz_save_payload(source);
        if (!output) {
            return failure(AssetFailure::bilz, row.payload_offset + output.error().offset);
        }
        if (output->size() != row.decoded_capacity) {
            return failure(AssetFailure::size_mismatch, entry.row_offset + 24);
        }
        return std::move(*output);
    }
    if (flags & std::to_underlying(DwfbEntryFlags::lz10fb)) {
        auto output = decode_lz10fb_payload(source, row.decoded_capacity);
        if (!output) {
            auto error = output.error();
            error.offset += row.payload_offset;
            return std::unexpected(error);
        }
        if (output->bytes.size() != row.decoded_capacity) {
            return failure(AssetFailure::size_mismatch, entry.row_offset + 24);
        }
        return std::move(output->bytes);
    }
    return std::vector<std::byte> { source.begin(), source.end() };
}

std::expected<DwfbArchive, AssetError> parse_dwfb_archive(std::vector<std::byte> bytes) {
    if (bytes.size() < save::record_width<DwfbHeader>) {
        return failure(AssetFailure::short_header, bytes.size());
    }
    const auto header = read<DwfbHeader>(bytes, 0);
    if (header.signature != std::array { 'D', 'W', 'F', 'B' }) {
        return failure(AssetFailure::bad_magic, 0);
    }
    if (header.version > 2) {
        return failure(AssetFailure::unsupported_version, 4);
    }
    constexpr auto start = save::record_width<DwfbHeader>, row_size = save::record_width<DwfbDirectoryRow>;
    if (header.entry_count > (bytes.size() - start) / row_size) {
        return failure(AssetFailure::directory_bounds, start);
    }
    if (header.version == 2 && !contains(bytes.size(), header.name_block_offset, header.name_block_size)) {
        return failure(AssetFailure::name_block_bounds, header.name_block_offset);
    }
    std::vector<DwfbEntry> entries;
    entries.reserve(header.entry_count);
    std::string parent;
    for (std::size_t i = 0; i < header.entry_count; ++i) {
        const auto offset = start + i * row_size;
        const auto row = read<DwfbDirectoryRow>(bytes, offset);
        std::size_t length;
        if (header.version == 2) {
            if (row.name_offset < header.name_block_offset || row.name_offset - header.name_block_offset >= header.name_block_size) {
                return failure(AssetFailure::name_bounds, row.name_offset);
            }
            //Version2 ignores row.name_length and reads the NUL-terminated bulk name.
            length = header.name_block_size - (row.name_offset - header.name_block_offset);
        } else {
            length = static_cast<std::size_t>(row.name_length) + 1;
            if (!contains(bytes.size(), row.name_offset, length)) {
                return failure(AssetFailure::name_bounds, row.name_offset);
            }
        }
        const std::string_view name { reinterpret_cast<const char*>(bytes.data() + row.name_offset), length };
        const auto terminator = name.find('\0');
        if (terminator == std::string_view::npos) {
            return failure(AssetFailure::unterminated_name, row.name_offset);
        }
        auto normalized = normalize(name.substr(0, terminator));
        const auto slash = normalized.rfind('\\');
        //Native parent_node is initialized once, outside the row loop. A bare
        //filename following a directory-qualified name retains that parent.
        if (slash != std::string::npos) {
            parent = normalized.substr(0, slash + 1);
        }
        const auto leaf = slash == std::string::npos ? std::string_view { normalized } : std::string_view { normalized }.substr(slash + 1);
        entries.push_back({ parent + std::string { leaf }, row, offset });
    }
    return DwfbArchive { header, std::move(bytes), std::move(entries) };
}

std::expected<DwfbArchive, AssetError> load_dwfb_archive(const std::filesystem::path& path) {
    std::ifstream stream { path, std::ios::binary | std::ios::ate };
    if (!stream) {
        return failure(AssetFailure::io, 0);
    }
    const auto end = static_cast<std::streamoff>(stream.tellg());
    if (end < 0 || !std::in_range<std::size_t>(end) || !std::in_range<std::streamsize>(end)) {
        return failure(AssetFailure::io, 0);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return failure(AssetFailure::io, static_cast<std::size_t>(stream.gcount()));
    }
    return parse_dwfb_archive(std::move(bytes));
}
} //namespace simcoaster
