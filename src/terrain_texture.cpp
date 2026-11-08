#include "simcoaster/frame_profile.hpp"
#include "simcoaster/terrain_texture.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
[[noreturn]] void fail(TextureFailure reason, std::size_t offset = 0) {
    throw TextureError { reason, offset, "base.lnd" };
}

void require(bool valid, TextureFailure reason = TextureFailure::dimensions) {
    if (!valid) {
        fail(reason);
    }
}

struct BufferHeader {
    [[=save::Field { "width", 0 }]] std::uint32_t width {};
    [[=save::Field { "height", 1 }]] std::uint32_t height {};
    [[=save::Field { "channels", 2 }]] std::uint32_t channels {};
};

struct Reader {
    std::span<const std::byte> bytes;
    std::size_t cursor {};

    void range(std::size_t size) const {
        if (cursor > bytes.size() || size > bytes.size() - cursor) {
            fail(TextureFailure::bounds, cursor);
        }
    }

    template <class T>
    T get() {
        constexpr auto size = save::value_width<T>;
        range(size);
        T value {};
        save::detail::decode<T>(std::span<const std::byte, size> { bytes.data() + cursor, size }, value);
        cursor += size;
        return value;
    }

    TerrainTextureBuffer buffer(std::uint32_t channels) {
        const auto h = get<BufferHeader>();
        require(h.width && h.height && h.width <= 16384 && h.height <= 16384 && h.channels == channels);
        const auto count = static_cast<std::size_t>(h.width) * h.height * channels;
        range(count);
        TerrainTextureBuffer result { h.width, h.height, h.channels, {} };
        result.bytes.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            result.bytes[i] = std::to_integer<std::uint8_t>(bytes[cursor + i]);
        }
        cursor += count;
        return result;
    }
};

std::array<std::uint8_t, 256> remap(std::uint8_t center, std::uint8_t extent) {
    std::array<std::uint8_t, 256> result {};
    const int span = extent + 1, half = span / 2, lower = center - half, upper = center + half;
    for (int i = 0; i < 256; ++i) {
        result[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i < lower ? 0 : i > upper ? 255 : (i - lower) * 255 / span);
    }
    return result;
}

std::uint8_t sample(const TerrainTextureBuffer& buffer, std::size_t index) {
    if (index >= buffer.bytes.size()) {
        fail(TextureFailure::bounds, index);
    }
    return buffer.bytes[index];
}

std::vector<std::uint8_t> resample(const TerrainTextureBuffer& source, std::size_t column, std::size_t row, std::size_t channel, std::size_t dimension) {
    const auto count = dimension / 4;
    const auto log = std::countr_zero(count);
    const auto scale = std::int64_t { 65536 } / static_cast<std::int64_t>(count);
    std::vector<std::uint8_t> result(dimension * dimension);
    for (std::size_t sy = 0; sy < 4; ++sy) {
        for (std::size_t sx = 0; sx < 4; ++sx) {
            //Deliberately use native linear addressing. The last terrain column can
            //sample the first byte of the next source row; clamping changes the game.
            const auto at = 4 * (sx + 4 * column + (sy + 4 * row) * source.width) + channel, below = at + 4 * source.width;
            const auto first = sample(source, at), next = sample(source, below);
            auto horizontal = (sample(source, at + 4) - first) * scale;
            const auto vertical = (next - first) * scale;
            const auto gradient = (((sample(source, below + 4) - next) * scale) - horizontal) >> log;
            auto initial = static_cast<std::int64_t>(first) * 65536;
            for (std::size_t y = 0; y < count; ++y) {
                auto value = initial;
                for (std::size_t x = 0; x < count; ++x) {
                    result[sx * count + x + (sy * count + y) * dimension] = static_cast<std::uint8_t>(value >> 16);
                    value += horizontal;
                }
                horizontal += gradient;
                initial += vertical;
            }
        }
    }
    return result;
}

std::uint8_t blend(std::uint8_t a, std::uint8_t b, std::uint8_t weight) {
    //Arithmetic right shift preserves the native floor for negative deltas.
    return static_cast<std::uint8_t>(b + ((static_cast<int>(weight) * (a - b)) >> 8));
}

RgbaPixel blend(const RgbaPixel& a, const RgbaPixel& b, std::uint8_t weight) {
    return { blend(a[0], b[0], weight), blend(a[1], b[1], weight), blend(a[2], b[2], weight), 255 };
}

struct Patch {
    const TerrainTextureBuffer* source;
    std::size_t offset;

    std::uint8_t at(std::size_t x, std::size_t y, std::size_t channel = 0) const {
        return sample(*source, offset + (x + y * source->width) * source->channels + channel);
    }
};

std::array<Patch, 13> patches(const TerrainTextureLayer& layer, std::size_t column, std::size_t row) {
    std::array<Patch, 13> result {};
    const auto n = layer.dimension;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto& p = layer.payloads[i];
        require(p.width > n && p.height > n);
        const auto x = column * n % (p.width - n), y = row * n % (p.height - n);
        result[i] = { &p, (x + y * p.width) * p.channels };
    }
    return result;
}

std::pair<std::size_t, std::size_t> oriented(std::size_t x, std::size_t y, std::size_t n, std::uint16_t flags) {
    //0x6BB039..146: source origin and signed pixel/row strides. Mirroring is
    //a transpose, followed by the same priority8/10/20 rotation as terrain UVs.
    if (flags & 0x40u) {
        std::swap(x, y);
    }
    if (flags & 8u) {
        const auto old = x;
        x = y;
        y = n - 1 - old;
    } else if (flags & 0x10u) {
        x = n - 1 - x;
        y = n - 1 - y;
    } else if (flags & 0x20u) {
        const auto old = x;
        x = n - 1 - y;
        y = old;
    }
    return { x, y };
}
} //namespace

std::expected<TerrainTextureDefinition, TextureError> parse_terrain_texture_definition(std::span<const std::byte> bytes) {
    try {
        Reader r { bytes };
        if (r.get<std::uint8_t>() != 3) {
            fail(TextureFailure::format);
        }
        TerrainTextureDefinition result;
        result.header = r.get<std::array<std::uint8_t, 10>>();
        constexpr std::array<std::size_t, 5> pairs { 0, 2, 6, 4, 8 };
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            result.remaps[i] = remap(result.header[pairs[i]], result.header[pairs[i] + 1]);
        }
        result.base = r.buffer(4);
        require(result.base.width >= 8 && result.base.height >= 8);
        constexpr std::array<std::size_t, 13> wire_order { 0, 1, 3, 4, 8, 9, 2, 5, 10, 6, 7, 11, 12 };
        for (auto& layer : result.layers) {
            layer.dimension = r.get<std::uint32_t>();
            layer.index = r.get<std::uint8_t>();
            require(layer.dimension >= 16 && layer.dimension <= 128 && std::has_single_bit(layer.dimension));
            for (const auto i : wire_order) {
                const bool color = i == 0 || i == 1 || i == 3 || i == 4 || i == 8 || i == 9;
                layer.payloads[i] = r.buffer(color ? 4u : 1u);
                require(layer.payloads[i].width > layer.dimension && layer.payloads[i].height > layer.dimension);
            }
        }
        require(r.cursor == bytes.size(), TextureFailure::format);
        return result;
    } catch (const TextureError& error) {
        return std::unexpected(error);
    }
}

std::expected<TextureImage, TextureError> compose_terrain_texture_cell(const TerrainTextureDefinition& definition, std::size_t layer_index, std::size_t column, std::size_t row, const TextureImage* overlay, TerrainCellFlags cell_flags) {
    try {
        require(layer_index < definition.layers.size());
        const auto& layer = definition.layers[layer_index];
        const auto n = static_cast<std::size_t>(layer.dimension);
        require(n >= 16 && n <= 128 && std::has_single_bit(n) && definition.base.channels == 4 && definition.base.width >= 8 && definition.base.height >= 8);
        const auto height = (definition.base.height - 1) / 4;
        require(column < definition.base.width / 4 && row < height);
        row = height - row - 1;
        TextureImage result { layer.dimension, layer.dimension, std::vector<RgbaPixel>(n * n), TextureAlpha::opaque };
        const auto marker = 4 * ((4 * row + 1) * definition.base.width + 4 * column + 1);
        if ((sample(definition.base, marker) & sample(definition.base, marker + 4) & sample(definition.base, marker + 4 * definition.base.width) & sample(definition.base, marker + 4 * definition.base.width + 4)) != 255) {
            std::ranges::fill(result.pixels, RgbaPixel { 127, 127, 127, 255 });
            return result;
        }
        const auto weight = resample(definition.base, column, row, 1, n), select_a = resample(definition.base, column, row, 2, n), select_c = resample(definition.base, column, row, 3, n);
        const auto patch = patches(layer, column, row);
        constexpr std::array<std::size_t, 3> colors { 0, 3, 8 }, noise { 2, 5, 10 };
        for (std::size_t y = 0; y < n; ++y) {
            for (std::size_t block = 0; block < n; block += 16) {
                std::array<std::uint8_t, 16> mix_c {}, mix_a {};
                unsigned c_and = 255, c_or = 0, a_and = 255, a_or = 0;
                for (std::size_t i = 0; i < 16; ++i) {
                    const auto x = block + i, at = x + y * n;
                    mix_c[i] = std::max(definition.remaps[4][(select_c[at] * patch[11].at(x, y)) >> 8], definition.remaps[4][(select_c[at] * patch[12].at(x, y)) >> 8]);
                    c_and &= mix_c[i];
                    c_or |= mix_c[i];
                }
                if (c_and != 255) {
                    for (std::size_t i = 0; i < 16; ++i) {
                        const auto x = block + i, at = x + y * n;
                        mix_a[i] = std::max(definition.remaps[3][(select_a[at] * patch[6].at(x, y)) >> 8], definition.remaps[3][(select_a[at] * patch[7].at(x, y)) >> 8]);
                        a_and &= mix_a[i];
                        a_or |= mix_a[i];
                    }
                }
                //The native THREE-way scalar helper never increments its shared-weight
                //pointer. Two-way and MMX paths do. Preserve this visible16-pixel rule.
                const bool three_way = c_and != 255 && c_or && a_and != 255 && a_or;
                for (std::size_t i = 0; i < 16; ++i) {
                    const auto x = block + i, at = x + y * n;
                    const auto candidate = [&](std::size_t kind) {
                        const auto first = colors[kind];
                        const auto factor = definition.remaps[kind][(weight[three_way ? block + y * n : at] * patch[noise[kind]].at(x, y)) >> 8];
                        RgbaPixel pixel { 0, 0, 0, 255 };
                        for (std::size_t c = 0; c < 3; ++c) {
                            pixel[c] = blend(patch[first].at(x, y, c + 1), patch[first + 1].at(x, y, c + 1), factor);
                        }
                        return pixel;
                    };
                    RgbaPixel pixel;
                    if (c_and == 255) {
                        pixel = candidate(2);
                    } else if (c_or) {
                        auto below = a_and == 255 ? candidate(1) : a_or ? blend(candidate(1), candidate(0), mix_a[i]) : candidate(0);
                        pixel = blend(candidate(2), below, mix_c[i]);
                    } else {
                        pixel = a_and == 255 ? candidate(1) : a_or ? blend(candidate(1), candidate(0), mix_a[i]) : candidate(0);
                    }
                    result.pixels[x + (n - y - 1) * n] = pixel;
                }
            }
        }
        if (overlay) {
            require(overlay->width && overlay->height && overlay->pixels.size() == static_cast<std::size_t>(overlay->width) * overlay->height);
            auto background = std::move(result.pixels);
            result.pixels.resize(n * n);
            const auto sx = overlay->width / n, sy = overlay->height / n;
            for (std::size_t y = 0; y < n; ++y) {
                for (std::size_t x = 0; x < n; ++x) {
                    const auto [bx, by] = oriented(x, y, n, static_cast<std::uint16_t>(cell_flags));
                    const auto& top = overlay->pixels[x * sx + y * sy * overlay->width];
                    result.pixels[x + y * n] = blend(top, background[bx + by * n], top[3]);
                }
            }
        }
        return result;
    } catch (const TextureError& error) {
        return std::unexpected(error);
    }
}

TerrainTextureAtlas::TerrainTextureAtlas(TerrainTextureDefinition definition, std::size_t layer) : definition_(std::move(definition)), layer_(layer) {
    if (layer_ >= definition_.layers.size()) {
        throw std::invalid_argument("Terrain texture layer is out of range");
    }
}

void initialize_procedural_terrain_cell_flags(TerrainGridData& grid) {
    for (auto& cell : grid.cell_metadata) {
        const auto flags = static_cast<std::uint16_t>(cell.flags);
        if (!(flags & 0x81u)) {
            cell.flags = static_cast<TerrainCellFlags>(flags & 0xFF87u);
        }
    }
}

std::expected<TerrainTextureAtlas, TextureError> load_terrain_texture_atlas(const MeshModelLibrary& library, std::uint32_t dimension) {
    auto bytes = library.read_resource("base.lnd");
    if (!bytes) {
        return std::unexpected(TextureError { TextureFailure::compression, bytes.error().offset, "base.lnd" });
    }
    if (!*bytes) {
        return std::unexpected(TextureError { TextureFailure::missing, 0, "base.lnd" });
    }
    auto definition = parse_terrain_texture_definition(**bytes);
    if (!definition) {
        return std::unexpected(definition.error());
    }
    for (std::size_t i = 0; i < definition->layers.size(); ++i) {
        if (definition->layers[i].dimension == dimension) {
            return TerrainTextureAtlas { std::move(*definition), i };
        }
    }
    return std::unexpected(TextureError { TextureFailure::dimensions, 0, "base.lnd" });
}

void TerrainTextureAtlas::update(MeshModel& model, MeshTextureLibrary& textures) {
    FrameTimer timer { FrameStage::terrain_atlas };
    if (!model.terrain) {
        throw std::invalid_argument("Procedural textures need a terrain grid");
    }
    auto& grid = *model.terrain;
    const auto count = static_cast<std::size_t>(grid.width_cells) * grid.height_cells;
    if (!count || grid.cell_metadata.size() != count) {
        throw std::invalid_argument("Invalid procedural terrain dimensions");
    }
    const bool reset = grid.width_cells != width_ || grid.height_cells != height_ || bindings_.size() != count;
    const auto n = dimension(), pitch = n + 2, across = 2048 / pitch;
    const auto capacity = static_cast<std::size_t>(across) * across;
    std::vector<std::string> names;
    for (const auto& slot : model.textures) {
        names.push_back(textures.filename(slot));
    }
    bool unchanged = !reset && names == previous_slot_names_ && previous_cells_.size() == count;
    for (std::size_t cell = 0; unchanged && cell < count; ++cell) {
        const auto& current = grid.cell_metadata[cell];
        const auto& previous = previous_cells_[cell];
        //6D0EE0 skips bit1 cells before acquiring procedural sources. Their
        //pending dirty bit is not a request to copy/upload an invisible tile.
        if ((static_cast<std::uint16_t>(current.flags) & 1u) && (static_cast<std::uint16_t>(previous.flags) & 1u)) {
            continue;
        }
        const bool dirty = bounds_.contains(cell % grid.width_cells, cell / grid.width_cells) && (static_cast<std::uint16_t>(current.flags) & 0x100u);
        unchanged = current.flags == previous.flags && current.resource_slot_index == previous.resource_slot_index && !dirty;
    }
    if (unchanged) {
        return;
    }
    std::vector<TextureImagePtr> pages = reset ? std::vector<TextureImagePtr>((count + capacity - 1) / capacity) : pages_;
    auto bindings = reset ? std::vector<TerrainTextureAtlasBinding>(count) : bindings_;
    std::vector<std::shared_ptr<TextureImage>> changed(pages.size());
    for (std::size_t cell = 0; cell < count; ++cell) {
        const auto column = cell % grid.width_cells, row = cell / grid.width_cells;
        if (!bounds_.contains(column, row)) {
            bindings[cell].procedural = false;
            continue;
        }
        const auto metadata = grid.cell_metadata[cell];
        const auto bits = static_cast<std::uint16_t>(metadata.flags);
        const auto page = cell / capacity, within = cell % capacity;
        if (bits & 1u) {
            bindings[cell].procedural = false;
            continue;
        }
        const auto x = static_cast<std::uint32_t>(within % across) * pitch, y = static_cast<std::uint32_t>(within / across) * pitch;
        const bool same_slot = cell < previous_cells_.size() && metadata.resource_slot_index == previous_cells_[cell].resource_slot_index;
        const bool same_name = metadata.resource_slot_index < names.size() && metadata.resource_slot_index < previous_slot_names_.size() && names[metadata.resource_slot_index] == previous_slot_names_[metadata.resource_slot_index];
        const bool same_flags = cell < previous_cells_.size() && metadata.flags == previous_cells_[cell].flags;
        if (!reset && same_slot && same_name && same_flags && !(bits & 0x100u)) {
            continue;
        }
        if (!changed[page]) {
            if (pages[page]) {
                changed[page] = std::make_shared<TextureImage>(*pages[page]);
            } else {
                const auto rows = (std::min(capacity, count - page * capacity) + across - 1) / across;
                changed[page] = std::make_shared<TextureImage>(TextureImage { across * pitch, static_cast<std::uint32_t>(rows) * pitch, {}, TextureAlpha::opaque });
                changed[page]->pixels.resize(static_cast<std::size_t>(changed[page]->width) * changed[page]->height, RgbaPixel { 127, 127, 127, 255 });
            }
        }
        //705AC0 maps a procedural source from its first texel centre to its last,
        //not over the outside edges of the padded atlas allocation.
        auto& image = *changed[page];
        bindings[cell] = { page, { (static_cast<float>(x) + 1.5f) / static_cast<float>(image.width), (static_cast<float>(y) + 1.5f) / static_cast<float>(image.height) }, { static_cast<float>(n - 1) / static_cast<float>(image.width), static_cast<float>(n - 1) / static_cast<float>(image.height) } };
        TextureImagePtr overlay;
        if (bits & 0x80u) {
            if (metadata.resource_slot_index >= model.textures.size()) {
                throw std::runtime_error("Invalid terrain overlay slot");
            }
            auto loaded = textures.load(model.textures[metadata.resource_slot_index]);
            if (!loaded) {
                loaded = textures.missing_texture();
            }
            if (loaded) {
                overlay = *loaded;
            }
        }
        bindings[cell].procedural = true;
        auto composed = compose_terrain_texture_cell(definition_, layer_, static_cast<std::size_t>(static_cast<std::int64_t>(column) - bounds_.minimum_x), static_cast<std::size_t>(static_cast<std::int64_t>(row) - bounds_.minimum_y), overlay.get(), metadata.flags);
        if (!composed) {
            throw std::runtime_error(describe_texture_error(composed.error()));
        }
        for (std::uint32_t py = 0; py < pitch; ++py) {
            for (std::uint32_t px = 0; px < pitch; ++px) {
                const auto tx = std::clamp(px, 1u, n) - 1, ty = std::clamp(py, 1u, n) - 1;
                image.pixels[x + px + static_cast<std::size_t>(y + py) * image.width] = composed->pixels[tx + static_cast<std::size_t>(ty) * n];
            }
        }
    }
    for (std::size_t i = 0; i < changed.size(); ++i) {
        if (changed[i]) {
            pages[i] = std::move(changed[i]);
        }
    }
    for (std::size_t cell = 0; cell < count; ++cell) {
        if (bindings[cell].procedural) {
            grid.cell_metadata[cell].flags = static_cast<TerrainCellFlags>(static_cast<std::uint16_t>(grid.cell_metadata[cell].flags) & ~0x100u);
        }
    }
    pages_ = std::move(pages);
    bindings_ = std::move(bindings);
    previous_cells_ = grid.cell_metadata;
    previous_slot_names_ = std::move(names);
    width_ = grid.width_cells;
    height_ = grid.height_cells;
}
} //namespace simcoaster
