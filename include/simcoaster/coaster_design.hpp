#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace simcoaster {
struct Stream;

//COS is a portable design, distinct from the live COAST save section. Native
//48F110 writes little-endian scalars, including paired name units, without
//compiler padding. Keep the otherwise unused grid padding when round-tripping
//installed designs rather than treating it as host struct padding.
struct CoasterDesignGridCell {
    std::uint32_t code{};
    std::uint8_t attribute{};
    std::array<std::byte, 3> reserved{};
};

struct CoasterDesignGeometry {
    std::int32_t tile_offset_x{}, tile_offset_y{};
    bool starts_two_piece{}, axis_mode{}; //Object flag bits 0x80 and 0x40.
    std::uint32_t flags{}, link_slot{};
    float heading{}, loft{}, secondary_parameter{}, angle_b{};
    //Native import consumes these ordinals, then rebuilds actual peer pointers
    //from geometry. Retain them here for lossless design-file IO only.
    std::vector<std::uint32_t> peers;
};

struct CoasterDesign {
    std::uint32_t version{ 2 };
    std::array<std::uint16_t, 33> name_a{}, name_b{};
    std::uint32_t source_index{}, definition_id{}, build_cost{};
    std::int32_t placement_offset_x{}, placement_offset_y{};
    std::uint32_t grid_width{}, grid_height{};
    std::array<CoasterDesignGridCell, 1600> grid{};
    std::vector<CoasterDesignGeometry> geometry;
};
enum class CoasterDesignFailure {
    transfer,
    geometry_count,
    peer_count
};

struct CoasterDesignError {
    CoasterDesignFailure reason{};
    std::size_t record{};
};

//48FF00 does not validate the version or grid header, so the low-level reader
//preserves them. Applying a design to a live World performs semantic checks.
std::expected<CoasterDesign, CoasterDesignError> read_coaster_design(Stream&);
std::expected<void, CoasterDesignError> write_coaster_design(Stream&, const CoasterDesign&);
} //namespace simcoaster
