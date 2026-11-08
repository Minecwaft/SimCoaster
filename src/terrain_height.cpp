#include "simcoaster/terrain_grid.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <cmath>
#include <stdexcept>

namespace simcoaster {
double sample_terrain_surface_height(const TerrainGridView& grid, float x, float y) {
    if (!(x >= 0.f) || !(y >= 0.f) || static_cast<double>(grid.width_cells) * 10.0 <= x || static_cast<double>(grid.height_cells) * 10.0 <= y) {
        return 0.0;
    }
    double integral_x {}, integral_y {};
    //This particular sampler deliberately does not subtract grid origins.
    const auto fx = static_cast<float>(std::modf(static_cast<double>(x) / grid.cell_scale_x, &integral_x));
    const auto fy = static_cast<float>(std::modf(static_cast<double>(y) / grid.cell_scale_y, &integral_y));
    const auto column = fixed::truncate_integer(integral_x), row = fixed::truncate_integer(integral_y);
    if (column < 0 || row < 0 || static_cast<std::uint32_t>(column) >= grid.width_cells || static_cast<std::uint32_t>(row) >= grid.height_cells) {
        throw std::out_of_range("terrain sampling cell");
    }
    const auto cell = static_cast<std::size_t>(row) * grid.width_cells + static_cast<std::uint32_t>(column);
    if (cell >= grid.cell_metadata.size()) {
        throw std::out_of_range("terrain sampling metadata");
    }
    const auto top = static_cast<std::size_t>(row) * (static_cast<std::size_t>(grid.width_cells) + 1u) + static_cast<std::uint32_t>(column);
    const auto bottom = top + grid.width_cells + 1u;
    const auto height = [&](std::size_t index) -> double {
        if (index >= grid.height_samples.size()) {
            throw std::out_of_range("terrain height sample");
        }
        return grid.height_samples[index];
    };
    const auto flags = static_cast<std::uint16_t>(grid.cell_metadata[cell].flags);
    if (!(flags & 0x800u) || ((flags & 0x4u) && fx < 1.0 - fy)) {
        return fx * height(top + 1) + (1.0 - fx) * height(top) + (height(bottom) - height(top)) * fy;
    }
    if (flags & 0x4u) {
        return (1.0 - fx) * height(bottom) + fx * height(bottom + 1) - (height(bottom + 1) - height(top + 1)) * (1.0 - fy);
    }
    if (fx >= fy) {
        return (1.0 - fx) * height(top) + fx * height(top + 1) + (height(bottom + 1) - height(top + 1)) * fy;
    }
    return fx * height(bottom + 1) + (1.0 - fx) * height(bottom) - (height(bottom) - height(top)) * (1.0 - fy);
}
} //namespace simcoaster
