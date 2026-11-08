#include "simcoaster/ui_font.hpp"
#include "simcoaster/bfst_strings.hpp"
#include <format>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
//Native896DF0/896FC0/89B460. These are disk records, not x86 objects.
struct FontHeader {
    [[=save::Field { "signature", 0 }]] std::array<char, 4> magic {};
    [[=save::Field { "format_byte", 1 }]] std::uint8_t format {};
    [[=save::Field { "line_advance", 2 }]] std::uint8_t line_advance {};
    [[=save::Field { "glyph_count", 3 }]] std::uint16_t count {};
};

struct GlyphHeader {
    [[=save::Field { "character_code", 0 }]] std::uint32_t character {};
    [[=save::Field { "stored_payload_bytes", 1 }]] std::uint32_t stored {};
    [[=save::Field { "decoded_coverage_bytes", 2 }]] std::uint32_t decoded {};
    [[=save::Field { "encoding_flags", 3 }]] std::uint8_t flags {};
    [[=save::Field { "opaque_0D", 4 }]] std::array<std::uint8_t, 3> opaque {};
    [[=save::Field { "width", 5 }]] std::uint16_t width {};
    [[=save::Field { "height", 6 }]] std::uint16_t height {};
    [[=save::Field { "bearing_x", 7 }]] std::int8_t x {};
    [[=save::Field { "bearing_y", 8 }]] std::int8_t y {};
    [[=save::Field { "horizontal_advance", 9 }]] std::uint16_t advance {};
};

struct IndexRecord {
    [[=save::Field { "resource_id", 0 }]] std::uint32_t id {};
    [[=save::Field { "resource_type", 1 }]] std::uint16_t type {};
    [[=save::Field { "path_offset", 2 }]] std::uint16_t path {};
    [[=save::Field { "cached_object", 3 }]] std::uint32_t cached {};
};

static_assert(save::value_width<FontHeader> == 8);
static_assert(save::value_width<GlyphHeader> == 24);
static_assert(save::value_width<IndexRecord> == 12);

void require(bool condition, std::size_t offset, const char* reason) {
    if (!condition) {
        throw UiFontError { offset, {}, reason };
    }
}

template <class T>
T at(std::span<const std::byte> data, std::size_t offset) {
    constexpr auto size = save::value_width<T>;
    require(offset <= data.size() && size <= data.size() - offset, offset, "truncated font resource");
    T value {};
    save::detail::decode(std::span<const std::byte, size> { data.data() + offset, size }, value);
    return value;
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw UiFontError { 0, path.native_encoded_string(), "cannot open font resource" };
    }
    const auto size = input.tellg();
    if (size < 0) {
        throw UiFontError { 0, path.native_encoded_string(), "cannot size font resource" };
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw UiFontError { 0, path.native_encoded_string(), "cannot read font resource" };
    }
    return bytes;
}

std::string folded(std::string value) {
    for (auto& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch + ('a' - 'A'));
        }
    }
    return value;
}

struct Nibbles {
    std::span<const std::byte> bytes;
    std::size_t cursor {};

    std::uint8_t read() {
        require(cursor / 2 < bytes.size(), cursor / 2, "truncated glyph nibble stream");
        const auto value = static_cast<std::uint8_t>(bytes[cursor / 2]);
        return static_cast<std::uint8_t>((cursor++ & 1u) ? value & 15u : value >> 4);
    }
};

std::vector<std::uint8_t> coverage(std::span<const std::byte> data, const GlyphHeader& header) {
    const auto pixels = static_cast<std::size_t>(header.width) * header.height;
    std::vector<std::uint8_t> result;
    result.reserve(pixels);
    if (header.flags & 1u) {
        //High-nibble first. Nonzero is a literal;0,count,value repeats count
        //times;0,0 terminates. Native89B460 has no input/output bounds checks.
        Nibbles input { data };
        std::size_t written {};
        const auto emit = [&](std::uint8_t value, std::size_t count) {
            require(count <= static_cast<std::size_t>(header.decoded) * 2 - written, input.cursor / 2, "glyph RLE output exceeds declared extent");
            const auto visible = std::min(count, pixels - result.size());
            result.insert(result.end(), visible, value);
            written += count;
        };
        for (;;) {
            const auto literal = input.read();
            if (literal) {
                emit(literal, 1);
            } else {
                const auto count = input.read();
                if (!count) {
                    break;
                }
                const auto value = input.read();
                emit(value, count);
            }
        }
        require(written >= pixels, input.cursor / 2, "short glyph RLE output");
    }
    if (header.flags & 2u) {
        //Native's second pass reads the ORIGINAL payload, replacing any RLE
        //output when both bits are set. Bits are consumed MSB first.
        const auto bits = static_cast<std::size_t>(header.decoded) * 2;
        require((bits + 7) / 8 <= data.size(), 0, "short1-bit glyph coverage");
        result.resize(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            result[i] = (static_cast<std::uint8_t>(data[i / 8]) & (128u >> (i & 7u))) ? 15 : 0;
        }
    } else if (!(header.flags & 1u)) {
        require((pixels + 1) / 2 <= data.size(), 0, "short raw glyph coverage");
        result.resize(pixels);
        Nibbles input { data };
        for (auto& value : result) {
            value = input.read();
        }
    }
    result.resize(pixels);
    return result;
}

//Native4468E0 resource-ID table, resolved through each locale's ResIdx.dat.
//Do not replace resolution tiers with a nearest-size font heuristic.
constexpr std::array<std::array<std::uint32_t, 14>, 5> font_ids { { { 0x425, 0x41F, 0x420, 0x428, 0x40F, 0x41C, 0x40D, 0x40F, 0x40D, 0x40D, 0x41A, 0x40F, 0x42C, 0x42D },
                                                                    { 0x426, 0x421, 0x422, 0x429, 0x417, 0x41D, 0x410, 0x412, 0x413, 0x410, 0x41A, 0x410, 0x42C, 0x42D },
                                                                    { 0x427, 0x423, 0x424, 0x42B, 0x415, 0x41E, 0x412, 0x414, 0x415, 0x412, 0x41A, 0x412, 0x42C, 0x42E },
                                                                    { 0x427, 0x423, 0x424, 0x42B, 0x419, 0x41E, 0x414, 0x416, 0x419, 0x414, 0x41A, 0x414, 0x42C, 0x42F },
                                                                    { 0x427, 0x423, 0x424, 0x42B, 0x419, 0x41E, 0x414, 0x416, 0x419, 0x414, 0x41A, 0x414, 0x42C, 0x42F } } };

struct Character {
    char16_t code {};
    std::uint16_t glyph {};
    std::uint8_t flags = 1;
    std::uint32_t advance {};
    std::size_t logical {};
};

struct Line {
    std::size_t begin {}, end {};
    float width {};
};

struct Layout {
    UiTextExtent extent;
    std::vector<Character> characters;
    std::vector<Line> lines;
    std::vector<std::array<float, 2>> visual_positions;
};

bool rtl(const Character& c) {
    return c.code > 0x05B0u && c.code < 0x05EBu;
}

bool separator(const Character& c) {
    return c.code == u' ' || c.code == u',';
}

unsigned parenthesis(const Character& c) {
    return c.code == u'(' ? 1u : c.code == u')' ? 2u : 0u;
}

void reorder(std::vector<Character>& characters, const Line& line) {
    auto begin = characters.begin() + static_cast<std::ptrdiff_t>(line.begin);
    const auto end = characters.begin() + static_cast<std::ptrdiff_t>(line.end);
    if (!std::any_of(begin, end, rtl)) {
        return;
    }
    //Native894D70's deliberately limited RTL algorithm, including its
    //one-record suffix guard. This is not a general Unicode bidi replacement.
    std::reverse(begin, end);
    while (end - begin >= 2) {
        while (begin != end && separator(*begin)) {
            ++begin;
        }
        if (begin == end) {
            break;
        }
        auto stop = std::find_if(begin, end, separator);
        if (!std::any_of(begin, stop, rtl)) {
            std::reverse(begin, stop);
        } else if (const auto initial = parenthesis(*begin)) {
            int balance {};
            for (auto at = begin; at != stop; ++at) {
                const auto kind = parenthesis(*at);
                if (kind == initial) {
                    ++balance;
                } else if (kind) {
                    --balance;
                }
                if (!balance) {
                    std::iter_swap(begin, at);
                    break;
                }
            }
        }
        begin = stop;
    }
}
} //namespace

std::expected<UiBitmapFont, UiFontError> parse_ui_bitmap_font(std::span<const std::byte> bytes, std::string name) {
    try {
        const auto header = at<FontHeader>(bytes, 0);
        require(header.magic == std::array { 'F', '4', 'F', 'B' }, 0, "expected F4FB font");
        require(header.count <= (bytes.size() - 8) / 4, 6, "font glyph directory exceeds resource");
        UiBitmapFont font;
        font.name = name;
        font.line_advance = header.line_advance;
        font.glyphs.reserve(header.count);
        std::vector<std::vector<std::uint8_t>> glyph_pixels;
        glyph_pixels.reserve(header.count);
        std::size_t total_pixels {};
        for (std::size_t i = 0; i < header.count; ++i) {
            const auto offset = at<std::uint32_t>(bytes, 8 + i * 4);
            require(offset >= 8 + static_cast<std::size_t>(header.count) * 4, 8 + i * 4, "font glyph overlaps offset directory");
            const auto glyph = at<GlyphHeader>(bytes, offset);
            const auto data = static_cast<std::size_t>(offset) + 24;
            require(data <= bytes.size() && glyph.stored <= bytes.size() - data, data, "font glyph payload exceeds resource");
            //The bitmap renderer has finite texture dimensions. The shipped maximum
            //glyph widths reach59px and heights69px; hostile metadata must not
            //request a multi-gigabyte atlas.
            require(glyph.width <= 1022 && glyph.height <= 16382, offset + 16, "glyph exceeds UI texture dimensions");
            require(glyph.decoded >= (static_cast<std::size_t>(glyph.width) * glyph.height + 1) / 2 && glyph.decoded <= 16u * 1024u * 1024u, offset + 8, "glyph coverage extent disagrees with dimensions or texture budget");
            total_pixels += static_cast<std::size_t>(glyph.width) * glyph.height;
            require(total_pixels <= 16u * 1024u * 1024u, offset, "font exceeds UI texture budget");
            try {
                glyph_pixels.push_back(coverage(bytes.subspan(data, glyph.stored), glyph));
            } catch (UiFontError& error) {
                error.offset += data;
                throw;
            }
            font.glyphs.push_back({ glyph.character, glyph.width, glyph.height, glyph.advance, glyph.x, glyph.y, {}, {} });
        }
        constexpr std::uint32_t atlas_width = 1024;
        std::uint32_t x = 1, y = 1, row_height {};
        std::vector<std::array<std::uint32_t, 2>> locations;
        locations.reserve(font.glyphs.size());
        for (const auto& glyph : font.glyphs) {
            if (x + glyph.width + 1 > atlas_width) {
                x = 1;
                y += row_height + 2;
                row_height = 0;
            }
            locations.push_back({ x, y });
            x += glyph.width + 2;
            row_height = std::max(row_height, static_cast<std::uint32_t>(glyph.height));
        }
        require(y + row_height + 1 <= 16384, 0, "font atlas exceeds UI texture height");
        std::uint32_t atlas_height = 1;
        while (atlas_height < y + row_height + 1) {
            atlas_height *= 2;
        }
        auto atlas = std::make_shared<TextureImage>();
        atlas->width = atlas_width;
        atlas->height = atlas_height;
        atlas->alpha = TextureAlpha::translucent;
        atlas->pixels.resize(static_cast<std::size_t>(atlas_width) * atlas_height, { 255, 255, 255, 0 });
        for (std::size_t i = 0; i < font.glyphs.size(); ++i) {
            auto& glyph = font.glyphs[i];
            const auto place = locations[i];
            for (std::size_t row = 0; row < glyph.height; ++row) {
                for (std::size_t col = 0; col < glyph.width; ++col) {
                    atlas->pixels[(place[1] + row) * atlas_width + place[0] + col][3] = static_cast<std::uint8_t>(glyph_pixels[i][row * glyph.width + col] * 17u);
                }
            }
            glyph.image = atlas;
            glyph.uv = { static_cast<float>(place[0]) / static_cast<float>(atlas_width), static_cast<float>(place[1]) / static_cast<float>(atlas_height), static_cast<float>(place[0] + glyph.width) / static_cast<float>(atlas_width), static_cast<float>(place[1] + glyph.height) / static_cast<float>(atlas_height) };
        }
        return font;
    } catch (UiFontError& error) {
        error.resource = std::move(name);
        return std::unexpected(std::move(error));
    }
}

struct UiFontLibrary::State {
    std::filesystem::path directory;
    MbToUnicodeMap decoder;
    std::vector<std::byte> encoder;
    std::map<std::uint32_t, std::string> resources;
    std::map<std::string, std::shared_ptr<const UiBitmapFont>, std::less<>> fonts;
    std::array<std::uint16_t, 65536> glyph_indices {};
    std::array<bool, 65536> mapped {};

    std::uint16_t encoded(char16_t code) const {
        const auto payload = std::span { encoder }.subspan(4);
        std::size_t offset {};
        for (std::size_t steps = 0; steps <= payload.size() / 8; ++steps) {
            const auto first = at<std::uint16_t>(payload, offset), last = at<std::uint16_t>(payload, offset + 4);
            require(first <= last, offset, "reversed Unicode conversion range");
            if (code >= first && code <= last) {
                return at<std::uint16_t>(payload, offset + 8 + static_cast<std::size_t>(code - first) * 2);
            }
            const auto delta = at<std::uint16_t>(payload, offset + (code < first ? 2 : 6));
            if (!delta) {
                return 0;
            }
            require(delta <= payload.size() - offset, offset, "Unicode conversion branch exceeds resource");
            offset += delta;
        }
        throw UiFontError { offset, {}, "Unicode conversion tree did not terminate" };
    }

    Layout layout(const UiBitmapFont& font, std::u16string_view text, const std::array<float, 4>& bounds, const UiTextStyle& style) const {
        require(std::ranges::all_of(bounds,
                                    [](float v) {
                                        return std::isfinite(v);
                                    })
                    && std::isfinite(style.scale[0])
                    && style.scale[0] > 0
                    && std::isfinite(style.scale[1])
                    && style.scale[1] > 0
                    && (!style.depth || std::isfinite(*style.depth)),
                0,
                "invalid UI text geometry");
        require(bounds[2] >= bounds[0] && bounds[3] >= bounds[1], 0, "reversed UI text bounds");
        require(style.horizontal <= UiTextHorizontal::right && style.vertical <= UiTextVertical::bottom, 0, "invalid UI text alignment");
        text = text.substr(0, text.find(char16_t {}));
        Layout result;
        result.extent.positions.resize(text.size() + 1);
        result.characters.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto code = text[i];
            Character c { code, 0, 1, 0, i };
            if (code == u'\n') {
                c.flags = 4;
            } else if (code == u'\r') {
                c.flags = 12;
            } else {
                require(mapped[code] && glyph_indices[code] < font.glyphs.size(), i, "UTF16 character has no font glyph mapping");
                c.glyph = glyph_indices[code];
                c.advance = font.glyphs[c.glyph].advance;
                if (code == u'\t' || code == u' ') {
                    c.flags = 18;
                }
            }
            result.characters.push_back(c);
        }
        auto& chars = result.characters;
        const auto width = style.wrap ? (bounds[2] - bounds[0]) / style.scale[0] : std::numeric_limits<float>::infinity();
        const auto add_line = [&](std::size_t begin, std::size_t mark, std::size_t end, float used) {
            for (auto i = mark; i < end; ++i) {
                chars[i].flags |= 64;
            }
            result.lines.push_back({ begin, end, used });
        };
        const auto wrap = [&](std::size_t begin, std::size_t end) {
            if (begin == end) {
                add_line(begin, end, end, 0);
                return;
            }
            auto start = begin, at_index = begin;
            float used {};
            while (at_index < end) {
                auto& c = chars[at_index];
                if (!(c.flags & 4u)) {
                    const auto sum = used + static_cast<float>(c.advance);
                    if (sum <= width) {
                        used = sum;
                    } else {
                        auto visible_end = at_index;
                        auto line_width = sum;
                        if (at_index == start) {
                            visible_end = ++at_index;
                        } else {
                            auto candidate = at_index;
                            line_width = used;
                            do {
                                if (chars[candidate].flags & 16u) {
                                    break;
                                }
                                --candidate;
                                line_width -= static_cast<float>(chars[candidate].advance);
                            } while (candidate != start);
                            if (candidate == start) {
                                visible_end = at_index;
                                line_width = used;
                            } else {
                                at_index = candidate;
                                auto previous = candidate - 1;
                                while (previous != start && (chars[previous].flags & 2u)) {
                                    line_width -= static_cast<float>(chars[previous].advance);
                                    --previous;
                                }
                                visible_end = previous + 1;
                            }
                        }
                        while (at_index < end && (chars[at_index].flags & 2u)) {
                            ++at_index;
                        }
                        add_line(start, visible_end, at_index, line_width);
                        start = at_index;
                        used = 0;
                        continue;
                    }
                }
                ++at_index;
            }
            if (start != end) {
                add_line(start, end, end, used);
            }
        };
        std::size_t start {}, cursor {};
        while (cursor < chars.size()) {
            if (chars[cursor].flags & 4u) {
                ++cursor;
                if (cursor < chars.size() && (chars[cursor].flags & 4u) && (chars[cursor].flags & 8u)) {
                    ++cursor;
                }
                wrap(start, cursor);
                start = cursor;
            } else {
                ++cursor;
            }
        }
        if (start != chars.size()) {
            wrap(start, chars.size());
        }
        float maximum {};
        for (const auto& line : result.lines) {
            maximum = std::max(maximum, line.width);
        }
        const auto text_width = maximum * style.scale[0];
        const auto text_height = static_cast<float>(result.lines.size()) * font.line_advance * style.scale[1];
        auto left = bounds[0], top = bounds[1];
        if (style.horizontal == UiTextHorizontal::center) {
            left += (bounds[2] - bounds[0] - text_width) * .5f;
        } else if (style.horizontal == UiTextHorizontal::right) {
            left = bounds[2] - text_width;
        }
        if (style.vertical == UiTextVertical::middle) {
            top += (bounds[3] - bounds[1] - text_height) * .5f;
        } else if (style.vertical == UiTextVertical::bottom) {
            top = bounds[3] - text_height;
        }
        //894E00/89B460 pass integer POINT origins to the bitmap rasterizer.
        //Direct GPU glyph quads must land on that same physical pixel grid;
        //fractional centered origins sample texel boundaries with point filtering
        //and can drop or duplicate thin strokes within a letter.
        const auto pixel_aligned = [](float value, float scale) {
            return std::round(value / scale) * scale;
        };
        left = pixel_aligned(left, style.scale[0]);
        top = pixel_aligned(top, style.scale[1]);
        result.extent.bounds = { left, top, left + text_width, top + text_height };
        result.extent.lines = result.lines.size();
        result.visual_positions.resize(chars.size());
        std::array<float, 2> terminal { left, top };
        if (chars.empty()) {
            terminal[0] = style.horizontal == UiTextHorizontal::right ? bounds[2] : style.horizontal == UiTextHorizontal::center ? (bounds[0] + bounds[2]) * .5f : bounds[0];
            const auto line_height = font.line_advance * style.scale[1];
            terminal[1] = style.vertical == UiTextVertical::bottom ? bounds[3] - line_height : style.vertical == UiTextVertical::middle ? bounds[1] + (bounds[3] - bounds[1] - line_height) * .5f : bounds[1];
        }
        std::size_t line_index {};
        for (const auto& line : result.lines) {
            reorder(chars, line);
            auto x = left;
            if (style.horizontal == UiTextHorizontal::center) {
                x += (text_width - line.width * style.scale[0]) * .5f;
            } else if (style.horizontal == UiTextHorizontal::right) {
                x += text_width - line.width * style.scale[0];
            }
            x = pixel_aligned(x, style.scale[0]);
            const auto y = top + static_cast<float>(line_index++) * font.line_advance * style.scale[1];
            std::size_t last_logical {};
            for (auto i = line.begin; i < line.end; ++i) {
                const auto& c = chars[i];
                result.visual_positions[i] = { x, y };
                result.extent.positions[c.logical] = { x, y };
                if (c.logical >= last_logical) {
                    bool advance = !rtl(c);
                    if (advance && (c.flags & 2u) && i + 1 != line.end && rtl(chars[i + 1])) {
                        advance = false;
                    }
                    last_logical = c.logical;
                    terminal = { x, y };
                    if (advance) {
                        if (c.flags & 4u) {
                            terminal = { style.horizontal == UiTextHorizontal::right ? bounds[2] : style.horizontal == UiTextHorizontal::center ? (bounds[0] + bounds[2]) * .5f : bounds[0], y + font.line_advance * style.scale[1] };
                        } else {
                            terminal[0] += static_cast<float>(c.advance) * style.scale[0];
                        }
                    }
                }
                if (!(c.flags & 4u)) {
                    x += static_cast<float>(c.advance) * style.scale[0];
                }
            }
        }
        result.extent.positions.back() = terminal;
        return result;
    }
};

UiFontLibrary::UiFontLibrary(const std::filesystem::path& root, std::string language) : state_(std::make_unique<State>()) {
    try {
        auto& s = *state_;
        s.directory = root / "Data/Language" / language;
        const auto decoded = parse_mb_to_unicode_map(read_file(s.directory / "MBToUni.dat"));
        if (!decoded) {
            throw std::runtime_error(std::format("UI font MBToUni.dat: {}", decoded.error().reason));
        }
        s.decoder = *decoded;
        s.encoder = read_file(s.directory / "UniToMB.dat");
        require(s.encoder.size() >= 12 && at<std::array<char, 4>>(s.encoder, 0) == std::array { 'B', 'F', 'U', 'M' }, 0, "expected BFUM Unicode conversion resource");
        for (std::size_t i = 0; i < s.mapped.size(); ++i) {
            const auto encoded = s.encoded(static_cast<char16_t>(i));
            if (!encoded) {
                continue;
            }
            auto index = static_cast<std::uint16_t>(static_cast<std::uint8_t>(encoded) - 1);
            if (index >= s.decoder.threshold) {
                index = static_cast<std::uint16_t>((encoded >> 8) + 255u * (index + 257u) - 254u * s.decoder.threshold);
            }
            if (index >= s.decoder.index_limit || index >= s.decoder.mapping.size()) {
                continue;
            }
            s.glyph_indices[i] = index;
            s.mapped[i] = true;
        }
        const auto index = read_file(s.directory / "ResIdx.dat");
        require(at<std::array<char, 4>>(index, 0) == std::array { 'B', 'F', 'R', 'I' }, 0, "expected BFRI language index");
        const auto count = at<std::uint32_t>(index, 4);
        require(index.size() >= 8 && count <= (index.size() - 8) / 12, 4, "language resource directory exceeds resource");
        for (std::size_t i = 0; i < count; ++i) {
            const auto record = at<IndexRecord>(index, 8 + i * 12);
            if (record.type != 4) {
                continue;
            }
            const auto offset = static_cast<std::size_t>(record.path) + 8;
            require(offset < index.size(), offset, "font path exceeds language resource index");
            auto end = offset;
            while (end < index.size() && index[end] != std::byte {}) {
                ++end;
            }
            require(end < index.size(), end, "unterminated font resource path");
            std::string path(reinterpret_cast<const char*>(index.data() + offset), end - offset);
            require(!path.empty() && !s.resources.contains(record.id), offset, "duplicate or empty font resource path");
            s.resources.emplace(record.id, std::move(path));
        }
    } catch (const UiFontError& error) {
        throw std::runtime_error(std::format("UI font resources: {}: {} at byte {}", error.resource, error.reason, error.offset));
    }
}

UiFontLibrary::~UiFontLibrary() = default;

std::expected<std::shared_ptr<const UiBitmapFont>, UiFontError> UiFontLibrary::load(std::string_view name) {
    try {
        const auto key = folded(std::string { name });
        if (const auto found = state_->fonts.find(key); found != state_->fonts.end()) {
            return found->second;
        }
        auto font = parse_ui_bitmap_font(read_file(state_->directory / std::filesystem::path { name }), std::string { name });
        if (!font) {
            return std::unexpected(font.error());
        }
        auto owned = std::make_shared<const UiBitmapFont>(std::move(*font));
        state_->fonts.emplace(key, owned);
        return owned;
    } catch (UiFontError& error) {
        return std::unexpected(std::move(error));
    }
}

std::expected<std::shared_ptr<const UiBitmapFont>, UiFontError> UiFontLibrary::slot(std::uint32_t ordinal, std::uint32_t width) {
    if (ordinal >= 14) {
        return std::unexpected(UiFontError { ordinal, {}, "UI font ordinal exceeds14 slots" });
    }
    const std::size_t group = width <= 512 ? 0 : width <= 640 ? 1 : width <= 800 ? 2 : width <= 1024 ? 3 : 4;
    const auto resource = state_->resources.find(font_ids[group][ordinal]);
    if (resource == state_->resources.end()) {
        return std::unexpected(UiFontError { font_ids[group][ordinal], {}, "missing UI font resource ID" });
    }
    return load(resource->second);
}

std::expected<UiTextExtent, UiFontError> UiFontLibrary::measure(const UiBitmapFont& font, std::u16string_view text, const std::array<float, 4>& bounds, const UiTextStyle& style) const {
    try {
        return state_->layout(font, text, bounds, style).extent;
    } catch (UiFontError& error) {
        error.resource = font.name;
        return std::unexpected(std::move(error));
    }
}

std::expected<UiTextExtent, UiFontError> UiFontLibrary::append(UiDrawList& draw, const UiBitmapFont& font, std::u16string_view text, const std::array<float, 4>& bounds, const UiTextStyle& style, const std::array<float, 4>& clip) const {
    try {
        auto layout = state_->layout(font, text, bounds, style);
        require(std::ranges::all_of(clip,
                                    [](float value) {
                                        return std::isfinite(value);
                                    }),
                0,
                "invalid UI text clip");
        if (clip[2] <= clip[0] || clip[3] <= clip[1]) {
            return layout.extent;
        }
        require(layout.characters.size() <= (std::numeric_limits<std::uint32_t>::max() - draw.vertices.size()) / 4 && layout.characters.size() <= (std::numeric_limits<std::uint32_t>::max() - draw.indices.size()) / 6, 0, "UI text draw list exceeds index range");
        const auto native_depth = style.depth ? style.depth : draw.native_depth;
        const bool depth_buffered = native_depth.has_value();
        const auto z = native_depth.value_or(0.f) * .1f;
        MeshMaterial material;
        if (depth_buffered) {
            //512680 rasterizes BFRI into a surface;6FCB00 submits its tiles.
            //The unconditional6FCD75 state enables alpha test AND depth writes:
            //transparent texels must be rejected before writing the surface Z.
            material.flags = 0x07060556u;
        } else {
            //A standalone font diagnostic has no native attachment registration.
            material.flags |= 0x100u | 0x8000u | 0x800u | 0x20000u | 0x40000u;
            material.flags &= ~0x400u;
        }
        for (std::size_t i = 0; i < layout.characters.size(); ++i) {
            const auto& c = layout.characters[i];
            if (c.flags & (4u | 64u)) {
                continue;
            }
            const auto& glyph = font.glyphs[c.glyph];
            if (!glyph.width || !glyph.height || (glyph.character_code & 65535u) == 32u) {
                continue;
            }
            const auto position = layout.visual_positions[i];
            const auto left = position[0] + glyph.bearing_x * style.scale[0], top = position[1] + glyph.bearing_y * style.scale[1], right = left + glyph.width * style.scale[0], bottom = top + glyph.height * style.scale[1];
            if (right <= clip[0] || bottom <= clip[1] || left >= clip[2] || top >= clip[3]) {
                continue;
            }
            const auto vertex = static_cast<std::uint32_t>(draw.vertices.size());
            const auto index = static_cast<std::uint32_t>(draw.indices.size());
            draw.vertices.insert(draw.vertices.end(), { { { left, top, z }, { glyph.uv[0], glyph.uv[1] }, style.color }, { { right, top, z }, { glyph.uv[2], glyph.uv[1] }, style.color }, { { right, bottom, z }, { glyph.uv[2], glyph.uv[3] }, style.color }, { { left, bottom, z }, { glyph.uv[0], glyph.uv[3] }, style.color } });
            draw.indices.insert(draw.indices.end(), { vertex, vertex + 1, vertex + 2, vertex, vertex + 2, vertex + 3 });
            if (!draw.commands.empty() && draw.commands.back().texture == glyph.image && draw.commands.back().clip == clip && draw.commands.back().material.flags == material.flags && draw.commands.back().depth_buffered == depth_buffered && draw.commands.back().first_index + draw.commands.back().index_count == index) {
                draw.commands.back().index_count += 6;
            } else {
                draw.commands.push_back({ glyph.image, index, 6, clip, material, false, depth_buffered });
            }
        }
        return std::move(layout.extent);
    } catch (UiFontError& error) {
        error.resource = font.name;
        return std::unexpected(std::move(error));
    }
}
} //namespace simcoaster
