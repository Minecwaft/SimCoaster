#include "terrain_scene_cache.hpp"
#include "simcoaster/frame_profile.hpp"
#include "simcoaster/terrain_sky_shadow.hpp"
#include "simcoaster/weather_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
bool same_grid(const TerrainGridData& saved, const TerrainGridView& grid) {
    if (saved.width_cells != grid.width_cells || saved.height_cells != grid.height_cells || saved.origin_x != grid.origin_x || saved.origin_y != grid.origin_y || saved.cell_scale_x != grid.cell_scale_x || saved.cell_scale_y != grid.cell_scale_y || !std::ranges::equal(saved.height_samples, grid.height_samples) || saved.cell_metadata.size() != grid.cell_metadata.size()) {
        return false;
    }
    for (std::size_t i = 0; i < grid.cell_metadata.size(); ++i) {
        if ((static_cast<std::uint16_t>(saved.cell_metadata[i].flags) & 2u) != (static_cast<std::uint16_t>(grid.cell_metadata[i].flags) & 2u)) {
            return false;
        }
    }
    return true;
}

void validate(const TerrainGridView& grid) {
    const auto width = static_cast<std::size_t>(grid.width_cells);
    const auto height = static_cast<std::size_t>(grid.height_cells);
    if (!width || !height || width > std::numeric_limits<std::uint32_t>::max() / 6u / height || grid.height_samples.size() != (width + 1) * (height + 1) || grid.cell_metadata.size() != width * height || !std::isfinite(grid.origin_x) || !std::isfinite(grid.origin_y) || !std::isfinite(grid.cell_scale_x) || !std::isfinite(grid.cell_scale_y) || !std::ranges::all_of(grid.height_samples, [](float value) {
            return std::isfinite(value);
        })) {
        throw std::runtime_error("Invalid terrain sky-shadow geometry");
    }
}

std::vector<float> shadow_heights(const TerrainGridView& grid) {
    //6CEB57 copies resource heights, then 6CE660 adjusts only that workspace.
    //Order matters at overlapping rectangles, including both +4 passes.
    struct Rectangle {
        std::uint32_t x0, y0, x1, y1;
        float delta;
    };

    static constexpr std::array rectangles { Rectangle { 28, 0, 30, 12, 4 },  Rectangle { 31, 11, 43, 13, 4 }, Rectangle { 44, 11, 46, 16, 4 }, Rectangle { 29, 0, 29, 12, 4 }, Rectangle { 30, 11, 44, 12, 4 }, Rectangle { 45, 11, 45, 16, 4 }, Rectangle { 31, 10, 47, 10, 1 },  Rectangle { 32, 8, 34, 8, 1 },    Rectangle { 31, 9, 46, 9, 1 },
                                             Rectangle { 31, 0, 32, 8, 1 },   Rectangle { 33, 6, 34, 8, 1 },   Rectangle { 35, 7, 39, 8, 1 },   Rectangle { 66, 0, 68, 12, 4 }, Rectangle { 53, 11, 65, 13, 4 }, Rectangle { 50, 11, 52, 16, 4 }, Rectangle { 67, 0, 67, 12, 4 },   Rectangle { 52, 11, 67, 12, 4 },  Rectangle { 51, 11, 51, 16, 4 },
                                             Rectangle { 49, 10, 65, 10, 1 }, Rectangle { 48, 8, 50, 8, 1 },   Rectangle { 49, 9, 65, 9, 1 },   Rectangle { 63, 0, 65, 8, 1 },  Rectangle { 63, 6, 65, 8, 1 },   Rectangle { 58, 7, 62, 8, 1 },   Rectangle { 33, 0, 46, 6, 1.4f }, Rectangle { 50, 0, 60, 6, 1.4f }, Rectangle { 46, 0, 49, 4, 1.4f } };
    std::vector<float> heights(grid.height_samples.begin(), grid.height_samples.end());
    const auto stride = static_cast<std::size_t>(grid.width_cells) + 1;
    for (const auto& rectangle : rectangles) {
        for (auto y = rectangle.y0; y <= std::min(rectangle.y1, grid.height_cells); ++y) {
            for (auto x = rectangle.x0; x <= std::min(rectangle.x1, grid.width_cells); ++x) {
                heights[x + static_cast<std::size_t>(y) * stride] += rectangle.delta;
            }
        }
    }
    return heights;
}
} //namespace

void TerrainSkyShadowCache::clear() {
    valid_ = false;
    source_.reset();
    visibility_.reset();
    vertices_.clear();
    indices_.clear();
    grid_.height_samples.clear();
    grid_.cell_metadata.clear();
}

void TerrainSkyShadowCache::append(MeshScene& scene, const TerrainGridView* grid, const WeatherFrame& frame, const MeshMatrix& view_projection, bool cull_to_view) {
    FrameTimer timer { FrameStage::sky_shadow };
    //Native requires the height workspace and sky source, independently of
    //SKYQUALITY, SKYDETAIL, lighting and ordinary terrain's excluded-cell bit.
    if (!frame.sky_shadow || !grid || !frame.sky) {
        clear();
        return;
    }
    validate(*grid);
    const auto offset = frame.sky_offsets[0];
    if (!std::ranges::all_of(offset, [](float value) {
            return std::isfinite(value);
        })) {
        throw std::runtime_error("Invalid terrain sky-shadow offset");
    }
    //6DC795/6DC7A4: hardware ordinary/fogged height biases are .6/.25.
    const auto height_bias = (frame.camera_flags & 0x16u) ? 0.25f : 0.60000002f;
    const bool geometry_changed = !valid_ || height_bias_ != height_bias || !same_grid(grid_, *grid);
    const bool uv_rebuild = geometry_changed || source_ != frame.sky || view_projection_ != view_projection;
    if (geometry_changed) {
        if (active_frame_profile) {
            ++active_frame_profile->counters.sky_geometry_rebuilds;
        }
        valid_ = false;
        vertices_.clear();
        indices_.clear();
        const auto heights = shadow_heights(*grid);
        const auto width = static_cast<std::size_t>(grid->width_cells), stride = width + 1;
        vertices_.reserve(grid->cell_metadata.size() * 4);
        indices_.reserve(grid->cell_metadata.size() * 6);
        for (std::size_t y = 0; y < grid->height_cells; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                const auto base = static_cast<std::uint32_t>(vertices_.size());
                const auto tl = x + y * stride;
                const std::array samples { tl, tl + stride, tl + stride + 1, tl + 1 };
                //6D3111..6D31A8: binary32 column base is reused by the right corner.
                const double column = static_cast<double>(x) * 0.023;
                const auto column_stored = static_cast<float>(column);
                const double row = static_cast<double>(y) * 0.023;
                const float left = static_cast<float>(column + offset[0]);
                const float right = static_cast<float>(offset[0] + 0.023 + column_stored);
                const float lower = static_cast<float>(offset[2] - row);
                const float upper = static_cast<float>(offset[2] + 0.023 - row);
                const std::array<std::array<float, 2>, 4> uv { { { left, upper }, { left, lower }, { right, lower }, { right, upper } } };
                const bool alternate = (static_cast<std::uint16_t>(grid->cell_metadata[x + y * width].flags) & 2u) != 0;
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    const auto physical = alternate ? corner : (corner + 1) % 4;
                    const auto sample = samples[physical];
                    SceneVertex vertex;
                    vertex.position = { static_cast<float>(static_cast<double>(sample % stride) * grid->cell_scale_x + grid->origin_x), heights[sample] + height_bias, static_cast<float>(static_cast<double>(sample / stride) * grid->cell_scale_y + grid->origin_y) };
                    vertex.uv = uv[physical];
                    //6F1D6F initializes black with alpha79. Top samples test row==0;
                    //lower samples test row==-1, not last_row. Preserve that literal.
                    vertex.diffuse = mesh_argb_color(y == 0 && (physical == 0 || physical == 3) ? 0u : 0x4F000000u);
                    vertices_.push_back(vertex);
                }
                indices_.insert(indices_.end(), { base, base + 2, base + 3, base, base + 1, base + 2 });
            }
        }
        //Copies retain independent geometry; detach the matching bounds on rebuild.
        if (!visibility_ || !visibility_.unique()) {
            visibility_ = std::make_shared<TerrainVisibilityCache>();
        }
        visibility_->rebuild(grid->width_cells, grid->height_cells, [&](std::size_t x, std::size_t y) {
            const auto at = (x + y * grid->width_cells) * 4;
            return std::array { vertices_[at].position, vertices_[at + 1].position, vertices_[at + 2].position, vertices_[at + 3].position };
        });
        grid_.origin_x = grid->origin_x;
        grid_.origin_y = grid->origin_y;
        grid_.cell_scale_x = grid->cell_scale_x;
        grid_.cell_scale_y = grid->cell_scale_y;
        grid_.width_cells = grid->width_cells;
        grid_.height_cells = grid->height_cells;
        grid_.height_samples.assign(grid->height_samples.begin(), grid->height_samples.end());
        grid_.cell_metadata.assign(grid->cell_metadata.begin(), grid->cell_metadata.end());
        view_projection_ = view_projection;
        source_ = frame.sky;
        height_bias_ = height_bias;
        valid_ = true;
    } else if (uv_rebuild) {
        for (std::size_t y = 0; y < grid->height_cells; ++y) {
            for (std::size_t x = 0; x < grid->width_cells; ++x) {
                const double column = static_cast<double>(x) * 0.023;
                const auto column_stored = static_cast<float>(column);
                const double row = static_cast<double>(y) * 0.023;
                const float left = static_cast<float>(column + offset[0]);
                const float right = static_cast<float>(offset[0] + 0.023 + column_stored);
                const float lower = static_cast<float>(offset[2] - row);
                const float upper = static_cast<float>(offset[2] + 0.023 - row);
                const std::array<std::array<float, 2>, 4> uv { { { left, upper }, { left, lower }, { right, lower }, { right, upper } } };
                const auto cell = x + y * grid->width_cells;
                const bool alternate = (static_cast<std::uint16_t>(grid->cell_metadata[cell].flags) & 2u) != 0;
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    vertices_[cell * 4 + corner].uv = uv[alternate ? corner : (corner + 1) % 4];
                }
            }
        }
    } else {
        //6D2C55..6D2CA0 deliberately differ from rebuild's X/Z mapping above:
        //cached U receives delta Z and cached V receives delta X. Keep native
        //binary32 delta storage/addition rather than recomputing absolute UVs.
        if (active_frame_profile) {
            ++active_frame_profile->counters.sky_uv_updates;
        }
        const auto du = static_cast<float>(static_cast<double>(offset[2]) - sky_offset_[2]);
        const auto dv = static_cast<float>(static_cast<double>(offset[0]) - sky_offset_[0]);
        for (auto& vertex : vertices_) {
            vertex.uv[0] += du;
            vertex.uv[1] += dv;
        }
    }
    view_projection_ = view_projection;
    source_ = frame.sky;
    sky_offset_ = offset;
    if (vertices_.empty() || indices_.empty()) {
        return;
    }
    if (scene.vertices.size() > std::numeric_limits<std::uint32_t>::max() - vertices_.size() || scene.indices.size() > std::numeric_limits<std::uint32_t>::max() - indices_.size()) {
        throw std::overflow_error("Terrain sky-shadow scene capacity");
    }
    SceneDraw draw;
    draw.stage = SceneDrawStage::terrain_sky_shadow;
    draw.first_index = static_cast<std::uint32_t>(scene.indices.size());
    draw.index_count = static_cast<std::uint32_t>(indices_.size());
    //Colors are already authored (black/alpha79), not computed lighting.
    //Leave unlit override clear: that shader override substitutes a solid mask.
    draw.texture = source_;
    draw.cull_back_faces = true;
    //6F556D forces sky source mode alpha-low and wrapped UVs. 6D2D03
    //clears alpha-test bits and disables depth writes. Fog is view-derived.
    draw.material.flags = 0x400956u | ((frame.camera_flags & 0x16u) ? world_fog_material_flag : 0u);
    draw.fog_eligibility = WorldFogEligibility::native_geometry;
    if (cull_to_view) {
        visibility_->update(view_projection);
        for (std::size_t cell = 0; cell < vertices_.size() / 4; ++cell) {
            if (!visibility_->admits(cell)) {
                continue;
            }
            const auto base = static_cast<std::uint32_t>(scene.vertices.size());
            scene.vertices.insert(scene.vertices.end(), vertices_.begin() + static_cast<std::ptrdiff_t>(cell * 4), vertices_.begin() + static_cast<std::ptrdiff_t>(cell * 4 + 4));
            for (std::size_t i = 0; i < 6; ++i) {
                scene.indices.push_back(base + indices_[cell * 6 + i] - static_cast<std::uint32_t>(cell * 4));
            }
        }
        draw.index_count = static_cast<std::uint32_t>(scene.indices.size() - draw.first_index);
    } else {
        const auto base = static_cast<std::uint32_t>(scene.vertices.size());
        scene.vertices.insert(scene.vertices.end(), vertices_.begin(), vertices_.end());
        for (const auto index : indices_) {
            scene.indices.push_back(base + index);
        }
    }
    if (!draw.index_count) {
        return;
    }
    scene.draws.push_back(std::move(draw));
}
} //namespace simcoaster
