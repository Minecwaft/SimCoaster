#include "simcoaster/object_catalog.hpp"

#include <algorithm>

namespace simcoaster {
ConfiguredObjectText parse_configured_object_definition_text(std::span<const std::byte> bytes) {
    ConfiguredObjectText result;
    if (bytes.size() % 2)
        return result; //Native installs defaults before the size check.
    std::u16string text;
    text.reserve(bytes.size() / 2 + 1);
    for (std::size_t i = 0; i < bytes.size(); i += 2)
        text.push_back(static_cast<char16_t>(static_cast<std::uint16_t>(bytes[i])
                                             | (static_cast<std::uint16_t>(bytes[i + 1]) << 8)));
    text.push_back(0);
    std::size_t cursor = text[0] == u'\uFEFF' ? 1u : 0u;
    const auto line_end = [&](std::size_t at) {
        while (text[at] && text[at] != u'\r' && text[at] != u'\n')
            ++at;
        return at;
    };
    const auto next_line = [&](std::size_t at) {
        if (!text[at])
            return at;
        const auto first = text[at++];
        if ((first == u'\n' && text[at] == u'\r') || (first == u'\r' && text[at] == u'\n'))
            ++at;
        return at;
    };
    const auto next_nonblank = [&](std::size_t at) {
        for (;;) {
            const auto end = line_end(at);
            auto scan = at;
            while (scan < end && (text[scan] == u' ' || text[scan] == u'\t'))
                ++scan;
            if (scan != end || !text[end])
                return scan == end ? end : at;
            at = next_line(end);
        }
    };
    constexpr std::array<std::u16string_view, 3> keys{ u"NAME", u"SIGNA", u"SIGNB" };
    while (text[cursor]) {
        auto key_end = cursor;
        while (text[key_end]
               && text[key_end] != u' '
               && text[key_end] != u'\t'
               && text[key_end] != u'\r'
               && text[key_end] != u'\n')
            ++key_end;
        const auto key = std::u16string_view{ text }.substr(cursor, key_end - cursor);
        const auto found = std::ranges::find(keys, key);
        if (found == keys.end())
            break;
        cursor = next_nonblank(next_line(line_end(cursor)));
        const auto end = line_end(cursor);
        result.fields[static_cast<std::size_t>(found - keys.begin())] = text.substr(cursor, end - cursor);
        cursor = next_nonblank(next_line(end));
    }
    return result;
}
} //namespace simcoaster
