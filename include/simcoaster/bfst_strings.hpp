#pragma once
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace simcoaster {
struct MbToUnicodeMap {
    std::uint8_t threshold {};
    std::uint16_t index_limit {};
    std::vector<char16_t> mapping;
};

struct BfstLiteralTable {
    std::uint32_t resource_id {};
    std::vector<std::u16string> strings;
};

struct BfstError {
    std::size_t offset;
    std::string reason;
};

struct BfstWideStringArgument {
    std::uint32_t key;
    std::u16string_view value;
};

struct BfstSignedIntegerArgument {
    std::uint32_t key;
    std::int32_t value;
};

//Formats one row, including keyed wide strings. Optional BFST state opcodes
//are consumed; the native wide-string argument ignores their values.
std::expected<std::u16string, BfstError> format_bfst_wide_string(std::span<const std::byte>, const MbToUnicodeMap&, std::uint32_t row, std::size_t capacity, std::span<const BfstWideStringArgument> arguments = {}, std::span<const BfstSignedIntegerArgument> integers = {});
std::expected<MbToUnicodeMap, BfstError> parse_mb_to_unicode_map(std::span<const std::byte>);
//Literal lookup follows the native no-argument token stream. Formatting rows
//requiring an MRAV argument fail explicitly; no placeholder text is invented.
std::expected<BfstLiteralTable, BfstError> parse_bfst_literals(std::span<const std::byte>, const MbToUnicodeMap&);
} //namespace simcoaster
