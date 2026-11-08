#include "world_person_internal.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/object_balance.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster {
namespace {
bool has(const ParkObjectRuntimeView& object, ParkObjectCapabilities flag) {
    return (std::to_underlying(object.capabilities) & std::to_underlying(flag)) != 0;
}

template <class Visit>
void each_object(VisitorWorld& c, Visit&& visit) {
    auto id = person_detail::base_world(c).saved.first_park_object;
    std::size_t count = 0;
    while (id) {
        if (count++ >= thing_slot_count) {
            throw std::length_error("cyclic nearby-object list");
        }
        auto& object = person_detail::park_object(c, id);
        visit(id, object);
        id = object.next_park_object;
    }
}

bool slot_zero(VisitorWorld& c, const ParkObjectRuntimeView& object, int slot) {
    auto* state = world_operations::find_script(c, object.script_id);
    return world_operations::script_slot(c, state, slot) == 0;
}
} //namespace

ThingId find_nearest_eligible_litter_bin(VisitorWorld& c, PersonRuntimeView& person, std::int32_t radius) {
    ThingId best = 0;
    auto distance = std::numeric_limits<std::uint32_t>::max();
    each_object(c, [&](ThingId id, ParkObjectRuntimeView& object) {
        if (!has(object, ParkObjectCapabilities::holds_litter)) {
            return;
        }
        const auto d = person_detail::squared_cell_distance(person_detail::cell_id(person.base), person_detail::cell_id(object.base));
        if (d >= static_cast<std::uint32_t>(radius) * static_cast<std::uint32_t>(radius) || d >= distance) {
            return;
        }
        const auto [x, y] = compute_park_object_service_target_coordinates(c, object);
        if (slot_zero(c, object, 10) && slot_zero(c, object, 5) && request_mobile_entity_path_to_destination(c, person.base, person.mobile, x, y)) {
            distance = d;
            best = id;
        }
    });
    return best;
}

ThingId find_nearest_vegetation_for_fire_prank(VisitorWorld& c, PersonRuntimeView& person, std::int32_t radius) {
    ThingId best = 0;
    auto distance = std::numeric_limits<std::uint32_t>::max();
    const auto maximum = static_cast<std::uint32_t>(radius) * static_cast<std::uint32_t>(radius);
    each_object(c, [&](ThingId id, ParkObjectRuntimeView& object) {
        if (!has(object, ParkObjectCapabilities::requires_watering) || !slot_zero(c, object, 13) || !slot_zero(c, object, 12)) {
            return;
        }
        const auto origin = person_detail::cell_id(person.base);
        const auto d = person_detail::squared_cell_distance(origin, person_detail::cell_id(object.base));
        if (d >= maximum || d >= distance) {
            return;
        }
        const auto path = find_nearest_path_cell_around_object(c, object, origin);
        if (path && try_set_mobile_entity_path_to_cell(c, person.base, person.mobile, path)) {
            distance = d;
            best = id;
        }
    });
    return best;
}

ThingId find_nearby_unstarted_fireworks(VisitorWorld& c, const PersonRuntimeView& person) {
    ThingId best = 0;
    auto distance = std::numeric_limits<std::uint32_t>::max();
    each_object(c, [&](ThingId id, ParkObjectRuntimeView& object) {
        if (!has(object, ParkObjectCapabilities::is_fireworks) || !slot_zero(c, object, 0)) {
            return;
        }
        const auto d = person_detail::squared_cell_distance(person_detail::cell_id(person.base), person_detail::cell_id(object.base));
        if (d <= 4u && d < distance) {
            distance = d;
            best = id;
        }
    });
    return best;
}

ThingId find_nearest_entertainer_to_entity(VisitorWorld& c, const MapObjectBase& origin) {
    const auto origin_cell = person_detail::cell_id(origin);
    ThingId best = 0;
    auto distance = std::numeric_limits<std::uint32_t>::max();
    auto id = person_detail::base_world(c).saved.first_entertainer;
    std::size_t count = 0;
    while (id) {
        if (count++ >= thing_slot_count) {
            throw std::length_error("cyclic entertainer list");
        }
        auto& record = *reinterpret_cast<StaffRuntimeRecord*>(&person_detail::map_object(c, id));
        const auto d = person_detail::squared_cell_distance(origin_cell, person_detail::cell_id(record.base));
        if (d < distance) {
            distance = d;
            best = id;
        }
        id = record.next_staff;
    }
    return best;
}

ThingId find_nearest_active_person_record(VisitorWorld& c, const MapObjectBase& origin) {
    auto id = person_detail::base_world(c).saved.first_park_object;
    ThingId best = 0;
    auto distance = std::numeric_limits<std::uint32_t>::max();
    std::size_t count = 0;
    while (id) {
        if (count++ >= thing_slot_count) {
            throw std::length_error("cyclic nearby-person suffix");
        }
        const auto& record = person_detail::map_object(c, id);
        if (record.header.kind == ThingKind::person) {
            const auto d = person_detail::squared_cell_distance(person_detail::cell_id(origin), person_detail::cell_id(record));
            if (d < distance) {
                distance = d;
                best = id;
            }
        }
        const auto* next = c.entities.at(record.header.id).active_next;
        id = next ? static_cast<ThingId>(next->id_storage) : ThingId {};
    }
    return best;
}

ThingId select_best_available_info_desk_for_person(VisitorWorld& c, const PersonRuntimeView& person) {
    ThingId best = 0;
    auto score = std::numeric_limits<std::uint32_t>::max();
    each_object(c, [&](ThingId id, ParkObjectRuntimeView& object) {
        if (!get_park_object_balance(c, object).UsageInfo.IsInfoDesk) {
            return;
        }
        const auto distance = person_detail::squared_cell_distance(person_detail::cell_id(person.base), person_detail::cell_id(object.base));
        const auto candidate = distance / 5u + static_cast<std::uint32_t>(get_person_park_object_queue_position(c, &object, 0));
        if (candidate < score && object.can_load) {
            score = candidate;
            best = id;
        }
    });
    return best;
}

std::uint32_t query_world_used_thing_type_in_square_radius(VisitorWorld& c, std::int32_t kind, CellId center, std::int32_t radius, bool first_only) {
    const auto value = static_cast<std::uint32_t>(center) - 1u;
    const auto x = static_cast<std::int32_t>(value & 127u), y = static_cast<std::int32_t>(value >> 7);
    const auto begin_x = std::max<std::int64_t>(std::int64_t { x } - radius, 0), begin_y = std::max<std::int64_t>(std::int64_t { y } - radius, 0);
    const auto end_x = std::min<std::int64_t>(std::int64_t { x } + radius, 127), end_y = std::min<std::int64_t>(std::int64_t { y } + radius, 127);
    if (!c.simulation.map_cell_records) {
        throw std::logic_error("nearby query has no map cells");
    }
    std::uint32_t count = 0;
    for (auto cx = begin_x; cx <= end_x; ++cx) {
        for (auto cy = begin_y; cy <= end_y; ++cy) {
            auto id = static_cast<ThingId>(c.simulation.map_cell_records[static_cast<std::size_t>(cy * 128 + cx)].who);
            std::size_t visited = 0;
            while (id) {
                if (visited++ >= thing_slot_count) {
                    throw std::length_error("cyclic nearby map list");
                }
                const auto& record = person_detail::map_object(c, id);
                if (std::to_underlying(record.header.kind) == kind) {
                    if (first_only) {
                        return 1;
                    }
                    ++count;
                }
                id = record.map_child;
            }
        }
    }
    return count;
}
} //namespace simcoaster
