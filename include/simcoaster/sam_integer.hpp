#pragma once
#include "sam_error.hpp"
#include "schema_metadata.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace simcoaster::sam {
struct IntegerField {
    const char* group;
    const char* field;
    bool nonnegative;
    bool bounded = false;
    std::int32_t minimum = 0, maximum = 0;

    consteval IntegerField(std::string_view group_name, std::string_view field_name, bool require_nonnegative = false) : group(std::define_static_string(group_name)), field(std::define_static_string(field_name)), nonnegative(require_nonnegative) {}

    consteval IntegerField(std::string_view group_name, std::string_view field_name, std::int32_t lower, std::int32_t upper) : IntegerField(group_name, field_name) {
        if (lower >= upper) {
            throw "SAM integer bounds must form a nonempty half-open range";
        }
        bounded = true;
        minimum = lower;
        maximum = upper;
    }
};

struct Rows {
    const char* group;

    consteval explicit Rows(std::string_view name) : group(std::define_static_string(name)) {}
};

struct Extent {
    const char* group;

    consteval explicit Extent(std::string_view name) : group(std::define_static_string(name)) {}
};

namespace detail {
constexpr bool alpha(char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

constexpr bool digit(char value) {
    return value >= '0' && value <= '9';
}

constexpr bool alnum(char value) {
    return alpha(value) || digit(value);
}

constexpr bool space(char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\v' || value == '\f' || value == '\r';
}

constexpr bool identifier(std::string_view name, bool field) {
    if (name.empty() || (field ? !alpha(name.front()) : !alnum(name.front()))) {
        return false;
    }
    for (char character : name) {
        if (!alnum(character)) {
            return false;
        }
    }
    return true;
}

consteval bool integer_type(std::meta::info type) {
    if (std::meta::is_const(type) || std::meta::is_volatile(type)) {
        return false;
    }
    return type == ^^typename std::int32_t || (std::meta::is_enum_type(type) && std::meta::underlying_type(type) == ^^typename std::int32_t);
}

consteval IntegerField integer_field(std::meta::info member) {
    const auto annotations = std::meta::annotations_of_with_type(member, ^^IntegerField);
    if (annotations.size() != 1 || std::meta::is_bit_field(member) || !integer_type(std::meta::type_of(member))) {
        throw "SAM integer fields need one annotation and mutable int32 or " "int32-backed enum storage";
    }
    const auto field = schema::annotation<IntegerField>(member, "SAM integer fields need one annotation and mutable int32 or " "int32-backed enum storage");
    if (!identifier(field.group, false) || !identifier(field.field, true)) {
        throw "invalid SAM integer field name";
    }
    return field;
}

struct Member {
    std::meta::info member, leaf, extent;
    IntegerField field;
    std::size_t capacity;
    bool indexed;
};

consteval void append(std::vector<Member>& entries, Member entry) {
    for (const auto& previous : entries) {
        if (std::string_view { previous.field.group } == entry.field.group) {
            if (previous.indexed != entry.indexed || (entry.indexed && previous.member != entry.member)) {
                throw "SAM group cannot have multiple scalar/indexed declarations";
            }
            if (std::string_view { previous.field.field } == entry.field.field) {
                throw "duplicate SAM integer field name";
            }
        }
    }
    entries.push_back(entry);
}

template <class T>
consteval auto plan() {
    std::vector<Member> result;
    const auto root_members = std::define_static_array(schema::members_of(^^T, std::meta::access_context::current()));
    for (auto member : root_members) {
        const auto annotations = std::meta::annotations_of_with_type(member, ^^IntegerField);
        const auto rows = std::meta::annotations_of_with_type(member, ^^Rows);
        const auto extents = std::meta::annotations_of_with_type(member, ^^Extent);
        if (annotations.size() + rows.size() + extents.size() > 1) {
            throw "SAM member needs one mapping annotation";
        }
        if (!annotations.empty()) {
            append(result, { member, member, member, integer_field(member), 1, false });
        }
        if (!rows.empty()) {
            const auto group = std::meta::extract<Rows>(std::meta::constant_of(rows.front())).group;
            const auto type = std::meta::type_of(member);
            if (!identifier(group, false) || !std::meta::has_template_arguments(type) || std::meta::template_of(type) != ^^std::array || std::meta::is_const(type) || std::meta::is_volatile(type)) {
                throw "SAM Rows needs a mutable std::array of row records";
            }
            const auto arguments = std::meta::template_arguments_of(type);
            const auto capacity = std::meta::extract<std::size_t>(arguments[1]);
            if (!capacity || capacity > 2147483647u) {
                throw "SAM row capacity must be a positive signed index bound";
            }
            std::meta::info extent {};
            for (auto candidate : root_members) {
                const auto candidate_annotations = std::meta::annotations_of_with_type(candidate, ^^Extent);
                if (candidate_annotations.empty()) {
                    continue;
                }
                const auto candidate_group = std::meta::extract<Extent>(std::meta::constant_of(candidate_annotations.front())).group;
                if (std::string_view { group } != candidate_group) {
                    continue;
                }
                if (extent != std::meta::info {} || candidate_annotations.size() != 1 || std::meta::is_bit_field(candidate) || std::meta::type_of(candidate) != ^^typename std::uint32_t) {
                    throw "SAM Rows needs one mutable uint32 Extent";
                }
                extent = candidate;
            }
            if (extent == std::meta::info {}) {
                throw "SAM Rows needs its matching Extent";
            }
            const auto before = result.size();
            for (auto leaf : std::meta::nonstatic_data_members_of(arguments[0], std::meta::access_context::current())) {
                if (std::meta::annotations_of_with_type(leaf, ^^IntegerField).empty()) {
                    continue;
                }
                const auto field = integer_field(leaf);
                if (std::string_view { field.group } != group) {
                    throw "SAM row field must name its Rows group";
                }
                append(result, { member, leaf, extent, field, capacity, true });
            }
            if (result.size() == before) {
                throw "SAM Rows needs annotated integer leaves";
            }
        }
        if (!extents.empty()) {
            const auto group = std::meta::extract<Extent>(std::meta::constant_of(extents.front())).group;
            bool paired = false;
            for (auto candidate : root_members) {
                const auto candidate_rows = std::meta::annotations_of_with_type(candidate, ^^Rows);
                if (!candidate_rows.empty() && std::string_view { std::meta::extract<Rows>(std::meta::constant_of(candidate_rows.front())).group } == group) {
                    paired = true;
                }
            }
            if (!paired) {
                throw "SAM Extent needs its matching Rows";
            }
        }
    }
    return result;
}
template <class T>
inline constexpr auto members = std::define_static_array(plan<T>());

inline bool integer(std::string_view source, std::size_t& position, std::int32_t& value) {
    while (position < source.size() && space(source[position])) {
        ++position;
    }
    const auto begin = position;
    if (position < source.size() && source[position] == '-') {
        ++position;
    }
    const auto digits = position;
    while (position < source.size() && digit(source[position])) {
        ++position;
    }
    if (position < source.size() && !space(source[position])) {
        return false;
    }
    //Native atoi accepts missing values and a lone minus as zero. Out-of-range
    //conversions are deliberately replaced with an explicit parse failure.
    if (position == digits) {
        value = 0;
        return true;
    }
    const auto result = std::from_chars(source.data() + begin, source.data() + position, value);
    return result.ec == std::errc {} && result.ptr == source.data() + position;
}

//Packed colors accept signed values or the full unsigned decimal range.
//Ordinary signed fields continue to reject values outside their own range.
inline bool integer(std::string_view source, std::size_t& position, std::uint32_t& value) {
    while (position < source.size() && space(source[position])) {
        ++position;
    }
    if (position < source.size() && source[position] == '-') {
        std::int32_t parsed {};
        if (!integer(source, position, parsed)) {
            return false;
        }
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    const auto start = position;
    while (position < source.size() && digit(source[position])) {
        ++position;
    }
    if (position < source.size() && !space(source[position])) {
        return false;
    }
    if (start == position) {
        value = 0;
        return true;
    }
    const auto result = std::from_chars(source.data() + start, source.data() + position, value);
    return result.ec == std::errc {} && result.ptr == source.data() + position;
}
} //namespace detail

//Binding-map initialization and non-preserving parsing share these defaults:
//bounded minima, zero unbounded values, full row capacities, and zero extents.
template <class T>
void reset_integer_record(T& record) {
    template for (constexpr auto entry : detail::members<T>) {
        if constexpr (entry.indexed) {
            for (auto& row : record.[:entry.member:]) {
                using Value = std::remove_reference_t<decltype(row.[:entry.leaf:])>;
                row.[:entry.leaf:] = static_cast<Value>(entry.field.minimum);
            }
            record.[:entry.extent:] = 0;
        } else {
            using Value = std::remove_reference_t<decltype(record.[:entry.member:])>;
            record.[:entry.member:] = static_cast<Value>(entry.field.minimum);
        }
    }
}

//Integer subset of Game.exe0x419CD0: ASCII identifiers, optional row indexes,
//independent dotted fields, and at most16 fields per statement. One level of
//typed row arrays is supported; no float/string/grid grammar is claimed.
//Earlier stores survive a later error. Native fixed scratch buffers are not
//runtime layouts, and only annotated fields participate.
template <class T, bool SelectKnownGroups = false>
std::expected<void, Error> parse_integer_record_checked(T& record, std::string_view text, bool preserve = false) {
    if (!preserve) {
        reset_integer_record(record);
    }
    std::string normalized { text.substr(0, text.find('\0')) };
    std::erase(normalized, '\r');
    const std::string_view source = normalized;
    std::size_t line = 0;
    while (line < source.size()) {
        auto position = line;
        std::string_view error_group, error_field;
        std::optional<std::uint32_t> error_index;
        const auto fail = [&](Failure reason) {
            return std::unexpected(Error { reason, position, std::string { error_group }, std::string { error_field }, error_index });
        };
        while (position < source.size() && detail::space(source[position])) {
            ++position;
        }
        if (position == source.size()) {
            return {};
        }
        if (source[position] != '#') {
            const auto group_start = position;
            while (position < source.size() && detail::alnum(source[position])) {
                ++position;
            }
            if (position == group_start || position == source.size()) {
                return fail(Failure::path);
            }
            const auto group = source.substr(group_start, position - group_start);
            error_group = group;
            if constexpr (SelectKnownGroups) {
                bool known = false;
                template for (constexpr auto entry : detail::members<T>) if (group == entry.field.group) known = true;
                if (!known) {
                    const auto next = source.find('\n', position);
                    line = next == source.npos ? source.size() : next + 1;
                    continue;
                }
            }
            const bool indexed = source[position] == '[';
            std::uint32_t index = 0;
            if (indexed) {
                const auto start = ++position;
                while (position < source.size() && detail::digit(source[position])) {
                    ++position;
                }
                if (position == start || position == source.size() || source[position] != ']') {
                    return fail(Failure::path);
                }
                const auto converted = std::from_chars(source.data() + start, source.data() + position, index);
                if (converted.ec != std::errc {}) {
                    return fail(Failure::path);
                }
                ++position;
                error_index = index;
            }
            if (position == source.size() || source[position++] != '.') {
                return fail(Failure::path);
            }
            std::array<std::string_view, 16> fields {};
            std::size_t field_count = 0;
            while (true) {
                if (field_count == fields.size() || position == source.size() || !detail::alpha(source[position])) {
                    return fail(Failure::path);
                }
                const auto start = position++;
                while (position < source.size() && detail::alnum(source[position])) {
                    ++position;
                }
                fields[field_count++] = source.substr(start, position - start);
                if (position == source.size()) {
                    return fail(Failure::path); //A field path needs its value separator.
                }
                const auto separator = source[position++];
                if (separator == '.') {
                    if (field_count < fields.size()) {
                        continue;
                    }
                    //Native0x419EAE..ED9 stops collecting at exactly16 fields after
                    //consuming this dot; a following dot fails, otherwise values begin.
                    if (position < source.size() && source[position] == '.') {
                        return fail(Failure::path);
                    }
                    break;
                }
                if (!detail::space(separator)) {
                    return fail(Failure::path);
                }
                break;
            }
            for (std::size_t i = 0; i < field_count; ++i) {
                error_field = fields[i];
                bool found = false;
                template for (constexpr auto entry : detail::members<T>) {
                    if (indexed == entry.indexed && group == entry.field.group && fields[i] == entry.field.field) {
                        found = true;
                        std::int32_t value {};
                        if (index >= entry.capacity) {
                            return fail(Failure::index);
                        }
                        const auto value_start = position;
                        if (!detail::integer(source, position, value)) {
                            return fail(Failure::value);
                        }
                        //Native0x41A1C9..D3 raises parsed extent after lexical conversion,
                        //even when the following numeric range test rejects this value.
                        if constexpr (entry.indexed) {
                            if (record.[:entry.extent:] < index + 1) {
                                record.[:entry.extent:] = index + 1;
                            }
                        }
                        if ((entry.field.nonnegative && value < 0) || (entry.field.bounded && (value < entry.field.minimum || value >= entry.field.maximum))) {
                            auto failure = fail(Failure::range);
                            failure.error().value = std::string { source.substr(value_start, position - value_start) };
                            return failure;
                        }
                        if constexpr (entry.indexed) {
                            auto& destination = record.[:entry.member:][index].[:entry.leaf:];
                            destination = static_cast<std::remove_reference_t<decltype(destination)>>(value);
                        } else {
                            using Value = std::remove_reference_t<decltype(record.[:entry.member:])>;
                            record.[:entry.member:] = static_cast<Value>(value);
                        }
                    }
                }
                if (!found) {
                    return fail(Failure::unknown_field);
                }
            }
        }
        //Native0x41A401 resumes at the original line, not the value cursor. Thus
        //trailing separated text is ignored, while cross-LF values can commit
        //before that following line is encountered again and rejected as a path.
        while (line < source.size() && source[line] != '\n') {
            ++line;
        }
        if (line < source.size()) {
            ++line;
        }
    }
    return {};
}

template <class T>
bool parse_integer_record(T& record, std::string_view text, bool preserve = false) {
    return parse_integer_record_checked(record, text, preserve).has_value();
}
} //namespace simcoaster::sam
