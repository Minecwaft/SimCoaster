#pragma once
#include <iterator>
#include <format>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace simcoaster::sam {
enum class Failure {
    path,
    unknown_field,
    index,
    value,
    range,
    map_fields,
    map_description,
    map_marker,
    map_symbol,
    map_width,
    map_height
};

struct Error {
    Failure reason;
    std::size_t offset;
    std::string group {}, field {};
    std::optional<std::uint32_t> index {};
    std::string value {};
};

inline std::string describe_error(std::string_view file, const Error& error) {
    std::string result { file };
    result += ": ";
    result += error.group.empty() ? "SAM" : error.group;
    if (error.index) {
        std::format_to(std::back_inserter(result), "[{}]", *error.index);
    }
    if (!error.field.empty()) {
        result += '.' + error.field;
    }
    std::format_to(std::back_inserter(result), " at offset {}: ", error.offset);
    result += match (error.reason) -> std::string_view {
        case Failure::path          => "invalid field path";
        case Failure::unknown_field => "unknown field";
        case Failure::index         => "index outside capacity";
        case Failure::value         => "invalid value";
        case Failure::range         => "value outside native bounds";
        case _                      => "invalid map value";
    };
    if (!error.value.empty()) {
        result += " (value '" + error.value + "')";
    }
    return result;
}
} //namespace simcoaster::sam
