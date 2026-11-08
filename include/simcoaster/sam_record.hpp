#pragma once
#include "schema_metadata.hpp"
#include "world_types.hpp"
#include <charconv>
#include <expected>

namespace simcoaster::sam {
enum class Kind {
    integer = 4,
    nonnegative_integer = 5,
    bounded_integer = 6,
    scalar = 7,
    nonnegative_scalar = 8,
    bounded_scalar = 9,
    string = 10,
    grid = 11
};
enum class GridSymbols {
    placement = 1,
    hoarding = 2
};

struct Field {
    const char* name;
    Kind kind;
    std::int32_t minimum = 0, maximum = 0;
    float lower = 0, upper = 0;
    GridSymbols symbols = GridSymbols::placement;

    consteval Field(std::string_view text, Kind type) : name(std::define_static_string(text)), kind(type) {}

    consteval Field(std::string_view text, std::int32_t low, std::int32_t high) : Field(text, Kind::bounded_integer) {
        minimum = low;
        maximum = high;
        if (low >= high) {
            throw "empty SAM integer range";
        }
    }

    consteval Field(std::string_view text, float low, float high) : Field(text, Kind::bounded_scalar) {
        lower = low;
        upper = high;
        if (!(low < high)) {
            throw "empty SAM float range";
        }
    }

    consteval Field(std::string_view text, GridSymbols map) : Field(text, Kind::grid) {
        symbols = map;
    }
};

struct Group {
    const char* name;

    consteval explicit Group(std::string_view text) : name(std::define_static_string(text)) {}
};

struct ParsedGrid {
    std::unique_ptr<DefinitionGrid> value;
    std::size_t next_line;
};

std::expected<ParsedGrid, Error> parse_grid(std::string_view, std::size_t line, GridSymbols);
std::expected<float, Error> parse_scalar(std::string_view, std::size_t&);

namespace record_detail {
struct Binding {
    std::meta::info parent, leaf, extent;
    const char* group;
    Field field;
    std::size_t capacity;
    bool indexed;
};

template <class T>
consteval auto plan() {
    std::vector<Binding> result;
    const auto roots = std::define_static_array(schema::members_of(^^T, std::meta::access_context::current()));
    for (auto parent : roots) {
        const auto group = std::meta::annotations_of_with_type(parent, ^^Group), rows = std::meta::annotations_of_with_type(parent, ^^Rows);
        if (group.empty() && rows.empty()) {
            continue;
        }
        if (group.size() + rows.size() != 1 || std::meta::is_bit_field(parent)) {
            throw "SAM group needs one mapping";
        }
        const bool indexed = !rows.empty();
        const char* name;
        auto row_type = std::meta::type_of(parent);
        std::size_t capacity = 1;
        std::meta::info extent {};
        if (indexed) {
            name = std::meta::extract<Rows>(std::meta::constant_of(rows.front())).group;
            if (!std::meta::has_template_arguments(row_type) || std::meta::template_of(row_type) != ^^std::array) {
                throw "SAM Rows requires std::array";
            }
            const auto args = std::meta::template_arguments_of(row_type);
            row_type = args[0];
            capacity = std::meta::extract<std::size_t>(args[1]);
            if (!capacity || capacity > 2147483647u) {
                throw "SAM capacity must fit a positive signed index";
            }
            for (auto candidate : roots) {
                const auto extents = std::meta::annotations_of_with_type(candidate, ^^Extent);
                if (extents.empty()) {
                    continue;
                }
                if (std::string_view { std::meta::extract<Extent>(std::meta::constant_of(extents.front())).group } != name) {
                    continue;
                }
                if (extent != std::meta::info {} || extents.size() != 1 || std::meta::type_of(candidate) != ^^typename std::uint32_t) {
                    throw "SAM group needs one uint32 extent";
                }
                extent = candidate;
            }
            if (extent == std::meta::info {}) {
                throw "SAM Rows needs its extent";
            }
        } else {
            name = std::meta::extract<Group>(std::meta::constant_of(group.front())).name;
        }
        if (!detail::identifier(name, false)) {
            throw "invalid SAM group name";
        }
        for (auto leaf : std::meta::nonstatic_data_members_of(row_type, std::meta::access_context::current())) {
            const auto fields = std::meta::annotations_of_with_type(leaf, ^^Field);
            if (fields.empty()) {
                continue;
            }
            if (fields.size() != 1 || std::meta::is_bit_field(leaf)) {
                throw "SAM value needs one Field";
            }
            const auto field = schema::annotation<Field>(leaf, "SAM value needs one Field");
            const auto type = std::meta::type_of(leaf);
            if (!detail::identifier(field.name, true)) {
                throw "invalid SAM field name";
            }
            const bool integer = field.kind == Kind::integer || field.kind == Kind::nonnegative_integer || field.kind == Kind::bounded_integer;
            const bool scalar = field.kind == Kind::scalar || field.kind == Kind::nonnegative_scalar || field.kind == Kind::bounded_scalar;
            if ((integer && (type != ^^typename std::int32_t)&&(type != ^^typename std::uint32_t)) || (scalar && type != ^^float) || (field.kind == Kind::string && type != ^^std::optional<std::string>) || (field.kind == Kind::grid && type != ^^std::unique_ptr<DefinitionGrid>)) {
                throw "SAM field type disagrees with its descriptor";
            }
            for (const auto& previous : result) {
                if (std::string_view { previous.group } == name && std::string_view { previous.field.name } == field.name) {
                    throw "duplicate SAM field";
                }
            }
            result.push_back({ parent, leaf, extent, name, field, capacity, indexed });
        }
    }
    return result;
}
template <class T>
inline constexpr auto bindings = std::define_static_array(plan<T>());

template <class T>
consteval auto group_names() {
    std::vector<const char*> result;
    for (const auto& binding : bindings<T>) {
        if (std::ranges::none_of(result, [&](const char* group) {
                return std::string_view { group } == binding.group;
            })) {
            result.push_back(binding.group);
        }
    }
    return result;
}
template <class T>
inline constexpr auto groups = std::define_static_array(group_names<T>());

template <auto Parent, auto Leaf, bool Indexed, class T>
decltype(auto) value(T& record, std::size_t index) {
    if constexpr (Indexed) {
        return schema::member<Leaf>(schema::member<Parent>(record)[index]);
    } else {
        return schema::member<Leaf>(schema::member<Parent>(record));
    }
}

template <class Value>
std::expected<void, Error> parse_value(Value& destination, const Field& field, std::string_view text, std::size_t& cursor, std::size_t& line, std::uint32_t* extent, std::uint32_t index) {
    const auto raise_extent = [&] {
        if (extent && *extent < index + 1) {
            *extent = index + 1;
        }
    };
    if constexpr (std::same_as<Value, std::int32_t> || std::same_as<Value, std::uint32_t>) {
        Value parsed {};
        if (!detail::integer(text, cursor, parsed)) {
            return std::unexpected(Error { Failure::value, cursor });
        }
        raise_extent();
        const auto checked = static_cast<std::int64_t>(parsed);
        if ((field.kind == Kind::nonnegative_integer && checked < 0) || (field.kind == Kind::bounded_integer && (checked < field.minimum || checked >= field.maximum))) {
            return std::unexpected(Error { Failure::range, cursor });
        }
        destination = parsed;
    } else if constexpr (std::same_as<Value, float>) {
        const auto parsed = parse_scalar(text, cursor);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        raise_extent();
        if ((field.kind == Kind::nonnegative_scalar && *parsed < 0) || (field.kind == Kind::bounded_scalar && (*parsed < field.lower || *parsed >= field.upper))) {
            return std::unexpected(Error { Failure::range, cursor });
        }
        destination = *parsed;
    } else if constexpr (std::same_as<Value, std::optional<std::string>>) {
        while (cursor < text.size() && detail::space(text[cursor])) {
            ++cursor;
        }
        if (cursor == text.size() || text[cursor++] != '"') {
            return std::unexpected(Error { Failure::value, cursor });
        }
        const auto start = cursor;
        while (cursor < text.size() && text[cursor] != '"' && text[cursor] != '\n') {
            ++cursor;
        }
        if (cursor == text.size() || text[cursor] != '"') {
            return std::unexpected(Error { Failure::value, cursor });
        }
        destination = std::string { text.substr(start, cursor - start) };
        ++cursor;
        raise_extent();
    } else {
        auto parsed = parse_grid(text, line, field.symbols);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        destination = std::move(parsed->value);
        line = parsed->next_line;
        raise_extent();
    }
    return {};
}
} //namespace record_detail

template <class T>
void reset_record(T& record) {
    template for (constexpr auto binding : record_detail::bindings<T>) {
        for (std::size_t i = 0; i < binding.capacity; ++i) {
            auto& destination = record_detail::value<binding.parent, binding.leaf, binding.indexed>(record, i);
            if constexpr (binding.field.kind == Kind::bounded_integer) {
                destination = binding.field.minimum;
            } else if constexpr (binding.field.kind == Kind::bounded_scalar) {
                destination = binding.field.lower;
            } else {
                destination = {};
            }
        }
        if constexpr (binding.indexed) {
            record.[:binding.extent:] = 0;
        }
    }
}

//Fields are discovered once at compile time. Names and descriptor rules stay
//explicit metadata; no native field pointers, four-byte slots or vtables enter
//the host model. Failed parses retain earlier writes, as the original does.
//SelectKnownGroups projects scalar/string groups from a larger balance file;
//owned groups still receive strict field/index/range validation. Grid values
//need their complete owning record and cannot use this projection mode.
template <class T, bool SelectKnownGroups = false>
std::expected<void, Error> parse_record(T& record, std::string_view text, bool preserve = false) {
    static_assert(!SelectKnownGroups || std::ranges::none_of(record_detail::bindings<T>, [](const auto& binding) {
        return binding.field.kind == Kind::grid;
    }));
    if (!preserve) {
        reset_record(record);
    }
    std::string normalized { text.substr(0, text.find('\0')) };
    std::erase(normalized, '\r');
    const std::string_view source = normalized;
    std::size_t line = 0;
    while (line < source.size()) {
        auto cursor = line;
        while (cursor < source.size() && detail::space(source[cursor])) {
            ++cursor;
        }
        if (cursor == source.size()) {
            return {};
        }
        if (source[cursor] != '#') {
            if constexpr (SelectKnownGroups) {
                auto end = cursor;
                while (end < source.size() && detail::alnum(source[end])) {
                    ++end;
                }
                const auto root = source.substr(cursor, end - cursor);
                if (std::ranges::none_of(record_detail::groups<T>, [&](const char* group) {
                        return root == group;
                    })) {
                    const auto next = source.find('\n', line);
                    line = next == std::string_view::npos ? source.size() : next + 1;
                    continue;
                }
            }
            const auto start = cursor;
            while (cursor < source.size() && detail::alnum(source[cursor])) {
                ++cursor;
            }
            if (cursor == start || cursor == source.size()) {
                return std::unexpected(Error { Failure::path, cursor });
            }
            const auto group = source.substr(start, cursor - start);
            const bool indexed = source[cursor] == '[';
            std::uint32_t index = 0;
            if (indexed) {
                const auto first = ++cursor;
                while (cursor < source.size() && detail::digit(source[cursor])) {
                    ++cursor;
                }
                if (cursor == first || cursor == source.size() || source[cursor] != ']') {
                    return std::unexpected(Error { Failure::path, cursor });
                }
                if (std::from_chars(source.data() + first, source.data() + cursor, index).ec != std::errc {}) {
                    return std::unexpected(Error { Failure::index, first });
                }
                ++cursor;
            }
            if (cursor == source.size() || source[cursor++] != '.') {
                return std::unexpected(Error { Failure::path, cursor });
            }
            std::array<std::string_view, 16> fields {};
            std::size_t count = 0;
            for (;;) {
                if (count == fields.size() || cursor == source.size() || !detail::alpha(source[cursor])) {
                    return std::unexpected(Error { Failure::path, cursor });
                }
                const auto first = cursor++;
                while (cursor < source.size() && detail::alnum(source[cursor])) {
                    ++cursor;
                }
                fields[count++] = source.substr(first, cursor - first);
                if (cursor == source.size()) {
                    return std::unexpected(Error { Failure::path, cursor });
                }
                const auto separator = source[cursor++];
                if (separator == '.') {
                    if (count < fields.size()) {
                        continue;
                    }
                    if (cursor < source.size() && source[cursor] == '.') {
                        return std::unexpected(Error { Failure::path, cursor });
                    }
                    break;
                }
                if (!detail::space(separator)) {
                    return std::unexpected(Error { Failure::path, cursor });
                }
                break;
            }
            for (std::size_t i = 0; i < count; ++i) {
                const auto value_start = cursor;
                bool found = false;
                std::expected<void, Error> parsed;
                template for (constexpr auto binding : record_detail::bindings<T>) {
                    constexpr auto field = binding.field;
                    if (group == binding.group && fields[i] == binding.field.name && indexed == binding.indexed) {
                        found = true;
                        if (index >= binding.capacity) {
                            return std::unexpected(Error { Failure::index, cursor, std::string { group }, std::string { fields[i] }, index });
                        }
                        if constexpr (binding.field.kind == Kind::grid) {
                            if (count != 1) {
                                return std::unexpected(Error { Failure::map_fields, cursor, std::string { group }, std::string { fields[i] }, index });
                            }
                        }
                        std::uint32_t* extent = nullptr;
                        if constexpr (binding.indexed) {
                            extent = &record.[:binding.extent:];
                        }
                        parsed = record_detail::parse_value(record_detail::value<binding.parent, binding.leaf, binding.indexed>(record, index), field, source, cursor, line, extent, index);
                    }
                }
                if (!found) {
                    return std::unexpected(Error { Failure::unknown_field, cursor, std::string { group }, std::string { fields[i] }, indexed ? std::optional { index } : std::nullopt });
                }
                if (!parsed) {
                    auto& error = parsed.error();
                    error.group = group;
                    error.field = fields[i];
                    if (indexed) {
                        error.index = index;
                    }
                    const auto end = source.find('\n', value_start);
                    error.value = std::string { source.substr(value_start, std::min<std::size_t>((end == source.npos ? source.size() : end) - value_start, 160)) };
                    return parsed;
                }
            }
        }
        //Grid parsing advances line past its closing marker; native outer parsing
        //then consumes the following line too. Ordinary values resume at line.
        while (line < source.size() && source[line] != '\n') {
            ++line;
        }
        if (line < source.size()) {
            ++line;
        }
    }
    return {};
}

template <class T, class Visitor>
void visit_record(const T& record, Visitor&& visitor) {
    template for (constexpr auto binding : record_detail::bindings<T>) {
        constexpr auto field = binding.field;
        for (std::size_t i = 0; i < binding.capacity; ++i) {
            std::invoke(visitor, std::string_view { binding.group }, field, i, record_detail::value<binding.parent, binding.leaf, binding.indexed>(record, i));
        }
        if constexpr (binding.indexed) {
            std::invoke(visitor, std::string_view { binding.group }, record.[:binding.extent:]);
        }
    }
}
} //namespace simcoaster::sam
