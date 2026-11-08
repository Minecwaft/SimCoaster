#pragma once
#include "mesh_animation.hpp"

namespace simcoaster {
struct DefinitionGrid;

struct MeshHeightMap {
    std::uint16_t width{}, height{};
    MeshBounds bounds;
    //Fine samples are a (5*width) by (5*height) row-major image, not
    //25 contiguous bytes per tile. Reduced heights and masks are tile-major.
    std::vector<std::uint8_t> fine_heights, cell_heights, placement_mask;
    bool generated{}, clipped{};
};

std::expected<MeshHeightMap, MeshError> parse_mesh_height_map(std::span<const std::byte>);
std::expected<std::vector<std::byte>, MeshError> encode_mesh_height_map(const MeshHeightMap&);
//0x4A7120/0x4A7830: integer scanline maxima, with .5 X/Z and 2.55 Y
//quantization. Samples eligible clips at 30ms intervals, excluding the first
//Create/Destroy variants. Source assets and host clocks remain unchanged.
std::expected<MeshHeightMap, MeshError> generate_mesh_height_map(const MeshModel&,
                                                                 const MeshAnimationLibrary&,
                                                                 std::uint32_t width,
                                                                 std::uint32_t height,
                                                                 const DefinitionGrid* shape,
                                                                 bool use_bounds_height = false);
} //namespace simcoaster
