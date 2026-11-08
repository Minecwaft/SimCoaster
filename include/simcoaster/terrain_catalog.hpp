#pragma once
#include "texture_image.hpp"

namespace simcoaster {
struct TerrainTextureCatalogEntry {
    std::int32_t value{};
    std::string filename;
    std::uint16_t texture_slot{};
};

struct TerrainTextureCatalog {
    std::vector<TerrainTextureCatalogEntry> paths;
    std::string resource_directory = "pathtex";
};

//The native active catalog has one PathTex group. QueueTex entries in the
//same file are not members of this table; queues use separate mesh templates.
std::expected<TerrainTextureCatalog, TextureError> parse_terrain_texture_catalog(std::string_view, bool small = false);
void bind_terrain_texture_catalog(MeshModel&, TerrainTextureCatalog&);
void update_terrain_cell_texture(MeshModel&,
                                 const TerrainTextureCatalog&,
                                 std::span<const TerrainCellMetadataRecord> initial,
                                 std::int32_t x,
                                 std::int32_t y,
                                 std::uint32_t group,
                                 std::uint32_t member,
                                 std::uint32_t rotation,
                                 bool procedural = true);
} //namespace simcoaster
