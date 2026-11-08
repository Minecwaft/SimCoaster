#include "terrain_scene_cache.hpp"
#include "simcoaster/frame_profile.hpp"
#include "simcoaster/mesh_scene.hpp"
#include "simcoaster/park_construction_refresh.hpp"
#include "simcoaster/terrain_texture.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
void require(bool valid) {
    if (!valid)
        throw std::runtime_error("Invalid terrain scene geometry");
}

MeshPoint cross(const MeshPoint& a, const MeshPoint& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

float dot(const MeshPoint& a, const MeshPoint& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

MeshPoint normalized(MeshPoint value) {
    const auto length = std::sqrt(dot(value, value));
    for (auto& component : value)
        component /= length;
    return value;
}

void validate(const TerrainGridView& grid) {
    const auto width = static_cast<std::size_t>(grid.width_cells), height = static_cast<std::size_t>(grid.height_cells);
    require(width
            && height
            && width < std::numeric_limits<std::size_t>::max() / height
            && grid.height_samples.size() == (width + 1) * (height + 1)
            && grid.cell_metadata.size() == width * height);
}
} //namespace

std::array<std::uint8_t, 4> terrain_cell_uv_selectors(TerrainCellFlags flags) {
    const auto bits = static_cast<std::uint16_t>(flags);
    std::array<std::uint8_t, 4> result{ 0, 1, 2, 3 };
    if (bits & 0x40u)
        std::swap(result[1], result[3]);
    if (bits & 8u)
        std::rotate(result.begin(), result.end() - 1, result.end());
    else if (bits & 0x10u)
        std::rotate(result.begin(), result.begin() + 2, result.end());
    else if (bits & 0x20u)
        std::rotate(result.begin(), result.begin() + 1, result.end());
    return result;
}

MeshBounds terrain_grid_bounds(const TerrainGridView& grid) {
    validate(grid);
    const auto heights = std::minmax_element(grid.height_samples.begin(), grid.height_samples.end());
    return { { grid.origin_x, *heights.first, grid.origin_y },
             { grid.origin_x + static_cast<float>(grid.width_cells) * grid.cell_scale_x,
               *heights.second,
               grid.origin_y + static_cast<float>(grid.height_cells) * grid.cell_scale_y } };
}

MeshPoint terrain_vertex_normal(const TerrainGridView& grid, std::size_t x, std::size_t y) {
    const auto width = static_cast<std::size_t>(grid.width_cells), height = static_cast<std::size_t>(grid.height_cells),
               stride = width + 1;
    require(width && height && x <= width && y <= height);
    const auto at = x + y * stride;
    require(grid.height_samples.size() > at && grid.height_samples.size() > std::min(y + 1, height) * stride + x);
    const auto left = x ? at - 1 : at, right = x < width ? at + 1 : at, above = y ? at - stride : at,
               below = y < height ? at + stride : at;
    const auto span = grid.height_range_max - grid.height_range_min, scale = span != 0 ? 1.0f / span : 0.0625f;
    MeshPoint normal{ (grid.height_samples[left] - grid.height_samples[right]) * scale,
                      1,
                      (grid.height_samples[above] - grid.height_samples[below]) * scale };
    const auto cell = (x ? x - 1 : 0) + (y ? y - 1 : 0) * width;
    require(cell < grid.cell_metadata.size());
    //Preserve the observed asymmetric writes, not an invented normalization:
    //flag2000 quarters Y, quarters Z twice, and leaves X unchanged.
    if (static_cast<std::uint16_t>(grid.cell_metadata[cell].flags) & 0x2000u) {
        normal[1] *= 0.25f;
        normal[2] *= 0.25f;
        normal[2] *= 0.25f;
    }
    //Native bottom-row access uses -128 even for97-wide data. The owned x64
    //grid uses its actual row stride. Point lights receive actual X/Z positions.
    return normal;
}

void initialize_terrain_slope_flags(TerrainGridData& data) {
    const auto grid = data.view();
    validate(grid);
    const auto stride = static_cast<std::size_t>(grid.width_cells) + 1;
    for (std::size_t y = 0; y < grid.height_cells; ++y)
        for (std::size_t x = 0; x < grid.width_cells; ++x) {
            const auto at = x + y * stride;
            const auto tl = grid.height_samples[at], tr = grid.height_samples[at + 1],
                       bl = grid.height_samples[at + stride], br = grid.height_samples[at + stride + 1];
            const auto first = cross(normalized({ 0, bl - tl, 10 }), normalized({ 10, tr - tl, 0 }));
            const auto second = cross(normalized({ 0, tr - br, -10 }), normalized({ -10, bl - br, 0 }));
            if (dot(first, second) < 0.99989998f)
                data.cell_metadata[x + y * grid.width_cells].flags = static_cast<TerrainCellFlags>(
                    static_cast<std::uint16_t>(data.cell_metadata[x + y * grid.width_cells].flags) | 0x800u);
        }
}

void append_terrain_scene(MeshScene& scene,
                          const MeshModel& model,
                          MeshTextureLibrary& textures,
                          const MeshLighting& lighting,
                          bool dim_shadows,
                          const TerrainTextureAtlas* atlas,
                          std::span<const std::uint8_t> excluded_cells) {
    TerrainSceneCache cache;
    append_cached_terrain_scene(scene, model, textures, lighting, dim_shadows, atlas, excluded_cells, cache, nullptr);
}

void TerrainSceneCache::prepare(const TerrainGridView& grid, const MeshLighting& lighting) {
    const bool geometry_changed =
        !valid_
        || grid_.width_cells != grid.width_cells
        || grid_.height_cells != grid.height_cells
        || grid_.origin_x != grid.origin_x
        || grid_.origin_y != grid.origin_y
        || grid_.cell_scale_x != grid.cell_scale_x
        || grid_.cell_scale_y != grid.cell_scale_y
        || grid_.height_range_min != grid.height_range_min
        || grid_.height_range_max != grid.height_range_max
        || !std::ranges::equal(grid_.height_samples, grid.height_samples)
        || grid_.cell_metadata.size() != grid.cell_metadata.size()
        || !std::ranges::equal(grid_.cell_metadata, grid.cell_metadata, [](const auto& a, const auto& b) {
               return (static_cast<std::uint16_t>(a.flags) & 0x2000u)
                   == (static_cast<std::uint16_t>(b.flags) & 0x2000u);
           });
    const bool light_changed = !valid_
                            || lighting_.ambient != lighting.ambient
                            || lighting_.diffuse_mask != lighting.diffuse_mask
                            || !std::ranges::equal(lighting_.lights, lighting.lights, [](const auto& a, const auto& b) {
                                   return a.kind == b.kind
                                       && a.color == b.color
                                       && a.position == b.position
                                       && a.direction == b.direction
                                       && a.radius_squared == b.radius_squared
                                       && a.attenuation == b.attenuation;
                               });
    if (!geometry_changed && !light_changed)
        return;
    if (geometry_changed) {
        positions.resize(grid.height_samples.size());
        normals.resize(positions.size());
        const auto stride = static_cast<std::size_t>(grid.width_cells) + 1;
        for (std::size_t y = 0; y <= grid.height_cells; ++y)
            for (std::size_t x = 0; x <= grid.width_cells; ++x) {
                const auto at = x + y * stride;
                positions[at] = { static_cast<float>(x) * grid.cell_scale_x + grid.origin_x,
                                  grid.height_samples[at],
                                  static_cast<float>(y) * grid.cell_scale_y + grid.origin_y };
                normals[at] = terrain_vertex_normal(grid, x, y);
            }
        visibility.rebuild(grid.width_cells, grid.height_cells, [&](std::size_t x, std::size_t y) {
            const auto tl = x + y * stride;
            return std::array{ positions[tl], positions[tl + 1], positions[tl + stride], positions[tl + stride + 1] };
        });
        grid_.origin_x = grid.origin_x;
        grid_.origin_y = grid.origin_y;
        grid_.cell_scale_x = grid.cell_scale_x;
        grid_.cell_scale_y = grid.cell_scale_y;
        grid_.width_cells = grid.width_cells;
        grid_.height_cells = grid.height_cells;
        grid_.height_range_min = grid.height_range_min;
        grid_.height_range_max = grid.height_range_max;
        grid_.height_samples.assign(grid.height_samples.begin(), grid.height_samples.end());
        grid_.cell_metadata.assign(grid.cell_metadata.begin(), grid.cell_metadata.end());
    }
    if (active_frame_profile)
        ++active_frame_profile->counters.terrain_lighting_rebuilds;
    colors.resize(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i)
        colors[i] = mesh_vertex_lighting(lighting, positions[i], normals[i]);
    lighting_ = lighting;
    valid_ = true;
}

void append_cached_terrain_scene(MeshScene& scene,
                                 const MeshModel& model,
                                 MeshTextureLibrary& textures,
                                 const MeshLighting& lighting,
                                 bool dim_shadows,
                                 const TerrainTextureAtlas* atlas,
                                 std::span<const std::uint8_t> excluded_cells,
                                 TerrainSceneCache& cache,
                                 const MeshMatrix* view_projection) {
    FrameTimer timer{ FrameStage::terrain };
    if (!model.terrain)
        return;
    const auto grid = model.terrain->view();
    validate(grid);
    require(excluded_cells.empty() || excluded_cells.size() == grid.cell_metadata.size());
    const auto width = static_cast<std::size_t>(grid.width_cells), stride = width + 1;
    if (scene.indices.empty())
        scene.bounds = {
            { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() },
            { -std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max() }
        };
    cache.prepare(grid, lighting);
    const auto& positions = cache.positions;
    const auto& colors = cache.colors;
    if (view_projection)
        cache.visibility.update(*view_projection);
    if (atlas)
        require(atlas->bindings().size() == grid.cell_metadata.size());
    const auto atlas_pages = atlas ? atlas->pages().size() : 0;
    std::vector<std::vector<std::size_t>> groups(atlas_pages + model.textures.size());
    for (std::size_t cell = 0; cell < grid.cell_metadata.size(); ++cell) {
        const auto& metadata = grid.cell_metadata[cell];
        if ((static_cast<std::uint16_t>(metadata.flags) & 1u) || (!excluded_cells.empty() && excluded_cells[cell]))
            continue;
        if (view_projection && !cache.visibility.admits(cell)) {
            if (active_frame_profile)
                ++active_frame_profile->counters.terrain_cells_culled;
            continue;
        }
        const auto group = atlas && atlas->bindings()[cell].procedural ? atlas->bindings()[cell].page
                                                                       : atlas_pages + metadata.resource_slot_index;
        require(group < groups.size());
        groups[group].push_back(cell);
    }
    constexpr std::array<std::array<float, 2>, 4> uv{ { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } } };
    constexpr std::array<std::uint16_t, 4> shadow_bits{ 0x4000, 0x1000, 0x2000, 0x8000 };
    for (std::size_t slot_index = 0; slot_index < groups.size(); ++slot_index)
        if (!groups[slot_index].empty()) {
            SceneDraw draw;
            draw.stage = SceneDrawStage::terrain_surface;
            const bool procedural = slot_index < atlas_pages;
            if (procedural)
                draw.texture = atlas->pages()[slot_index];
            else {
                const auto& slot = model.textures[slot_index - atlas_pages];
                draw.material_flags = slot.flags;
                const auto image = resolve_mesh_texture(scene, textures, slot);
                draw.texture = image ? image : untextured_mesh_texture();
                //The terrain batch keeps its fixed opaque material, but still uses
                //the acquired source's native705AC0 UV window like6D0EE0.
                draw.material.texture_uv = mesh_texture_uv_transform(*draw.texture, slot.flags);
            }
            require(draw.texture != nullptr && scene.indices.size() <= std::numeric_limits<std::uint32_t>::max());
            draw.first_index = static_cast<std::uint32_t>(scene.indices.size());
            draw.source_node = std::numeric_limits<std::size_t>::max();
            draw.diffuse_mask = mesh_argb_color(lighting.diffuse_mask);
            //The native terrain batch uses a fixed opaque state, independent of
            //the texture source's ordinary mesh alpha classification.
            //Atlas gutters preserve clamp sampling. Disable automatic whole-page
            //mipmaps, which would mix unrelated cells at the smaller mip levels.
            draw.material.flags = 0x1256u | (procedural ? 0x40000u : 0u);
            //6D0FC7 adds the fog material bit to this surface batch when the
            //active World view enables it; texture alpha does not choose fog.
            draw.fog_eligibility = WorldFogEligibility::native_geometry;
            for (const auto cell : groups[slot_index]) {
                if (active_frame_profile)
                    ++active_frame_profile->counters.terrain_cells;
                require(scene.vertices.size() <= std::numeric_limits<std::uint32_t>::max() - 4
                        && scene.indices.size() <= std::numeric_limits<std::uint32_t>::max() - 6);
                const auto base = static_cast<std::uint32_t>(scene.vertices.size());
                const auto x = cell % width, y = cell / width, tl = x + y * stride;
                const std::array samples{ tl, tl + stride, tl + stride + 1, tl + 1 };
                const auto flags = static_cast<std::uint16_t>(grid.cell_metadata[cell].flags);
                const auto selectors = terrain_cell_uv_selectors(grid.cell_metadata[cell].flags);
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    const auto selected = flags & 2u ? corner : (corner + 1) % 4;
                    SceneVertex vertex;
                    vertex.position = positions[samples[selected]];
                    vertex.uv = uv[selectors[selected]];
                    if (procedural) {
                        const auto& binding = atlas->bindings()[cell];
                        for (std::size_t axis = 0; axis < 2; ++axis)
                            vertex.uv[axis] = binding.origin[axis] + vertex.uv[axis] * binding.extent[axis];
                    }
                    vertex.diffuse = colors[samples[selected]];
                    if (dim_shadows && (flags & shadow_bits[selected])) {
                        for (std::size_t channel = 0; channel < 3; ++channel)
                            vertex.diffuse[channel] >>= 1;
                        vertex.diffuse[3] = 0;
                    }
                    scene.vertices.push_back(vertex);
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        scene.bounds.minimum[axis] = std::min(scene.bounds.minimum[axis], vertex.position[axis]);
                        scene.bounds.maximum[axis] = std::max(scene.bounds.maximum[axis], vertex.position[axis]);
                        draw.center[axis] += vertex.position[axis];
                    }
                }
                scene.indices.insert(scene.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
            }
            draw.index_count = static_cast<std::uint32_t>(scene.indices.size() - draw.first_index);
            for (auto& component : draw.center)
                component /= static_cast<float>(groups[slot_index].size() * 4);
            scene.draws.push_back(std::move(draw));
        }
}
} //namespace simcoaster
