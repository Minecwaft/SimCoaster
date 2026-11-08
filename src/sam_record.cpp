#include "simcoaster/sam_record.hpp"

namespace simcoaster::sam {
std::expected<float, Error> parse_scalar(std::string_view text, std::size_t& cursor) {
    while (cursor < text.size() && detail::space(text[cursor])) {
        ++cursor;
    }
    const auto start = cursor;
    if (cursor < text.size() && text[cursor] == '-') {
        ++cursor;
    }
    unsigned dots = 0, digits = 0;
    while (cursor < text.size() && (detail::digit(text[cursor]) || (text[cursor] == '.' && dots < 2))) {
        if (text[cursor] == '.') {
            ++dots;
        } else {
            ++digits;
        }
        ++cursor;
    }
    if ((cursor < text.size() && !detail::space(text[cursor])) || dots > 1 || cursor - start >= 100) {
        return std::unexpected(Error { Failure::value, cursor });
    }
    if (!digits) {
        return 0.0f;
    }
    double value {};
    const auto parsed = std::from_chars(text.data() + start, text.data() + cursor, value);
    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + cursor) {
        return std::unexpected(Error { Failure::value, start });
    }
    return static_cast<float>(value);
}

std::expected<ParsedGrid, Error> parse_grid(std::string_view text, std::size_t line, GridSymbols symbols) {
    struct Symbol {
        char character;
        std::int32_t code;
        std::uint8_t attribute;
    };

    constexpr std::array placement { Symbol { '.', 0, 0 }, Symbol { '*', 4, 0 },  Symbol { 'Q', 3, 0 },  Symbol { '@', 1, 0 },  Symbol { '+', 11, 0 }, Symbol { '#', 16, 0 },  Symbol { 'H', 8, 0 },   Symbol { 'X', 5, 0 },  Symbol { ',', 30, 0 }, Symbol { '=', 32, 0 },  Symbol { '%', 33, 0 }, Symbol { '8', 9, 1 },
                                     Symbol { '6', 9, 4 }, Symbol { '2', 9, 16 }, Symbol { '4', 9, 64 }, Symbol { 'N', 10, 1 }, Symbol { 'E', 10, 4 }, Symbol { 'S', 10, 16 }, Symbol { 'W', 10, 64 }, Symbol { 'D', 23, 0 }, Symbol { '>', 23, 4 }, Symbol { '<', 23, 64 }, Symbol { 'O', 27, 4 }, Symbol { 'I', 27, 64 } };
    constexpr std::array hoarding { Symbol { '.', 0, 0 }, Symbol { '^', 0, 1 }, Symbol { '_', 0, 16 }, Symbol { '[', 0, 64 }, Symbol { ']', 0, 4 }, Symbol { 'J', 0, 20 }, Symbol { 'F', 0, 65 }, Symbol { '7', 0, 5 }, Symbol { 'L', 0, 80 }, Symbol { '=', 0, 17 }, Symbol { 'H', 0, 68 }, Symbol { 'C', 0, 81 }, Symbol { 'U', 0, 84 }, Symbol { 'n', 0, 69 }, Symbol { '3', 0, 21 }, Symbol { 'O', 0, 85 } };
    const auto table = symbols == GridSymbols::placement ? std::span<const Symbol> { placement } : std::span<const Symbol> { hoarding };
    while (line < text.size() && detail::space(text[line])) {
        ++line;
    }
    auto end = text.find('\n', line);
    if (end == std::string_view::npos) {
        return std::unexpected(Error { Failure::map_description, line });
    }
    line = end + 1;
    if (!text.substr(line).starts_with("---")) {
        return std::unexpected(Error { Failure::map_marker, line });
    }
    end = text.find('\n', line);
    line = end == std::string_view::npos ? text.size() : end + 1;
    auto grid = std::make_unique<DefinitionGrid>();
    for (;;) {
        if (text.substr(line).starts_with("---")) {
            end = text.find('\n', line);
            line = end == std::string_view::npos ? text.size() : end + 1;
            break;
        }
        if (line == text.size()) {
            return std::unexpected(Error { Failure::map_marker, line });
        }
        if (grid->height >= 40) {
            return std::unexpected(Error { Failure::map_height, line });
        }
        end = text.find('\n', line);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::int32_t width = 0;
        for (auto cursor = line; cursor < end; ++cursor) {
            if (text[cursor] != ' ') {
                const auto symbol = std::ranges::find(table, text[cursor], &Symbol::character);
                if (symbol == table.end()) {
                    return std::unexpected(Error { Failure::map_symbol, cursor });
                }
                if (width == 40) {
                    return std::unexpected(Error { Failure::map_width, cursor });
                }
                grid->cells[static_cast<std::size_t>(grid->height * 40 + width)] = { symbol->code, symbol->attribute };
                ++width;
            }
        }
        grid->width = std::max(grid->width, width);
        ++grid->height;
        line = end == text.size() ? end : end + 1;
    }
    for (std::int32_t row = 0; row < grid->height / 2; ++row) {
        for (std::int32_t column = 0; column < 40; ++column) {
            std::swap(grid->cells[static_cast<std::size_t>(row * 40 + column)], grid->cells[static_cast<std::size_t>((grid->height - 1 - row) * 40 + column)]);
        }
    }
    return ParsedGrid { std::move(grid), line };
}
} //namespace simcoaster::sam
