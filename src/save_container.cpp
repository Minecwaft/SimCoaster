#include "simcoaster/save_container.hpp"
#include "simcoaster/save_metadata_notices.hpp"
#include <fstream>
#include <limits>
#include <stdexcept>
#define ZLIB_CONST
#include <zlib.h>

namespace simcoaster {
namespace {
constexpr std::array<char, 4> bilz_magic { 'B', 'I', 'L', 'Z' };
constexpr std::uint32_t executable_signature = 19011973;
constexpr std::size_t bilz_header_size = 28;

class Reader {
    std::span<const std::byte> bytes_;
    std::size_t cursor_ {};

public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    std::size_t position() const {
        return cursor_;
    }

    std::optional<std::span<const std::byte>> take(std::size_t count) {
        if (count > bytes_.size() - cursor_) {
            return std::nullopt;
        }
        auto result = bytes_.subspan(cursor_, count);
        cursor_ += count;
        return result;
    }

    template <class T>
    std::optional<T> value() {
        constexpr auto size = save::value_width<T>;
        const auto bytes = take(size);
        if (!bytes) {
            return std::nullopt;
        }
        T result {};
        save::detail::decode<T>(std::span<const std::byte, size> { bytes->data(), size }, result);
        return result;
    }
};

template <class T>
void append(std::vector<std::byte>& bytes, const T& value) {
    const auto encoded = save::detail::encode(value);
    bytes.insert(bytes.end(), encoded.begin(), encoded.end());
}

auto failure(SaveContainerFailure reason, std::size_t offset) {
    return std::unexpected(SaveContainerError { reason, offset });
}

bool stamp_equal(std::string_view a, std::string_view b) {
    const auto c_string = [](std::string_view text) {
        return text.substr(0, text.find('\0'));
    };
    a = c_string(a);
    b = c_string(b);
    const auto lower = [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
    };
    return std::ranges::equal(a, b, [&](unsigned char x, unsigned char y) {
        return lower(x) == lower(y);
    });
}

struct InflateSession {
    z_stream stream {};

    ~InflateSession() {
        inflateEnd(&stream);
    }
};

struct DeflateSession {
    z_stream stream {};

    ~DeflateSession() {
        deflateEnd(&stream);
    }
};
} //namespace

std::string_view save_container_failure_name(SaveContainerFailure reason) {
    return match (reason) -> std::string_view {
        case SaveContainerFailure::io                        => "file I/O failed";
        case SaveContainerFailure::short_header              => "incomplete save header";
        case SaveContainerFailure::invalid_stamp             => "invalid version stamp";
        case SaveContainerFailure::stamp_mismatch            => "version stamp mismatch";
        case SaveContainerFailure::future_version            => "save version is newer than500";
        case SaveContainerFailure::invalid_language          => "invalid save language";
        case SaveContainerFailure::locale_mismatch           => "locale compatibility token mismatch";
        case SaveContainerFailure::missing_definition        => "required object definition is missing";
        case SaveContainerFailure::signature_mismatch        => "executable compatibility signature mismatch";
        case SaveContainerFailure::short_payload             => "incomplete save payload";
        case SaveContainerFailure::bad_magic                 => "invalid BILZ signature";
        case SaveContainerFailure::empty_payload             => "empty save payload";
        case SaveContainerFailure::invalid_window            => "invalid compression window";
        case SaveContainerFailure::invalid_compressed_stream => "invalid compressed save data";
        case SaveContainerFailure::size_mismatch             => "decompressed save size mismatch";
        case SaveContainerFailure::too_large                 => "save exceeds its32-bit wire size";
    };
}

SaveDefinitionMask make_save_definition_mask(std::span<const SaveContentDefinition> definitions, bool exclude_preview) {
    SaveDefinitionMask mask {};
    for (const auto& definition : definitions) {
        if (definition.downloaded_ride_number && !(exclude_preview && definition.is_preview_ride)) {
            const auto bit = definition.downloaded_ride_number - 1;
            if (bit >= mask.size() * 8) {
                throw std::invalid_argument("save definition number exceeds256");
            }
            mask[bit / 8] |= static_cast<std::uint8_t>(1u << (bit % 8));
        }
    }
    return mask;
}

bool validate_save_definition_mask_compatibility(const SaveDefinitionMask& saved, const SaveDefinitionMask& available) {
    //Native compares (saved_byte & bit) to literal1. Only bit0 is checked.
    for (std::size_t i = 0; i < saved.size(); ++i) {
        if ((saved[i] & 1u) && !(available[i] & 1u)) {
            return false;
        }
    }
    return true;
}

std::expected<ParkSaveHeaderResult, SaveContainerError> read_park_save_header(std::span<const std::byte> bytes, const SaveCompatibility& compatibility) {
    Reader reader { bytes };
    ParkSaveHeader header;
    const auto version = reader.value<std::int32_t>();
    const auto has_stamp = reader.value<std::uint8_t>();
    if (!version || !has_stamp) {
        return failure(SaveContainerFailure::short_header, reader.position());
    }
    header.version = *version;
    if (*has_stamp) {
        const auto length = reader.value<std::uint32_t>();
        if (!length) {
            return failure(SaveContainerFailure::short_header, reader.position());
        }
        const auto stamp = reader.take(*length);
        if (!stamp) {
            return failure(SaveContainerFailure::short_header, reader.position());
        }
        const auto end = std::ranges::find(*stamp, std::byte {});
        if (end == stamp->end()) {
            return failure(SaveContainerFailure::invalid_stamp, reader.position());
        }
        header.version_stamp = std::string { reinterpret_cast<const char*>(stamp->data()), static_cast<std::size_t>(end - stamp->begin()) };
        if (compatibility.local_version_stamp && !stamp_equal(*header.version_stamp, *compatibility.local_version_stamp)) {
            return failure(SaveContainerFailure::stamp_mismatch, reader.position());
        }
    }
    if (compatibility.mode == SaveLoadMode::full && *version > 500) {
        return failure(SaveContainerFailure::future_version, reader.position());
    }
    const auto language = reader.value<SaveLanguage>();
    if (!language) {
        return failure(SaveContainerFailure::short_header, reader.position());
    }
    const auto locale = reader.value<std::array<char16_t, 640>>();
    if (!locale) {
        return failure(SaveContainerFailure::short_header, reader.position());
    }
    if (std::to_underlying(*language) >= save_metadata_notices.size()) {
        return failure(SaveContainerFailure::invalid_language, reader.position());
    }
    header.language = *language;
    const auto terminator = std::ranges::find(*locale, char16_t {});
    const std::u16string_view token { locale->data(), static_cast<std::size_t>(terminator - locale->begin()) };
    if (token != save_metadata_notices[std::to_underlying(*language)]) {
        return failure(SaveContainerFailure::locale_mismatch, reader.position());
    }
    const auto mask = reader.value<std::array<std::uint8_t, 256>>();
    if (!mask) {
        return failure(SaveContainerFailure::short_header, reader.position());
    }
    std::ranges::copy_n(mask->begin(), header.definitions.size(), header.definitions.begin());
    const auto available = make_save_definition_mask(compatibility.definitions, compatibility.mode == SaveLoadMode::full);
    if (!validate_save_definition_mask_compatibility(header.definitions, available)) {
        return failure(SaveContainerFailure::missing_definition, reader.position());
    }
    const auto signature = reader.value<std::uint32_t>();
    if (!signature) {
        return failure(SaveContainerFailure::short_header, reader.position());
    }
    if (std::byteswap(*signature) != executable_signature) {
        return failure(SaveContainerFailure::signature_mismatch, reader.position());
    }
    const auto publication = reader.value<std::uint32_t>();
    if (!publication) {
        return failure(SaveContainerFailure::short_header, reader.position());
    }
    if (*publication) {
        header.publication = reader.value<SavePublicationHeader>();
        if (!header.publication) {
            return failure(SaveContainerFailure::short_header, reader.position());
        }
    }
    return ParkSaveHeaderResult { std::move(header), reader.position() };
}

std::expected<std::vector<std::byte>, SaveContainerError> decode_bilz_save_payload(std::span<const std::byte> input) {
    Reader reader { input };
    const auto magic = reader.value<std::array<char, 4>>();
    const auto size = reader.value<std::uint32_t>();
    if (input.size() < bilz_header_size) {
        return failure(SaveContainerFailure::short_payload, input.size());
    }
    if (*magic != bilz_magic) {
        return failure(SaveContainerFailure::bad_magic, 0);
    }
    if (!*size) {
        return failure(SaveContainerFailure::empty_payload, 4);
    }
    (void)reader.value<std::uint32_t>();
    const auto window = *reader.value<std::int32_t>();
    //The original inflater accepts only signed window magnitudes8..15. Modern
    //zlib also accepts gzip/autodetect settings; those are outside this format.
    if (window < -15 || window > 15 || (window > -8 && window < 8)) {
        return failure(SaveContainerFailure::invalid_window, 12);
    }
    const auto compressed = input.subspan(bilz_header_size);
    if (compressed.size() > std::numeric_limits<uInt>::max()) {
        return failure(SaveContainerFailure::too_large, bilz_header_size);
    }
    std::vector<std::byte> output(*size);
    InflateSession state;
    auto& stream = state.stream;
    if (inflateInit2(&stream, window) != Z_OK) {
        return failure(SaveContainerFailure::invalid_compressed_stream, bilz_header_size);
    }
    stream.next_in = reinterpret_cast<const Bytef*>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = *size;
    const auto result = inflate(&stream, Z_FINISH);
    if (result != Z_STREAM_END) {
        return failure(SaveContainerFailure::invalid_compressed_stream, bilz_header_size + stream.total_in);
    }
    if (stream.total_out != *size) {
        return failure(SaveContainerFailure::size_mismatch, 4);
    }
    return output;
}

std::expected<std::vector<std::byte>, SaveContainerError> encode_bilz_save_payload(std::span<const std::byte> input) {
    if (input.empty()) {
        return failure(SaveContainerFailure::empty_payload, 0);
    }
    if (input.size() > std::numeric_limits<uInt>::max()) {
        return failure(SaveContainerFailure::too_large, 0);
    }
    DeflateSession state;
    auto& stream = state.stream;
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 9, Z_DEFAULT_STRATEGY) != Z_OK) {
        return failure(SaveContainerFailure::invalid_compressed_stream, 0);
    }
    const auto bound = deflateBound(&stream, static_cast<uLong>(input.size()));
    if (static_cast<std::uint64_t>(bound) + bilz_header_size > std::numeric_limits<std::uint32_t>::max()) {
        return failure(SaveContainerFailure::too_large, 0);
    }
    std::vector<std::byte> output(bilz_header_size + bound);
    stream.next_in = reinterpret_cast<const Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data() + bilz_header_size);
    stream.avail_out = bound;
    if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
        return failure(SaveContainerFailure::invalid_compressed_stream, 0);
    }
    output.resize(bilz_header_size + stream.total_out);
    std::vector<std::byte> header;
    append(header, bilz_magic);
    append(header, static_cast<std::uint32_t>(input.size()));
    append(header, static_cast<std::uint32_t>(output.size()));
    append(header, std::int32_t { 15 });
    append(header, std::int32_t { 9 });
    append(header, std::uint32_t {});
    append(header, std::uint32_t {});
    std::ranges::copy(header, output.begin());
    return output;
}

std::expected<ParkSaveContainer, SaveContainerError> decode_park_save(std::span<const std::byte> bytes, const SaveCompatibility& compatibility) {
    auto header = read_park_save_header(bytes, compatibility);
    if (!header) {
        return std::unexpected(header.error());
    }
    if (compatibility.mode == SaveLoadMode::header_only) {
        return ParkSaveContainer { std::move(header->header), {} };
    }
    auto payload = decode_bilz_save_payload(bytes.subspan(header->payload_offset));
    if (!payload) {
        auto error = payload.error();
        error.offset += header->payload_offset;
        return std::unexpected(error);
    }
    return ParkSaveContainer { std::move(header->header), std::move(*payload) };
}

std::expected<std::vector<std::byte>, SaveContainerError> encode_park_save(const ParkSaveHeader& header, std::span<const std::byte> sections) {
    auto payload = encode_bilz_save_payload(sections);
    if (!payload) {
        return std::unexpected(payload.error());
    }
    const auto language = std::to_underlying(header.language);
    if (language >= save_metadata_notices.size()) {
        return failure(SaveContainerFailure::invalid_language, 0);
    }
    std::vector<std::byte> output;
    append(output, header.version);
    append(output, static_cast<std::uint8_t>(header.version_stamp.has_value()));
    if (header.version_stamp) {
        const auto stamp = std::string_view { *header.version_stamp }.substr(0, header.version_stamp->find('\0'));
        if (stamp.size() >= std::numeric_limits<std::uint32_t>::max()) {
            return failure(SaveContainerFailure::too_large, output.size());
        }
        append(output, static_cast<std::uint32_t>(stamp.size() + 1));
        for (char c : stamp) {
            output.push_back(static_cast<std::byte>(c));
        }
        output.push_back(std::byte {});
    }
    append(output, header.language);
    std::array<char16_t, 640> token {};
    std::ranges::copy(save_metadata_notices[language], token.begin());
    append(output, token);
    std::array<std::uint8_t, 256> mask {};
    std::ranges::copy(header.definitions, mask.begin());
    append(output, mask);
    append(output, std::byteswap(executable_signature));
    append(output, static_cast<std::uint32_t>(header.publication.has_value()));
    if (header.publication) {
        append(output, *header.publication);
    }
    output.insert(output.end(), payload->begin(), payload->end());
    return output;
}

std::expected<ParkSaveBodyPrefix, SaveContainerError> read_park_save_body_prefix(std::span<const std::byte> bytes) {
    Reader reader { bytes };
    const auto published = reader.value<std::uint32_t>(), size = reader.value<std::uint32_t>();
    if (!published || !size) {
        return failure(SaveContainerFailure::short_payload, reader.position());
    }
    const auto recording = reader.take(*size);
    if (!recording) {
        return failure(SaveContainerFailure::short_payload, reader.position());
    }
    return ParkSaveBodyPrefix { *published, *recording, bytes.subspan(reader.position()) };
}

std::expected<ParkSaveContainer, SaveContainerError> load_park_save_file(const std::filesystem::path& path, const SaveCompatibility& compatibility) {
    std::ifstream stream { path, std::ios::binary | std::ios::ate };
    if (!stream) {
        return failure(SaveContainerFailure::io, 0);
    }
    const auto end = static_cast<std::streamoff>(stream.tellg());
    if (end < 0) {
        return failure(SaveContainerFailure::io, 0);
    }
    if (!std::in_range<std::size_t>(end) || !std::in_range<std::streamsize>(end)) {
        return failure(SaveContainerFailure::too_large, 0);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return failure(SaveContainerFailure::io, static_cast<std::size_t>(stream.gcount()));
    }
    return decode_park_save(bytes, compatibility);
}

std::expected<void, SaveContainerError> write_park_save_file(const std::filesystem::path& path, const ParkSaveHeader& header, std::span<const std::byte> sections) {
    auto bytes = encode_park_save(header, sections);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (!std::in_range<std::streamsize>(bytes->size())) {
        return failure(SaveContainerFailure::too_large, 0);
    }
    std::ofstream stream { path, std::ios::binary | std::ios::trunc };
    if (!stream) {
        return failure(SaveContainerFailure::io, 0);
    }
    stream.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    stream.close();
    if (!stream) {
        return failure(SaveContainerFailure::io, 0);
    }
    return {};
}
} //namespace simcoaster
