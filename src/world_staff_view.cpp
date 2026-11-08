#include "simcoaster/frame_profile.hpp"
#include "simcoaster/world_staff_view.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <utility>

namespace simcoaster {
namespace {
using Vec = std::array<float, 3>;

Vec subtract(const Vec& a, const Vec& b) {
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

Vec cross(const Vec& a, const Vec& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

float dot(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float triangle(const Vec& origin, const Vec& direction, const Vec& a, const Vec& b, const Vec& c) {
    const auto e1 = subtract(b, a), e2 = subtract(c, a), p = cross(direction, e2);
    const auto determinant = dot(e1, p);
    if (std::fabs(determinant) < .000001f)
        return std::numeric_limits<float>::infinity();
    const auto inverse = 1.f / determinant;
    const auto t = subtract(origin, a);
    const auto u = dot(t, p) * inverse;
    if (u < 0 || u > 1)
        return std::numeric_limits<float>::infinity();
    const auto q = cross(t, e1);
    const auto v = dot(direction, q) * inverse;
    if (v < 0 || u + v > 1)
        return std::numeric_limits<float>::infinity();
    const auto distance = dot(e2, q) * inverse;
    return distance >= 0 ? distance : std::numeric_limits<float>::infinity();
}
} //namespace

std::optional<StaffInteractionCursor> pick_staff_interaction_cursor(const SceneCamera& camera,
                                                                    const TerrainGridView& grid,
                                                                    float px,
                                                                    float py,
                                                                    std::uint32_t width,
                                                                    std::uint32_t height) {
    if (!width || !height || px < 0 || py < 0 || px >= static_cast<float>(width) || py >= static_cast<float>(height))
        return std::nullopt;
    if (!grid.width_cells
        || !grid.height_cells
        || grid.height_samples.size() != (static_cast<std::size_t>(grid.width_cells) + 1) * (grid.height_cells + 1)
        || grid.cell_metadata.size() != static_cast<std::size_t>(grid.width_cells) * grid.height_cells)
        throw std::invalid_argument("staff picking terrain");
    const auto view = camera.view();
    const auto origin = camera.eye();
    const auto focal = std::tan(camera.vertical_fov * .5f);
    const auto nx = (2.f * px / static_cast<float>(width) - 1.f)
                  * focal
                  * static_cast<float>(width)
                  / static_cast<float>(height),
               ny = (1.f - 2.f * py / static_cast<float>(height)) * focal;
    Vec direction{ view[2] + nx * view[0] + ny * view[1],
                   view[6] + nx * view[4] + ny * view[5],
                   view[10] + nx * view[8] + ny * view[9] };
    const auto length = std::sqrt(dot(direction, direction));
    for (auto& value : direction)
        value /= length;
    float nearest = std::numeric_limits<float>::infinity();
    CellId selected = 0;
    const auto vertex = [&](std::uint32_t x, std::uint32_t y) -> Vec {
        return { grid.origin_x + static_cast<float>(x) * grid.cell_scale_x,
                 grid.height_samples[static_cast<std::size_t>(y) * (grid.width_cells + 1) + x],
                 grid.origin_y + static_cast<float>(y) * grid.cell_scale_y };
    };
    for (std::uint32_t y = 0; y < grid.height_cells; ++y)
        for (std::uint32_t x = 0; x < grid.width_cells; ++x) {
            if (x >= 128 || y >= 128)
                continue;
            const auto at = static_cast<std::size_t>(y) * grid.width_cells + x;
            const auto flags = std::to_underlying(grid.cell_metadata[at].flags);
            if (flags & 1u)
                continue;
            std::array<Vec, 4> corners{ vertex(x, y), vertex(x, y + 1), vertex(x + 1, y + 1), vertex(x + 1, y) };
            if (!(flags & 2u))
                std::rotate(corners.begin(), corners.begin() + 1, corners.end());
            const auto distance = std::min(triangle(origin, direction, corners[0], corners[1], corners[2]),
                                           triangle(origin, direction, corners[0], corners[2], corners[3]));
            if (distance < nearest) {
                nearest = distance;
                selected = static_cast<CellId>(x + y * 128 + 1);
            }
        }
    if (!selected)
        return std::nullopt;
    StaffInteractionCursor result;
    result.cell = selected;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.ground_point[axis] = origin[axis] + direction[axis] * nearest;
        result.ray_point[axis] = origin[axis] + direction[axis] * 60.f;
    }
    return result;
}

ThingId find_staff_at_interaction_cursor(StaffWorld& c) {
    const auto cell = c.staff.interaction.cursor.cell;
    if (!cell || !c.referenced_world())
        return 0;
    auto id = c.referenced_world()->map_cells.at(cell - 1).who;
    for (std::size_t seen = 0; id; ++seen) {
        if (seen >= c.entities.size())
            throw std::logic_error("cyclic staff picking map list");
        const auto* record = c.entities.at(id).payload;
        if (!record)
            throw std::logic_error("missing staff picking record");
        if (is_staff_record_kind(record->kind) && !record->retirement_pending)
            return id;
        id = reinterpret_cast<const MapObjectBase*>(record)->map_child;
    }
    return 0;
}
} //namespace simcoaster
