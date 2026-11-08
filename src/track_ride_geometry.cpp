#include "simcoaster/track_ride_restore.hpp"
#include "simcoaster/ride_description_balance.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace simcoaster {
namespace {
constexpr std::size_t max_regions = 2048, max_nodes = 1024;

//Pool indices remain meaningful in saved selected-node references. Reuse the
//first inactive slot just as the original pool scan does.
std::optional<std::size_t> region(TrackRideManager& manager,
                                  std::uint32_t flags,
                                  std::int32_t x,
                                  std::int32_t y,
                                  std::int32_t ex,
                                  std::int32_t ey) {
    auto it = std::find_if(manager.geometry.begin(), manager.geometry.end(), [](const auto& value) {
        return !(value.flags & 1u);
    });
    if (it == manager.geometry.end()) {
        if (manager.geometry.size() >= max_regions)
            return std::nullopt;
        manager.geometry.emplace_back();
        it = manager.geometry.end() - 1;
    }
    const auto index = static_cast<std::size_t>(it - manager.geometry.begin());
    *it = TrackRideGeometryRegion{ flags, x, y, ex, ey, {} };
    return index;
}

std::optional<std::int32_t> node(TrackRideManager& manager,
                                 TrackRide& ride,
                                 std::uint32_t flags,
                                 std::int32_t x,
                                 std::int32_t y,
                                 std::optional<std::size_t> geometry = {}) {
    std::array<bool, max_nodes> occupied{};
    const auto mark = [&](const TrackRide& item) {
        for (const auto& n : item.nodes)
            if ((n.flags & 1u) && n.pool_slot >= 0 && static_cast<std::size_t>(n.pool_slot) < occupied.size())
                occupied[static_cast<std::size_t>(n.pool_slot)] = true;
    };
    bool found = false;
    for (const auto& item : manager.rides)
        if (item) {
            mark(*item);
            if (item.get() == &ride)
                found = true;
        }
    if (!found)
        mark(ride);
    const auto available = std::find(occupied.begin(), occupied.end(), false);
    if (available == occupied.end())
        return std::nullopt;
    const auto slot = static_cast<std::int32_t>(available - occupied.begin());
    ride.nodes.push_back({ flags, slot, x, y, geometry });
    if (geometry)
        manager.geometry[*geometry].linked_node_slot = slot;
    return slot;
}

std::pair<std::int32_t, std::int32_t> station_offset(std::uint32_t orientation) {
    std::int32_t x = (orientation & 1u) ? 0 : -3072, y = (orientation & 1u) ? -3072 : 0;
    if (orientation & 2u)
        x = -x;
    if (orientation & 4u)
        y = -y;
    return { x, y };
}

std::int32_t sample(unsigned index) {
    return static_cast<std::int32_t>(std::sin(static_cast<double>(index * 2) * 0.00613591796875) * 256.0);
}

bool radial_nodes(TrackRideManager& manager,
                  TrackRide& ride,
                  std::int32_t center_x,
                  std::int32_t center_y,
                  std::int32_t x,
                  std::int32_t y,
                  unsigned count,
                  std::int32_t sine,
                  std::int32_t cosine) {
    for (unsigned i = 0; i < count; ++i) {
        if (!node(manager, ride, 1, center_x + x, center_y + y))
            return false;
        const auto old_x = x;
        x = (y * sine + x * cosine) / 256;
        y = (y * cosine - old_x * sine) / 256;
    }
    return true;
}
} //namespace

bool build_standard_track_ride_geometry(TrackRideManager& manager, TrackRide& ride) {
    const auto tag = ride.template_tag;
    if (tag == -11 || tag > -1 || tag < -15)
        return false;
    if (tag == -2) {
        for (const auto item : std::array{ std::array{ 31, 46080, 38400, 7680 },
                                           std::array{ 95, 46080, 38400, 5120 },
                                           std::array{ 31, 36864, 29184, 9728 } }) {
            const auto index = region(manager, static_cast<std::uint32_t>(item[0]), item[1], item[2], item[3], item[3]);
            if (!index)
                return false;
            ride.unlinked_geometry.push_back(*index);
        }
        return radial_nodes(manager, ride, 46080, 38400, 0, -6144, 7, sample(448), sample(64))
            && radial_nodes(manager, ride, 36864, 29184, 0, 8192, 7, sample(448), sample(64));
    }
    if (tag == -1 || tag == -3 || tag == -6 || tag == -14 || tag == -15) {
        const auto cx = ride.origin_x + (tag == -15 ? 3072 : 1536), cy = ride.origin_y + (tag == -15 ? 2048 : 1536);
        const auto radius = tag == -1 ? 4608 : tag == -15 ? 4096 : 5632;
        const auto r = region(manager, 31, cx, cy, radius, radius);
        if (!r)
            return false;
        ride.geometry.insert(ride.geometry.begin(), *r);
        ride.simulation_spawn_x = cx;
        ride.simulation_spawn_y = cy;
        ride.simulation_target_x = cx;
        ride.simulation_target_y = cy;
        return radial_nodes(manager, ride, cx, cy, 0, tag == -1 ? 3072 : 4096, 8, sample(64), sample(192));
    }
    ride.simulation_spawn_x = ride.origin_x;
    ride.simulation_spawn_y = ride.origin_y;
    ride.simulation_target_x = ride.origin_x;
    ride.simulation_target_y = ride.origin_y;
    const bool grid = tag == -4 || tag == -7 || tag == -9 || tag == -10;
    if (grid)
        for (std::uint32_t i = 0; i < ride.simulation_record_limit; ++i) {
            auto x =
                (static_cast<std::int32_t>(ride.simulation_record_limit) - static_cast<std::int32_t>(i) - 2) * 1024;
            auto y = 1024 - static_cast<std::int32_t>(i & 1u) * 2048;
            if (ride.orientation & 1u)
                std::swap(x, y);
            if (ride.orientation & 2u)
                x = -x;
            if (ride.orientation & 4u)
                y = -y;
            if (!node(manager, ride, 3, ride.origin_x + x, ride.origin_y + y))
                return false;
        }
    const auto [x, y] = station_offset(ride.orientation);
    if (!node(manager, ride, 5, ride.origin_x + x, ride.origin_y + y))
        return false;
    if (!grid) {
        ride.simulation_target_x += x / 3;
        ride.simulation_target_y += y / 3;
    }
    return true;
}

bool build_described_track_ride_geometry(TrackRideManager& manager,
                                         TrackRide& ride,
                                         const RideDescriptionBalance& description) {
    if (ride.template_tag != -11)
        return false;
    const auto collision_end =
        std::find_if(description.ColObjects.begin(), description.ColObjects.end(), [](const auto& row) {
            return row.X == 512;
        });
    const auto buoy_end = std::find_if(description.Buoys.begin(), description.Buoys.end(), [](const auto& row) {
        return row.X == 512;
    });
    //The original scans to a sentinel without an array bound. Reject missing
    //sentinels rather than wandering into unrelated runtime memory.
    if (collision_end == description.ColObjects.end() || buoy_end == description.Buoys.end())
        return false;
    const auto scaled = [](std::int32_t value, double multiplier = 1.0) -> std::optional<std::int32_t> {
        const auto result = static_cast<double>(value) * static_cast<double>(307.2f) * multiplier;
        if (result < static_cast<double>(std::numeric_limits<std::int32_t>::min())
            || result > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
            return std::nullopt;
        return static_cast<std::int32_t>(result);
    };
    if (description.Info.PositionOverideX != -1) {
        const auto v = scaled(description.Info.PositionOverideX, 10.0);
        if (!v)
            return false;
        ride.origin_x = *v;
    }
    if (description.Info.PositionOverideY != -1) {
        const auto v = scaled(description.Info.PositionOverideY, 10.0);
        if (!v)
            return false;
        ride.origin_y = *v;
    }
    for (auto it = description.ColObjects.begin(); it != collision_end; ++it) {
        const auto x = scaled(it->X), y = scaled(it->Y), radius = scaled(it->Rad);
        if (!x || !y || !radius)
            return false;
        const auto index = region(manager, 31, *x, *y, *radius, *radius);
        if (!index)
            return false;
        ride.geometry.insert(ride.geometry.begin(), *index);
    }
    const auto start_x = scaled(description.Info.StartX), start_y = scaled(description.Info.StartY);
    if (!start_x || !start_y)
        return false;
    ride.simulation_spawn_x = *start_x;
    ride.simulation_spawn_y = *start_y;
    ride.simulation_target_x = *start_x;
    ride.simulation_target_y = *start_y;
    for (auto it = description.Buoys.begin(); it != buoy_end; ++it) {
        const auto x = scaled(it->X), y = scaled(it->Y);
        if (!x || !y || !node(manager, ride, 1, *x, *y))
            return false;
    }
    return true;
}

bool add_track_ride_geometry_piece(TrackRideManager& manager, TrackRide& ride, TrackRideAuxiliary& part) {
    //Low signed WORD selects the track piece; the high flag suppresses its node.
    const auto type = static_cast<std::int16_t>(part.flags & 0xffffu);
    const auto x = part.x, y = part.y, outer = ride.region_extent - ride.collision_radius + 3072,
               inner = ride.collision_radius - ride.region_extent + 3072;
    std::int32_t node_x{}, node_y{};
    std::optional<std::size_t> linked;
    const auto add = [&](std::uint32_t flags, std::int32_t gx, std::int32_t gy, std::int32_t ex, std::int32_t ey) {
        const auto index = region(manager, flags, gx, gy, ex, ey);
        if (index)
            part.geometry.insert(part.geometry.begin(), *index);
        return index;
    };
    if (type >= 1 && type <= 8) {
        const auto corner = (type - 1) % 4;
        const auto gx = x + ((corner == 0 || corner == 3) ? 6144 : 0), gy = y + (corner >= 2 ? 6144 : 0);
        constexpr std::array<std::uint32_t, 4> masks{ 0xffffffebu, 0xffffffedu, 0xfffffff5u, 0xfffffff3u };
        linked = add(31u & masks[static_cast<std::size_t>(corner)], gx, gy, outer, outer);
        if (!linked || !add((31u & masks[static_cast<std::size_t>(corner)]) | 64u, gx, gy, inner, inner))
            return false;
        node_x = x + ((corner == 0 || corner == 3) ? 3972 : 2172);
        node_y = y + ((corner < 2) ? 2172 : 3972);
    } else if (type == 9 || type == 10 || type == 12 || type == 13) {
        const auto extent = ride.region_extent - ride.collision_radius;
        const bool horizontal = type == 9 || type == 12;
        const auto flags = 31u | 32u | ((type == 12 || type == 13) ? 256u : 0u);
        linked = add(flags, x + 3072, y + 3072, horizontal ? 3104 : extent, horizontal ? extent : 3104);
        if (!linked)
            return false;
        node_x = x + 3072;
        node_y = y + 3072;
    } else if (type == 11) {
        const auto extent = ride.region_extent - ride.collision_radius;
        const auto diagonal =
            static_cast<std::int32_t>(std::sqrt(static_cast<double>(extent) * extent + 3072.0 * 3072.0));
        const bool special =
            ride.template_tag == -5 || ride.template_tag == -8 || ride.template_tag == -12 || ride.template_tag == -13;
        linked = add(31u | (special ? 128u : 0u), x + 3072, y + 3072, diagonal, diagonal);
        if (!linked)
            return false;
        constexpr std::array<std::uint32_t, 4> masks{ 0xffffffabu, 0xffffffadu, 0xffffffb5u, 0xffffffb3u };
        constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> corners{
            { { 6144, 0 }, { 0, 0 }, { 0, 6144 }, { 6144, 6144 } }
        };
        for (std::size_t i = 0; i < corners.size(); ++i)
            if (!add((31u & masks[i]) | 64u, x + corners[i].first, y + corners[i].second, inner, inner))
                return false;
        node_x = x + 3072;
        node_y = y + 3072;
    } else {
        //Invalid types have no region. The executable would dereference null if
        //they also request a node; reject that malformed input.
        return (part.flags & 0x10000u) != 0;
    }
    if (!(part.flags & 0x10000u)) {
        const auto slot = node(manager, ride, 1, node_x, node_y, linked);
        if (!slot)
            return false;
        part.linked_node_slot = *slot;
    }
    return true;
}
} //namespace simcoaster
