#pragma once
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace simcoaster {
//Native6CE330 uses inclusive tile coordinates. This selects procedural
//texture sources; it never changes the heightfield or mesh dimensions.
struct TerrainTileBounds {
    std::int64_t minimum_x{}, minimum_y{};
    std::int64_t maximum_x = std::numeric_limits<std::int64_t>::max();
    std::int64_t maximum_y = std::numeric_limits<std::int64_t>::max();

    bool contains(std::size_t x, std::size_t y) const {
        return static_cast<std::int64_t>(x) >= minimum_x
            && static_cast<std::int64_t>(x) <= maximum_x
            && static_cast<std::int64_t>(y) >= minimum_y
            && static_cast<std::int64_t>(y) <= maximum_y;
    }

    bool operator==(const TerrainTileBounds&) const = default;
};
enum class TerrainCellFlags : std::uint16_t {
    none = 0,
    source_grid_excluded = 1,
    render_tl_br_diagonal = 2,
    sum_diagonal = 4,
    uv_quarter_turn = 8,
    uv_half_turn = 0x10,
    uv_three_quarter_turn = 0x20,
    uv_mirror = 0x40,
    texture_overlay = 0x80,
    texture_dirty = 0x100,
    shadow_center = 0x400,
    two_triangles = 0x800,
    shadow_bottom_left = 0x1000,
    shadow_bottom_right = 0x2000,
    shadow_top_left = 0x4000,
    shadow_top_right = 0x8000,
};

struct TerrainCellMetadataRecord {
    TerrainCellFlags flags{};
    std::uint16_t resource_slot_index{};
};

//Logical runtime fields from the native48-byte terrain-grid header. Spans
//carry host bounds; this is not an ABI overlay or a serialized representation.
struct TerrainGridView {
    std::uint32_t unknown_header_word{};
    std::size_t height_sample_count{}, cell_record_count{};
    float origin_x{}, origin_y{}, cell_scale_x{}, cell_scale_y{};
    std::uint32_t width_cells{}, height_cells{};
    float height_range_min{}, height_range_max{};
    std::span<const float> height_samples;
    std::span<const TerrainCellMetadataRecord> cell_metadata;
};

//Native owners never retain the wire header's pointers or spans into another
//owner's vectors. A view is obtained after copies/moves have completed.
struct TerrainGridData {
    std::uint32_t unknown_header_word{};
    float origin_x{}, origin_y{}, cell_scale_x{}, cell_scale_y{};
    std::uint32_t width_cells{}, height_cells{};
    float height_range_min{}, height_range_max{};
    std::vector<float> height_samples;
    std::vector<TerrainCellMetadataRecord> cell_metadata;

    TerrainGridView view() const {
        return { unknown_header_word, height_samples.size(), cell_metadata.size(), origin_x,     origin_y,
                 cell_scale_x,        cell_scale_y,          width_cells,          height_cells, height_range_min,
                 height_range_max,    height_samples,        cell_metadata };
    }
};

//Renderer model projection used by the compiled cell logic. The full native
//model layout is recorded in IDA; its remaining render behavior is unported.
struct ActiveTerrainModelDataView {
    const TerrainGridView* terrain_grid{};
};

double sample_terrain_surface_height(const TerrainGridView&, float world_x, float world_y);
} //namespace simcoaster
