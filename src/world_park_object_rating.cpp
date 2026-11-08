#include "simcoaster/world_objects.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/track_ride_restore.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
const ObjectDefinitionBalance& get_park_object_balance(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    const auto* definition = get_park_object_definition(c, &object);
    if (!definition || !definition->balance) {
        throw std::logic_error("complete object balance is unavailable");
    }
    return *definition->balance;
}

TrackRideLayoutMetrics EntityEnginePort::collect_track_ride_layout_metrics(WorldRuntime&, std::uint32_t) {
    throw std::logic_error("track layout metrics backend is required");
}

RideRuntimeRatings world_operations::builtin::get_mesh_runtime_ride_ratings(WorldRuntime& c, std::uint32_t map_id) {
    if (!c.resources.meshes || !c.resources.meshes->coasters()) {
        throw std::logic_error("coaster ratings owner is not bound");
    }
    const auto* binding = c.resources.meshes->coasters()->find_map(map_id);
    if (!binding) {
        throw std::out_of_range("missing coaster ratings map ID");
    }
    return { binding->metrics.sickness, binding->metrics.excitement };
}

TrackRideLayoutMetrics query_track_ride_layout_metrics(const TrackRideManager& manager, std::uint32_t handle) {
    const auto slot = handle & 255u;
    if (slot >= manager.rides.size() || !manager.rides[slot] || manager.rides[slot]->handle != handle) {
        return {};
    }
    const auto& parts = manager.rides[slot]->auxiliary;
    TrackRideLayoutMetrics result {};
    std::int32_t run = 0;
    //The two passes are intentional: a run may cross the end/start seam.
    //Native publishes the longest run only on a break, never after the loop.
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (const auto& part : parts) {
            const auto type = part.flags & 0xffffu;
            if (type >= 9 && type <= 13) {
                ++run;
                if (type == 11) {
                    ++result.type_b_part_count;
                }
                if ((type == 12 || type == 13) && !part.track_cell_ordinal) {
                    ++result.unconfigured_cd_part_count;
                }
            } else {
                result.longest_supported_run = std::max(result.longest_supported_run, run);
                run = 0;
                ++result.segment_break_count;
            }
            ++result.visited_entry_count;
        }
    }
    result.segment_break_count /= 2;
    result.type_b_part_count /= 2;
    result.visited_entry_count /= 2;
    return result;
}

std::uint8_t calculate_park_object_excitement_rating(AttractionWorld& c, ParkObjectRuntimeView& object, std::int32_t first, std::int32_t second, std::int32_t third) {
    if (get_park_object_definition(c, &object)->which_ui_type == ObjectUiCategory::sideshows) {
        const auto difference = std::clamp(fixed::subtract(first, second), 0, 100);
        return static_cast<std::uint8_t>(20u - static_cast<std::uint32_t>(fixed::truncate_integer(std::sqrt(static_cast<double>(difference)) * third * 0.1 * static_cast<double>(-0.8f))));
    }
    if (get_park_object_balance(c, object).Bumper.WhichTrackType == 3) {
        const auto id = get_rsse_coaster_runtime_map_id(c, object.script_id);
        if (!id) {
            return 0;
        }
        return static_cast<std::uint8_t>(fixed::truncate_integer(static_cast<double>(world_operations::get_mesh_runtime_ride_ratings(c, id).excitement) * 0.5 + 50.0));
    }
    const auto base = get_park_object_balance(c, object).UsageInfo.ExcitementLevel;
    const auto initial_speed = get_park_object_balance(c, object).Upgrades.at(object.current_upgrade).InitSpeed;
    const auto initial_duration = get_park_object_balance(c, object).Upgrades.at(object.current_upgrade).InitDuration;
    auto excitement = base;
    if (object.track_ride_handle) {
        const auto metrics = c.ports.entities.collect_track_ride_layout_metrics(c, object.track_ride_handle);
        const auto bonus = std::clamp(fixed::add(fixed::add(fixed::add(metrics.type_b_part_count, metrics.longest_supported_run), fixed::add(metrics.type_b_part_count, metrics.type_b_part_count)), fixed::add(metrics.segment_break_count, metrics.segment_break_count)), 0, 40);
        excitement = std::clamp(fixed::add(bonus, fixed::narrow(std::int64_t { fixed::word(static_cast<std::uint32_t>(base) * 60u) } / 100)), 0, 100);
    }
    //Native makes redundant catalog reads here after the metrics callback.
    (void)get_park_object_definition(c, &object);
    auto speed_ratio = static_cast<double>(first) / initial_speed;
    const auto duration_ratio = static_cast<float>(static_cast<double>(second) / initial_duration);
    speed_ratio = speed_ratio >= 0.75 ? (speed_ratio > 1.25 ? 1.25 : speed_ratio) : 0.75;
    const auto duration = duration_ratio >= 0.75 ? (duration_ratio <= 1.25 ? static_cast<double>(duration_ratio) : 1.25) : 0.75;
    return static_cast<std::uint8_t>(fixed::truncate_integer(duration * speed_ratio * excitement));
}

std::uint8_t get_park_object_excitement(AttractionWorld& c, ParkObjectRuntimeView& object, bool use_base) {
    const auto base = get_park_object_balance(c, object).UsageInfo.ExcitementLevel;
    if (use_base || !base) {
        return static_cast<std::uint8_t>(base);
    }
    if (get_park_object_definition(c, &object)->which_ui_type == ObjectUiCategory::sideshows) {
        return calculate_park_object_excitement_rating(c, object, std::bit_cast<std::int32_t>(object.cost_of_goods), std::bit_cast<std::int32_t>(object.price_per_use), static_cast<std::uint8_t>(object.chance_of_winning));
    }
    return calculate_park_object_excitement_rating(c, object, std::bit_cast<std::int32_t>(object.operating_speed), object.operating_duration, object.operating_capacity);
}
} //namespace simcoaster
