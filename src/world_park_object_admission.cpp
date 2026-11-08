#include "world_person_internal.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/object_balance.hpp"
#include <algorithm>

namespace simcoaster {
bool has_spare_park_object_queue_capacity(AttractionWorld& c, ParkObjectRuntimeView& object) {
    return static_cast<std::uint32_t>(get_person_park_object_queue_position(c, &object, 0))
         < 4u * object.queue_size_in_cells;
}

std::int32_t get_park_object_queue_position_limit(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    if (!(std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::has_queue)))
        return 100;
    if (object.operating_speed)
        c.attractions.park_object_queue_operating_speed_scale =
            static_cast<float>(static_cast<double>(object.operating_speed)
                               / get_park_object_balance(c, object).Upgrades.at(object.current_upgrade).InitSpeed);
    else
        c.attractions.park_object_queue_operating_speed_scale = 1.f;
    const auto limit = static_cast<double>(
                           get_park_object_balance(c, object).Upgrades.at(object.current_upgrade).QueueWaitTimeConstant)
                     * object.operating_capacity
                     * c.attractions.park_object_queue_operating_speed_scale
                     / object.operating_duration;
    return fixed::truncate_integer(limit <= 4.0 ? 4.0 : limit);
}

bool park_object_queue_exceeds_position_limit(AttractionWorld& c, ParkObjectRuntimeView& object) {
    return static_cast<std::uint32_t>(get_person_park_object_queue_position(c, &object, 0))
        >= static_cast<std::uint32_t>(get_park_object_queue_position_limit(c, object));
}

void append_person_to_park_object_queue(AttractionWorld& c, ParkObjectRuntimeView& object, ThingId person_id) {
    auto id = object.first_in_queue;
    ThingId previous = 0;
    std::size_t visited = 0;
    while (id) {
        if (visited++ >= thing_slot_count)
            throw std::length_error("cyclic queue append");
        previous = id;
        auto& person = *reinterpret_cast<PersonRuntimeView*>(&person_detail::map_object(c, id));
        if (!person.queue_next) {
            person.queue_next = person_id;
            break;
        }
        id = person.queue_next;
    }
    if (!object.first_in_queue)
        object.first_in_queue = person_id;
    auto& person = *reinterpret_cast<PersonRuntimeView*>(&person_detail::map_object(c, person_id));
    person.queue_previous = previous;
    person.queue_next = 0;
}

namespace {
void queue_offsets(ParkObjectQueueTarget& target,
                   std::uint8_t direction,
                   std::uint8_t along,
                   std::uint8_t across,
                   bool second_half) {
    match (direction) {
        case 1 => {
            target.x_offset = across;
            target.y_offset = along;
        }
        case 4 => {
            target.x_offset = static_cast<std::uint8_t>(second_half ? along + 128u : 255u - along);
            target.y_offset = across;
        }
        case 16 => {
            target.x_offset = across;
            target.y_offset = static_cast<std::uint8_t>(255u - along);
        }
        case 64 => {
            target.x_offset = along;
            target.y_offset = across;
        }
        case _ => throw std::domain_error("queue destination has no cardinal direction");
    }
}
} //namespace

ParkObjectQueueTarget
find_park_object_queue_destination(AttractionWorld& c, ParkObjectRuntimeView& object, std::uint32_t position) {
    ParkObjectQueueTarget result{};
    auto remaining = position;
    const bool has_queue =
        (std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::has_queue)) != 0;
    if (has_queue) {
        result.cell = resolve_queue_start_item_id(c, &object);
        while (remaining >= 4 && result.cell) {
            remaining -= 4;
            result.cell = find_next_queue_item_id(c, result.cell);
        }
    }
    const auto along =
        static_cast<std::uint8_t>(fixed::truncate_integer(static_cast<double>(remaining) * 0.25 * 255.0));
    const auto across = static_cast<std::uint8_t>(
        static_cast<std::uint32_t>(next_world_prng_value(person_detail::base_world(c))) % 0x1Cu + 114u);
    if (!has_queue) {
        result.cell = get_back_of_queue(c, &object);
        auto* entry = get_world_map_cell_record_by_id(c, object.entry_cell);
        (void)get_back_of_queue(c, &object);
        queue_offsets(result, get_world_cell_direction(&entry->base), along, across, false);
        return result;
    }
    if (!result.cell)
        throw std::out_of_range("queue position exceeds its path cells");
    auto* cell = get_world_map_cell_record_by_id(c, result.cell);
    auto direction = get_world_cell_direction(&cell->base);
    const bool second_half = along > 128;
    if (second_half) {
        const auto next = find_next_queue_item_id(c, result.cell);
        if (next)
            direction = get_world_cell_direction(&get_world_map_cell_record_by_id(c, next)->base);
        else {
            constexpr std::array<std::uint8_t, 4> directions{ 1, 4, 16, 64 };
            const auto mask = get_world_cell_neighbor_mask(c, &cell->base);
            for (std::size_t i = 0; i < directions.size(); ++i)
                if (mask & directions[i]) {
                    if (!(get_world_cell_neighbor_mask(c, &cell->base) & directions[i]))
                        continue;
                    const auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &cell->base, directions[i]);
                    if (neighbor && neighbor->base.common.type == 1) {
                        direction = directions[(i + 2u) % 4u];
                        break;
                    }
                }
        }
    }
    queue_offsets(result, direction, along, across, second_half);
    return result;
}

bool is_person_being_loaded_by_park_object(const ParkObjectRuntimeView& object, ThingId person) {
    return object.person_being_loaded == person;
}

bool is_admission_slot_mismatched_with_queue_head(AttractionWorld& c, const ParkObjectRuntimeView& object) {
    auto* state = world_operations::find_script(c, object.script_id);
    return world_operations::script_slot(c, state, 0) != object.first_in_queue;
}

void clear_park_object_person_admission_state(AttractionWorld& c, ParkObjectRuntimeView& object, ThingId person) {
    object.person_being_loaded = 0;
    auto* state = world_operations::find_script(c, object.script_id);
    if (world_operations::script_slot(c, state, 0) == person) {
        state = world_operations::find_script(c, object.script_id);
        world_operations::set_script_slot(c, state, 0, 0);
    }
}

bool admit_person_to_park_object(AttractionWorld& c, ParkObjectRuntimeView& object, ThingId person) {
    if (object.state == ParkObjectState::broken_down || !object.can_load || object.state == ParkObjectState::condemned)
        return false;
    object.person_being_loaded = 0;
    auto* state = world_operations::find_script(c, object.script_id);
    if (world_operations::script_slot(c, state, 0))
        return false;
    world_operations::set_script_slot(c, state, 0, person);
    return true;
}

std::int32_t compute_person_ride_excitement_difference(AttractionWorld& c,
                                                       const PersonRuntimeView& person,
                                                       ParkObjectRuntimeView& object) {
    if (park_object_requires_staff_service(c, object))
        return 100;
    return std::clamp(int(get_park_object_excitement(c, object, false))
                          - int(static_cast<std::uint8_t>(person.preferred_excitement)),
                      -50,
                      50);
}
} //namespace simcoaster
