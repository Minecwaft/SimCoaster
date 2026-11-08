#include "simcoaster/bfst_output.hpp"
#include <algorithm>
#include <array>
#include <charconv>

namespace simcoaster {
namespace {
void require(bool valid, std::size_t offset, const char* reason) {
    if (!valid) {
        throw BfstError { offset, reason };
    }
}

std::uint32_t integer(std::span<const std::byte> data, std::size_t offset, std::size_t width = 4) {
    require(offset <= data.size() && width <= data.size() - offset, offset, "truncated string resource");
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < width; ++i) {
        result |= static_cast<std::uint32_t>(data[offset + i]) << (8 * i);
    }
    return result;
}

std::u16string decode(std::span<const std::byte> bytes, const MbToUnicodeMap& map, std::size_t offset) {
    std::u16string result;
    for (std::size_t i = 0; i < bytes.size();) {
        auto index = static_cast<std::uint16_t>(static_cast<std::uint8_t>(bytes[i++]) - 1);
        if (index >= map.threshold) {
            require(i < bytes.size(), offset + i, "truncated multibyte character");
            index = static_cast<std::uint16_t>(static_cast<std::uint8_t>(bytes[i++]) + 255u * (index + 257u) - 254u * map.threshold);
        }
        if (index >= map.index_limit) {
            result.push_back(0);
        } else {
            require(index < map.mapping.size(), offset + i, "character exceeds physical mapping table");
            result.push_back(map.mapping[index]);
        }
    }
    return result;
}
} //namespace

std::expected<MbToUnicodeMap, BfstError> parse_mb_to_unicode_map(std::span<const std::byte> bytes) {
    try {
        require(integer(bytes, 0) == 1431127618u, 0, "expected BFMU conversion table");
        MbToUnicodeMap result;
        result.threshold = static_cast<std::uint8_t>(255u - integer(bytes, 4, 1));
        result.index_limit = static_cast<std::uint16_t>(integer(bytes, 7, 1) + 255u * (static_cast<std::uint8_t>(integer(bytes, 6, 1) - 1) + 257u) - 254u * result.threshold);
        require(bytes.size() >= 8 && !(bytes.size() & 1u), bytes.size(), "invalid conversion payload");
        for (std::size_t i = 8; i < bytes.size(); i += 2) {
            result.mapping.push_back(static_cast<char16_t>(integer(bytes, i, 2)));
        }
        return result;
    } catch (const BfstError& error) {
        return std::unexpected(error);
    }
}

BfstWideOutput format_bfst_wide_output(std::span<const std::byte> bytes, const MbToUnicodeMap& map, std::uint32_t row, std::size_t capacity, std::span<const BfstWideStringArgument> arguments, std::span<const BfstSignedIntegerArgument> integers) {
    std::u16string text;
    try {
        require(capacity != 0, 0, "zero BFST output capacity");
        require(integer(bytes, 0) == 1414743618u, 0, "expected BFST string table");
        const auto count = integer(bytes, 8);
        require(bytes.size() >= 12 && count <= (bytes.size() - 12) / 4, 8, "invalid BFST row count");
        require(row < count, 12, "BFST row is out of range");
        std::size_t cursor = 12u + static_cast<std::uint64_t>(integer(bytes, 12 + static_cast<std::size_t>(row) * 4));
        while (true) {
            const auto token = integer(bytes, cursor);
            cursor += 4;
            const auto operand = token >> 8;
            match (token & 255u) {
                case 0 => {
                    if (text.size() >= capacity) {
                        text.resize(capacity - 1);
                    }
                    return { std::move(text), {} };
                }
                case 1 => {
                    require(cursor <= bytes.size() && operand <= bytes.size() - cursor, cursor, "truncated BFST literal");
                    text += decode(bytes.subspan(cursor, operand), map, cursor);
                    cursor += (static_cast<std::size_t>(operand) + 3u) & ~std::size_t { 3 };
                }
                case 2 => {
                    if (const auto argument = std::ranges::find(arguments, operand, &BfstWideStringArgument::key); argument != arguments.end()) {
                        const auto end = argument->value.find(char16_t {});
                        text += argument->value.substr(0, end);
                    } else {
                        const auto number = std::ranges::find(integers, operand, &BfstSignedIntegerArgument::key);
                        require(number != integers.end(), cursor - 4, "BFST row requires a missing formatting argument");
                        std::array<char, 18> formatted {};
                        const auto result = std::to_chars(formatted.data(), formatted.data() + formatted.size(), number->value);
                        require(result.ec == std::errc {}, cursor - 4, "BFST integer formatting failed");
                        for (auto p = formatted.data(); p != result.ptr; ++p) {
                            text.push_back(static_cast<char16_t>(*p));
                        }
                    }
                }
                case 3 => {
                    require(cursor <= bytes.size() && operand <= (bytes.size() - cursor) / 4, cursor, "truncated BFST supplemental data");
                    cursor += static_cast<std::size_t>(operand) * 4;
                }
                case 4 or 5 or 6 => {
                }
                case _ => throw BfstError { cursor - 4, "unknown BFST opcode" };
            }
        }
    } catch (const BfstError& error) {
        if (capacity && text.size() >= capacity) {
            text.resize(capacity - 1);
        }
        return { std::move(text), error };
    }
}

std::expected<std::u16string, BfstError> format_bfst_wide_string(std::span<const std::byte> bytes, const MbToUnicodeMap& map, std::uint32_t row, std::size_t capacity, std::span<const BfstWideStringArgument> arguments, std::span<const BfstSignedIntegerArgument> integers) {
    auto output = format_bfst_wide_output(bytes, map, row, capacity, arguments, integers);
    if (output.error) {
        return std::unexpected(std::move(*output.error));
    }
    return std::move(output.text);
}

std::expected<BfstLiteralTable, BfstError> parse_bfst_literals(std::span<const std::byte> bytes, const MbToUnicodeMap& map) {
    try {
        require(integer(bytes, 0) == 1414743618u, 0, "expected BFST string table");
        BfstLiteralTable result;
        result.resource_id = integer(bytes, 4);
        const auto count = integer(bytes, 8);
        require(bytes.size() >= 12 && count <= (bytes.size() - 12) / 4, 8, "invalid BFST row count");
        result.strings.reserve(count);
        for (std::size_t row = 0; row < count; ++row) {
            std::size_t cursor = 12u + static_cast<std::uint64_t>(integer(bytes, 12 + row * 4));
            std::u16string text;
            bool finished = false;
            while (!finished) {
                const auto token = integer(bytes, cursor);
                cursor += 4;
                const auto operand = token >> 8;
                match (token & 255u) {
                    case 0 => finished = true;
                    case 1 => {
                        require(cursor <= bytes.size() && operand <= bytes.size() - cursor, cursor, "truncated BFST literal");
                        text += decode(bytes.subspan(cursor, operand), map, cursor);
                        cursor += (static_cast<std::size_t>(operand) + 3u) & ~std::size_t { 3 };
                    }
                    case 3 => {
                        require(cursor <= bytes.size() && operand <= (bytes.size() - cursor) / 4, cursor, "truncated BFST supplemental data");
                        cursor += static_cast<std::size_t>(operand) * 4;
                    }
                    case 4 or 5 or 6 => {
                    }
                    case 2 => throw BfstError { cursor - 4, "BFST row requires formatting arguments" };
                    case _ => throw BfstError { cursor - 4, "unknown BFST opcode" };
                }
            }
            result.strings.push_back(std::move(text));
        }
        return result;
    } catch (const BfstError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
