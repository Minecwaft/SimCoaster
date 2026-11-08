#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_cursor_pick.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_surface.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace simcoaster {
namespace {
using Point = std::array<float, 3>;

double dot(const Point& a, const Point& b) {
    return static_cast<double>(a[0]) * b[0] + static_cast<double>(a[1]) * b[1] + static_cast<double>(a[2]) * b[2];
}

Point minus(const Point& a, const Point& b) {
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

Point scaled_point(const Point& origin, const Point& direction, double t) {
    Point result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result[axis] = static_cast<float>(origin[axis] + t * direction[axis]);
    }
    return result;
}

std::optional<Point> normalized(Point point) {
    const auto length = std::sqrt(dot(point, point));
    if (!(length > 0) || !std::isfinite(length)) {
        return {};
    }
    for (auto& value : point) {
        value = static_cast<float>(value / length);
    }
    return point;
}

Point direction(const MeshMatrix& matrix, const Point& value) {
    Point result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result[axis] = static_cast<float>(static_cast<double>(value[0]) * matrix[axis] + static_cast<double>(value[2]) * matrix[axis + 8] + static_cast<double>(value[1]) * matrix[axis + 4]);
    }
    return result;
}

bool opposite(double a, double b) {
    return (a < 0 && b > 0) || (a > 0 && b < 0);
}

std::int32_t cell(float position) {
    return fixed::truncate_integer(static_cast<double>(position) * .1);
}

Point cursor_direction(const SceneCamera& camera, std::array<std::int32_t, 2> pixel, std::uint32_t width, std::uint32_t height) {
    const auto inverse = mesh_transform::inverse(camera.view());
    const auto tangent = std::tan(camera.vertical_fov * .5f);
    const auto x = (2.f * static_cast<float>(pixel[0]) / static_cast<float>(width) - 1.f) * tangent * static_cast<float>(width) / static_cast<float>(height), y = (1.f - 2.f * static_cast<float>(pixel[1]) / static_cast<float>(height)) * tangent;
    const auto ray = normalized(direction(inverse, { x, y, 1 }));
    return require_bound<std::invalid_argument>(ray, "World cursor picker camera ray is invalid");
}

struct Picker {
    WorldRuntime& c;
    const TerrainGridView& terrain;
    Point origin, ray;
    double far_distance;
    std::map<std::uint32_t, std::vector<MeshMatrix>> poses;

    std::optional<WorldCursorMeshHit> triangle(const Point& a, const Point& b, const Point& corner_c, const Point& normal, std::uint32_t slot) const {
        const auto divisor = dot(normal, ray);
        if (divisor == 0 || !std::isfinite(divisor)) {
            return {};
        }
        const auto t = -dot(normal, minus(origin, a)) / divisor;
        if (!std::isfinite(t) || std::abs(t) > far_distance) {
            return {};
        }
        const auto hit = scaled_point(origin, ray, t);
        const auto first = normalized(minus(a, hit)), second = normalized(minus(b, hit)), third = normalized(minus(corner_c, hit));
        if (!first || !second || !third) {
            return {};
        }
        //4B9080 deliberately has no face-culling, positive-t, alpha, triangle
        //material, or nearest-triangle test. Unclamped acos reproduces rejection
        //when rounding puts a cosine outside its native domain.
        const auto angles = std::acos(dot(*third, *first)) + std::acos(dot(*second, *first)) + std::acos(dot(*third, *second));
        if (!(angles >= 6.2821855545043945)) {
            return {};
        }
        return WorldCursorMeshHit { slot, hit, normal };
    }

    std::optional<WorldCursorMeshHit> hierarchy(std::uint32_t slot) {
        auto* instance = c.resources.meshes->instance(slot);
        if (!instance) {
            throw std::out_of_range("Cursor picker candidate mesh was retired");
        }
        auto& model = instance->playback.model;
        if (!model.root) {
            return {};
        }
        auto found = poses.find(slot);
        if (found == poses.end()) {
            auto pose = evaluate_mesh_pose(model);
            if (!pose) {
                throw std::runtime_error(describe_mesh_error(pose.error()));
            }
            found = poses.emplace(slot, std::move(*pose)).first;
        }
        const auto& pose = found->second;
        std::vector<std::size_t> pending { *model.root };
        std::vector<bool> visited(model.nodes.size());
        while (!pending.empty()) {
            const auto index = pending.back();
            pending.pop_back();
            if (index >= model.nodes.size() || visited[index]) {
                throw std::logic_error("Cursor picker mesh hierarchy is cyclic");
            }
            visited[index] = true;
            const auto& node = model.nodes[index];
            if (!(node.flags & 0x8210u) && node.drawable) {
                const auto& data = *node.drawable;
                for (const auto& group : data.groups) {
                    for (std::size_t i = group.first_triangle; i < static_cast<std::size_t>(group.first_triangle) + group.triangle_count; ++i) {
                        const auto& face = data.triangles.at(i);
                        const auto normal = normalized(direction(pose.at(index), data.normals.at(face.normal_index_and_flags & 0x7FFFu)));
                        if (!normal) {
                            continue;
                        }
                        std::array<Point, 3> vertices;
                        for (std::size_t corner = 0; corner < vertices.size(); ++corner) {
                            vertices[corner] = mesh_transform::point(pose.at(index), data.positions.at(data.position_indices.at(face.corners[corner])));
                        }
                        if (auto hit = triangle(vertices[0], vertices[1], vertices[2], *normal, slot)) {
                            return hit;
                        }
                    }
                }
            }
            if (!(node.flags & 0x20u)) {
                std::vector<std::size_t> children;
                for (auto child = node.first_child; child; child = model.nodes.at(*child).next_sibling) {
                    if (children.size() >= model.nodes.size()) {
                        throw std::logic_error("Cursor picker mesh sibling cycle");
                    }
                    children.push_back(*child);
                }
                //Native recursion tests the node, then its children in linked order.
                pending.insert(pending.end(), children.rbegin(), children.rend());
            }
        }
        return {};
    }

    std::vector<std::uint32_t> candidates(std::uint32_t slot) const {
        const auto* instance = c.resources.meshes->instance(slot);
        if (!instance) {
            throw std::out_of_range("Cursor picker discovered a missing mesh");
        }
        if (!(instance->playback.runtime_flags & 0x80000u)) {
            return { slot };
        }
        const auto* coasters = c.resources.meshes->coasters();
        if (!coasters) {
            throw std::logic_error("Cursor picker pylon has no coaster owner");
        }
        for (const auto& placement : coasters->placements) {
            const bool owns = std::ranges::any_of(placement.links, [&](const auto& link) {
                return link && coasters->bindings.at(link->ride).geometry_state.at(link->node).mesh_slot == slot;
            });
            if (!owns) {
                continue;
            }
            std::vector<std::uint32_t> logical;
            const bool filtered = (placement.flags & 0x20u) != 0;
            for (const auto& link : placement.links) {
                if (!link) {
                    throw std::logic_error("Cursor picker pylon stack contains an empty physical link");
                }
                const auto& ride = coasters->bindings.at(link->ride);
                if (!filtered || !(ride.geometry.at(link->node).geometry_flags & 0x2000u)) {
                    logical.push_back(ride.geometry_state.at(link->node).mesh_slot);
                }
            }
            //48D1F0 subtracts exactly ONE from count when owner32 is set;
            //48D240 separately filters geometry8192 while resolving each index.
            const auto count = placement.links.size() - (filtered && !placement.links.empty() ? 1u : 0u);
            if (count > logical.size()) {
                throw std::logic_error("Cursor picker filtered pylon count is inconsistent");
            }
            logical.resize(count);
            std::ranges::reverse(logical);
            return logical;
        }
        throw std::logic_error("Cursor picker could not resolve the pylon placement owner");
    }

    std::optional<WorldCursorMeshHit> segment(Point point, bool major_x, float height_step, float signed_step, std::optional<std::array<float, 2>> vertical_interval = {}) {
        const auto x = std::clamp(cell(point[0]), 0, static_cast<std::int32_t>(terrain.width_cells) - 1);
        const auto z = std::clamp(cell(point[2]), 0, static_cast<std::int32_t>(terrain.height_cells) - 1);
        const auto surface = query_world_map_cell_surface(c, x, z);
        const auto resolved = surface.mesh_slot;
        auto sampled = surface.height;
        if (!resolved) {
            return {};
        }
        const auto options = candidates(resolved);
        const bool linked = (c.resources.meshes->instance(resolved)->playback.runtime_flags & 0x80000u) != 0;
        const auto coordinate = point[major_x ? 0 : 2];
        double integral {};
        const auto fraction = signed_step > 0 ? std::modf(static_cast<double>(coordinate) * .1, &integral) : 1. - std::fmod(coordinate, 10.) * .1;
        const auto first = vertical_interval ? (*vertical_interval)[0] : static_cast<float>(point[1] - fraction * height_step);
        const auto second = vertical_interval ? (*vertical_interval)[1] : static_cast<float>((1. - fraction) * height_step + point[1]);
        for (auto slot : options) {
            auto* instance = c.resources.meshes->instance(slot);
            if (!instance) {
                throw std::out_of_range("Cursor picker pylon mesh is missing");
            }
            if (linked) {
                sampled = query_world_map_cell_surface(c, x, z, slot).height;
            } else if ((instance->playback.model.flags & 0x60000u) == 0x60000u) {
                auto result = recompute_mesh_face_normals(instance->playback.model);
                if (!result) {
                    throw std::runtime_error(describe_mesh_error(result.error()));
                }
            }
            //An uncovered coarse-grid cell must not suppress real overhanging or
            //linked geometry. The hierarchy test still determines the actual hit.
            if ((!sampled || std::min(first, second) <= *sampled) && std::max(first, second) >= instance->playback.model.bounds.minimum[1]) {
                if (auto hit = hierarchy(slot)) {
                    return hit;
                }
            }
        }
        return {};
    }

    std::optional<WorldCursorMeshHit> ground() const {
        std::optional<WorldCursorMeshHit> result;
        double nearest = std::numeric_limits<double>::max();
        const auto clamped = [](double position, float offset, float scale, std::uint32_t count) {
            const auto value = (position - offset) / scale;
            if (!std::isfinite(value)) {
                return value < 0 ? 0 : static_cast<std::int32_t>(count) - 1;
            }
            return static_cast<std::int32_t>(std::clamp(std::trunc(value), 0., static_cast<double>(count - 1)));
        };
        std::array<std::int32_t, 2> first {}, last {};
        if (ray[1] != 0) {
            const auto upper = scaled_point(origin, ray, (terrain.height_range_max - origin[1]) / ray[1]);
            const auto lower = scaled_point(origin, ray, (terrain.height_range_min - origin[1]) / ray[1]);
            const auto ax = clamped(upper[0], terrain.origin_x, terrain.cell_scale_x, terrain.width_cells), bx = clamped(lower[0], terrain.origin_x, terrain.cell_scale_x, terrain.width_cells), az = clamped(upper[2], terrain.origin_y, terrain.cell_scale_y, terrain.height_cells), bz = clamped(lower[2], terrain.origin_y, terrain.cell_scale_y, terrain.height_cells);
            first = { std::min(ax, bx), std::min(az, bz) };
            last = { std::max(ax, bx), std::max(az, bz) };
        } else {
            last = { static_cast<std::int32_t>(terrain.width_cells) - 1, static_cast<std::int32_t>(terrain.height_cells) - 1 };
        }
        const auto xz = [&](double x, double z) {
            return -ray[0] * (z - origin[2]) + ray[2] * (x - origin[0]);
        };
        const auto xy = [&](double x, double y) {
            return -ray[0] * (y - origin[1]) + ray[1] * (x - origin[0]);
        };
        const bool opposite_xz = (ray[0] >= 0 || ray[2] >= 0) && (ray[0] <= 0 || ray[2] <= 0), opposite_xy = (ray[0] >= 0 || ray[1] >= 0) && (ray[0] <= 0 || ray[1] <= 0);
        for (auto z = first[1]; z <= last[1]; ++z) {
            for (auto x = first[0]; x <= last[0]; ++x) {
                const auto left = terrain.origin_x + static_cast<float>(x) * terrain.cell_scale_x, right = left + terrain.cell_scale_x, top = terrain.origin_y + static_cast<float>(z) * terrain.cell_scale_y, bottom = top + terrain.cell_scale_y;
                const bool vertical = ray[0] == 0 && ray[2] == 0;
                if (!vertical && !opposite(xz(left, opposite_xz ? top : bottom), xz(right, opposite_xz ? bottom : top))) {
                    continue;
                }
                const auto stride = static_cast<std::size_t>(terrain.width_cells) + 1;
                const auto index = static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * stride;
                const std::array heights { terrain.height_samples[index], terrain.height_samples[index + 1], terrain.height_samples[index + stride], terrain.height_samples[index + stride + 1] };
                const auto [minimum, maximum] = std::minmax_element(heights.begin(), heights.end());
                if (!vertical && !opposite(xy(left, opposite_xy ? *minimum : *maximum), xy(right, opposite_xy ? *maximum : *minimum))) {
                    continue;
                }
                const bool diagonal = (std::to_underlying(terrain.cell_metadata[static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * terrain.width_cells].flags) & 2u) != 0;
                const std::array<Point, 4> corners { { { left, heights[0], top }, { right, heights[1], top }, { left, heights[2], bottom }, { right, heights[3], bottom } } };
                //These are the TWO native planes, each accepted over the whole cell
                //rectangle. Deliberately do not add diagonal barycentric containment.
                const std::array<std::array<std::size_t, 3>, 2> triangles = diagonal ? std::array<std::array<std::size_t, 3>, 2> { { { 2, 3, 0 }, { 1, 0, 3 } } } : std::array<std::array<std::size_t, 3>, 2> { { { 0, 1, 2 }, { 3, 2, 1 } } };
                for (const auto indices : triangles) {
                    const auto& a = corners[indices[0]];
                    const auto b = minus(corners[indices[1]], a), d = minus(corners[indices[2]], a);
                    const auto normal = normalized({ b[1] * d[2] - b[2] * d[1], b[2] * d[0] - b[0] * d[2], b[0] * d[1] - b[1] * d[0] });
                    if (!normal || dot(*normal, ray) == 0) {
                        continue;
                    }
                    const auto t = -dot(*normal, minus(origin, a)) / dot(*normal, ray);
                    const auto point = scaled_point(origin, ray, t);
                    //Native4B8368/4B8386 accidentally use X spacing for Z bounds.
                    const auto min_z = terrain.origin_y + static_cast<float>(z) * terrain.cell_scale_x, max_z = min_z + terrain.cell_scale_x;
                    if (std::abs(t) <= far_distance && point[0] >= left && point[0] <= right && point[2] >= min_z && point[2] <= max_z && std::abs(t) < nearest) {
                        nearest = std::abs(t);
                        result = WorldCursorMeshHit { 0, point, *normal };
                    }
                }
            }
        }
        return result;
    }

    std::optional<WorldCursorMeshHit> walk(const std::optional<WorldCursorMeshHit>& terrain_hit) {
        const bool major_x = std::abs(ray[0]) > std::abs(ray[2]);
        const auto axis = major_x ? 0u : 2u, minor_axis = major_x ? 2u : 0u;
        if (ray[axis] == 0) {
            //The original divides by zero for a vertical ray. This bounded branch
            //uses the same cell candidate order over its full vertical interval.
            return segment(origin, major_x, 0, 0, std::array { origin[1], terrain_hit ? terrain_hit->position[1] : scaled_point(origin, ray, far_distance)[1] });
        }
        const auto step = ray[axis] <= 0 ? -10.f : 10.f, minor_step = ray[minor_axis] / ray[axis] * step, height_step = ray[1] / ray[axis] * step;
        const auto stop = terrain_hit ? height_step + terrain_hit->position[1] : 0.f;
        if (origin[1] < stop) {
            return {};
        }
        auto point = origin;
        double distance {};
        const auto step_distance = std::sqrt(static_cast<double>(step) * step + static_cast<double>(minor_step) * minor_step + static_cast<double>(height_step) * height_step);
        //Approved repair of4B863D: EVERY slope needs a distance bound. A tiny
        //negative height step can round away at each float store, so the native
        //height-only termination never fires even for a downward ray.
        do {
            const auto old_minor_cell = cell(point[minor_axis]);
            if (auto hit = segment(point, major_x, height_step, step)) {
                return hit;
            }
            const auto previous = point;
            const auto remaining = far_distance - distance;
            if (remaining <= 0) {
                break;
            }
            if (step_distance > remaining) {
                //Visit the final partial interval too: a cell boundary can precede
                //the far plane even when the next full native10-unit step would not.
                point = scaled_point(previous, ray, remaining);
                distance = far_distance;
            } else {
                point[axis] += step;
                point[minor_axis] += minor_step;
                point[1] += height_step;
                distance += step_distance;
            }
            if (point == previous || !std::isfinite(distance)) {
                break;
            }
            if (cell(point[minor_axis]) != old_minor_cell && point[0] >= 0 && point[2] >= 0) {
                const auto before = static_cast<double>(previous[minor_axis]) * .1, after = static_cast<double>(point[minor_axis]) * .1;
                double integral {};
                const auto fraction = static_cast<float>((before < after ? 1. - std::modf(after, &integral) / (after - before) : std::modf(before, &integral) / (before - after)) * 1.01);
                Point sample;
                for (std::size_t i = 0; i < sample.size(); ++i) {
                    sample[i] = fraction * point[i] + (1.f - fraction) * previous[i];
                }
                if (auto hit = segment(sample, major_x, height_step, step)) {
                    return hit;
                }
            }
        } while (height_step >= 0 || point[1] >= stop);
        return {};
    }
};
} //namespace

std::optional<WorldCursorPick> pick_world_cursor(WorldRuntime& c, const SceneCamera& camera, const TerrainGridView& terrain, std::array<std::int32_t, 2> pixel, std::uint32_t width, std::uint32_t height) {
    if (!width || !height || pixel[0] < 0 || pixel[1] < 0 || static_cast<std::uint32_t>(pixel[0]) >= width || static_cast<std::uint32_t>(pixel[1]) >= height) {
        return {};
    }
    if (!c.resources.meshes || !c.simulation.current) {
        throw std::logic_error("World cursor picking requires live mesh and World owners");
    }
    if (!std::isfinite(camera.far_plane) || camera.far_plane <= 0) {
        throw std::invalid_argument("World cursor picker far plane is invalid");
    }
    if (!terrain.width_cells || !terrain.height_cells || terrain.cell_scale_x <= 0 || terrain.cell_scale_y <= 0 || terrain.height_samples.size() < (static_cast<std::size_t>(terrain.width_cells) + 1) * (static_cast<std::size_t>(terrain.height_cells) + 1) || terrain.cell_metadata.size() < static_cast<std::size_t>(terrain.width_cells) * terrain.height_cells) {
        throw std::invalid_argument("World cursor picker terrain grid is invalid");
    }
    const auto ray = cursor_direction(camera, pixel, width, height);
    const auto depth_per_unit = direction(camera.view(), ray)[2];
    if (!(depth_per_unit > 0) || !std::isfinite(depth_per_unit)) {
        throw std::invalid_argument("World cursor picker view direction is invalid");
    }
    //The renderer clips camera-space Z, not radial world distance. Retain
    //visible edge-of-screen and scaled/rolled camera picks inside that plane.
    const auto far_distance = static_cast<double>(camera.far_plane) / depth_per_unit;
    Picker picker { c, terrain, camera.eye(), ray, far_distance, {} };
    const auto ground = picker.ground();
    const auto mesh = picker.walk(ground);
    if (!ground && !mesh) {
        return {};
    }
    const bool mesh_wins = mesh && (!ground || dot(minus(mesh->position, picker.origin), minus(mesh->position, picker.origin)) < dot(minus(ground->position, picker.origin), minus(ground->position, picker.origin)));
    const auto& surface = mesh_wins ? *mesh : *ground;
    return WorldCursorPick { surface.position, surface.normal, mesh_wins ? mesh->mesh_slot : 0u, ground.has_value(), mesh };
}

std::optional<StaffInteractionCursor> pick_world_interaction_cursor(WorldRuntime& c, const SceneCamera& camera, const TerrainGridView& terrain, std::array<std::int32_t, 2> pixel, std::uint32_t width, std::uint32_t height, WorldCursorPick* details) {
    const auto hit = pick_world_cursor(c, camera, terrain, pixel, width, height);
    if (!hit) {
        return {};
    }
    if (details) {
        *details = *hit;
    }
    const auto x = cell(hit->position[0]), y = cell(hit->position[2]);
    if (!is_world_map_coordinate_in_bounds(x, y) || static_cast<std::uint32_t>(x) >= terrain.width_cells || static_cast<std::uint32_t>(y) >= terrain.height_cells) {
        return {};
    }
    return StaffInteractionCursor { static_cast<CellId>(x + y * 128 + 1), scaled_point(camera.eye(), cursor_direction(camera, pixel, width, height), 60.), hit->position };
}
} //namespace simcoaster
