#include "simcoaster/terrain_catalog.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace simcoaster {
namespace {
std::string lower(std::string_view text) {
    std::string out{ text };
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return out;
}

bool letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

struct Tokens {
    std::string_view text;
    std::size_t cursor{};

    std::optional<std::string_view> next() {
        while (cursor < text.size()) {
            if (text[cursor] == '#' || text[cursor] == ';') {
                while (cursor < text.size() && text[cursor] != '\n')
                    ++cursor;
                continue;
            }
            if (static_cast<unsigned char>(text[cursor]) <= 32) {
                ++cursor;
                continue;
            }
            const auto start = cursor;
            while (cursor < text.size() && static_cast<unsigned char>(text[cursor]) > 32)
                ++cursor;
            return text.substr(start, cursor - start);
        }
        return std::nullopt;
    }
};
} //namespace

std::expected<TerrainTextureCatalog, TextureError> parse_terrain_texture_catalog(std::string_view text, bool small) {
    TerrainTextureCatalog result;
    if (small)
        result.resource_directory = "spathtex";
    Tokens tokens{ text };
    bool selected = false;
    while (const auto token = tokens.next()) {
        //460A10 accepts the configured SPathTex name OR that name with its
        //leading S removed. All four original TCT files use the PathTex alias.
        if (letter(token->front())) {
            const auto group = lower(*token);
            selected = group == "pathtex" || (small && group == "spathtex");
            continue;
        }
        std::int32_t number{};
        const auto parsed = std::from_chars(token->data(), token->data() + token->size(), number);
        const auto name = tokens.next();
        if (parsed.ec != std::errc{} || parsed.ptr != token->data() + token->size() || !name)
            return std::unexpected(TextureError{ TextureFailure::format, tokens.cursor, "terrain.tct" });
        if (selected)
            result.paths.push_back({ number, std::string{ *name }, 0 });
    }
    if (result.paths.empty())
        return std::unexpected(TextureError{ TextureFailure::format, tokens.cursor, "terrain.tct" });
    return result;
}

void bind_terrain_texture_catalog(MeshModel& model, TerrainTextureCatalog& catalog) {
    std::vector<MeshTextureSlot> merged;
    std::vector<std::size_t> mapping;
    std::vector<std::optional<std::size_t>> matched;
    for (const auto& slot : model.textures) {
        const auto found = std::ranges::find_if(catalog.paths, [&](const auto& row) {
            return lower(row.filename) == lower(slot.filename);
        });
        if (found == catalog.paths.end()) {
            mapping.push_back(merged.size());
            matched.push_back(std::nullopt);
            merged.push_back(slot);
        } else {
            mapping.push_back(0);
            matched.push_back(static_cast<std::size_t>(found - catalog.paths.begin()));
        }
    }
    const auto preserved = merged.size();
    if (preserved > 65535 || catalog.paths.size() > 65535 - preserved)
        throw std::length_error("Terrain texture slot table exceeds its serialized index range");
    for (std::size_t i = 0; i < mapping.size(); ++i)
        if (matched[i])
            mapping[i] = preserved + *matched[i];
    //0x4D7F60 stores the catalog group's directory separately from its filename.
    //Preserve that namespace so PathTex and SPathTex cannot collide in one WAD.
    for (auto& row : catalog.paths) {
        row.texture_slot = static_cast<std::uint16_t>(merged.size());
        merged.push_back({ 1, 0, 0, catalog.resource_directory + "/" + row.filename, {} });
    }
    for (auto& node : model.nodes)
        if (node.drawable)
            for (auto& group : node.drawable->groups)
                if (group.texture_slot)
                    group.texture_slot = mapping.at(*group.texture_slot);
    if (model.terrain)
        for (auto& cell : model.terrain->cell_metadata) {
            if (cell.resource_slot_index < mapping.size())
                cell.resource_slot_index = static_cast<std::uint16_t>(mapping[cell.resource_slot_index]);
            else if (!(static_cast<std::uint16_t>(cell.flags) & 1u))
                throw std::out_of_range("Terrain catalog references an invalid source slot");
        }
    model.textures = std::move(merged);
}

void update_terrain_cell_texture(MeshModel& model,
                                 const TerrainTextureCatalog& catalog,
                                 std::span<const TerrainCellMetadataRecord> initial,
                                 std::int32_t x,
                                 std::int32_t y,
                                 std::uint32_t group,
                                 std::uint32_t member,
                                 std::uint32_t rotation,
                                 bool procedural) {
    if (!model.terrain)
        return;
    auto& grid = *model.terrain;
    if (x < 0
        || y < 0
        || static_cast<std::uint32_t>(x) >= grid.width_cells
        || static_cast<std::uint32_t>(y) >= grid.height_cells)
        return;
    const auto index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * grid.width_cells;
    auto& cell = grid.cell_metadata.at(index);
    auto flags = static_cast<std::uint16_t>(cell.flags);
    if (!group && member == 8) {
        cell.flags = static_cast<TerrainCellFlags>((flags | 0x101u) & ~0x80u);
        return;
    }
    if (!group && member == 55) {
        if (index >= initial.size())
            throw std::out_of_range("Missing terrain snapshot cell");
        cell = initial[index];
        flags = static_cast<std::uint16_t>(cell.flags) | 0x100u;
        if (procedural)
            flags &= 0xFF87u;
        cell.flags = static_cast<TerrainCellFlags>(flags);
        return;
    }
    if (group > 1)
        throw std::out_of_range("Only the native PathTex terrain group is registered");
    if (catalog.paths.empty())
        return;
    cell.resource_slot_index = catalog.paths[member % catalog.paths.size()].texture_slot;
    flags = (flags & 0xFF86u) | 0x180u;
    if (rotation == 90)
        flags |= 0x20u;
    else if (rotation == 180)
        flags |= 0x10u;
    else if (rotation == 270)
        flags |= 8u;
    cell.flags = static_cast<TerrainCellFlags>(flags);
}
} //namespace simcoaster
