#pragma once
#include "texture_image.hpp"

namespace simcoaster {
struct TerrainTextureBuffer {
    std::uint32_t width {}, height {}, channels {};
    std::vector<std::uint8_t> bytes;
};

//Native payload indices are retained: three colour pairs (0/1,3/4,8/9),
//their noise maps (2,5,10), and selection masks (6/7,11/12).
struct TerrainTextureLayer {
    std::uint32_t dimension {};
    std::uint8_t index {};
    std::array<TerrainTextureBuffer, 13> payloads;
};

struct TerrainTextureDefinition {
    std::array<std::uint8_t, 10> header {};
    std::array<std::array<std::uint8_t, 256>, 5> remaps {};
    TerrainTextureBuffer base;
    std::array<TerrainTextureLayer, 3> layers;
};

std::expected<TerrainTextureDefinition, TextureError> parse_terrain_texture_definition(std::span<const std::byte>);
//0x4D7C50: ordinary procedural cells lose the static UV transform; excluded
//and overlay cells retain it. Call when enabling the procedural surface.
void initialize_procedural_terrain_cell_flags(TerrainGridData&);
//0x6B9B80, normalized to owned, opaque RGBA. The optional overlay is already
//decoded RGBA, so no legacy renderer pixel packing enters the host runtime.
std::expected<TextureImage, TextureError> compose_terrain_texture_cell(const TerrainTextureDefinition&, std::size_t layer, std::size_t column, std::size_t row, const TextureImage* overlay = nullptr, TerrainCellFlags flags = TerrainCellFlags::none);

struct TerrainTextureAtlasBinding {
    std::size_t page {};
    std::array<float, 2> origin {}, extent {};
    bool procedural {};
};

class TerrainTextureAtlas {
    TerrainTextureDefinition definition_;
    std::size_t layer_ {};
    std::uint32_t width_ {}, height_ {};
    std::vector<TerrainCellMetadataRecord> previous_cells_;
    std::vector<std::string> previous_slot_names_;
    std::vector<TextureImagePtr> pages_;
    std::vector<TerrainTextureAtlasBinding> bindings_;
    TerrainTileBounds bounds_;

public:
    TerrainTextureAtlas(TerrainTextureDefinition definition, std::size_t layer);
    //Regenerate changed cells and replace only affected immutable atlas pages.
    //Clearing native dirty bit100 commits only after all composition succeeds.
    void update(MeshModel&, MeshTextureLibrary&);

    void invalidate() {
        previous_cells_.clear();
    }

    void configure_bounds(TerrainTileBounds bounds) {
        if (bounds_ != bounds) {
            bounds_ = bounds;
            invalidate();
        }
    }

    const TerrainTileBounds& bounds() const {
        return bounds_;
    }

    const TerrainTextureDefinition& definition() const {
        return definition_;
    }

    std::size_t layer_index() const {
        return layer_;
    }

    std::uint32_t dimension() const {
        return definition_.layers[layer_].dimension;
    }

    std::span<const TextureImagePtr> pages() const {
        return pages_;
    }

    std::span<const TerrainTextureAtlasBinding> bindings() const {
        return bindings_;
    }
};

std::expected<TerrainTextureAtlas, TextureError> load_terrain_texture_atlas(const MeshModelLibrary&, std::uint32_t dimension = 32);
} //namespace simcoaster
