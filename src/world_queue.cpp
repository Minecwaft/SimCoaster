#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_research.hpp"
#include <bit>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
const ParkObjectRuntimeView& require_object(const ParkObjectRuntimeView* object) {
    return require_bound<std::invalid_argument>(object, "null queue object");
}

const PersonRuntimeView& require_person(const PersonRuntimeView* person) {
    return require_bound<std::invalid_argument>(person, "null queued person");
}

PersonRuntimeView* queue_person(WorldRuntime& c, ThingId id) {
    auto* record = c.entities.at(id).payload;
    if (!record)
        throw std::invalid_argument("null queue person payload");
    return reinterpret_cast<PersonRuntimeView*>(record);
}

std::uint8_t happiness_change(const WorldRuntime& c, HappinessChangeSize size) {
    const auto& balance = c.visitors.needs_balance.PeepInfo;
    return static_cast<std::uint8_t>(match (size) -> std::int32_t {
        case HappinessChangeSize::small  => balance.SmallHappinessChange;
        case HappinessChangeSize::medium => balance.MediumHappinessChange;
        case HappinessChangeSize::big    => balance.BigHappinessChange;
        case _                           => 0;
    });
}

CellBase& primary_map_cell(WorldRuntime& c, CellId id) {
    if (!c.simulation.reference)
        throw std::invalid_argument("null queue primary World");
    if (!id)
        throw std::out_of_range("zero queue cell ID");
    return c.simulation.reference->map_cells.at(static_cast<std::size_t>(id) - 1).base;
}

CellId offset_id(CellId id, int offset) {
    return static_cast<CellId>(static_cast<std::uint32_t>(id) + std::bit_cast<std::uint32_t>(offset));
}
} //namespace

CellId resolve_queue_start_item_id(WorldRuntime& c, const ParkObjectRuntimeView* object) {
    const auto entry = require_object(object).entry_cell;
    const auto mask = get_world_cell_neighbor_mask(c, &get_world_map_cell_record_by_id(c, entry)->base);
    //Native priority differs from find_next: north, south, west, east. It uses
    //the object's entry ID for flat WORD arithmetic, not the record's own ID.
    if (mask & 1)
        return offset_id(entry, -128);
    if (mask & 16)
        return offset_id(entry, 128);
    if (mask & 64)
        return offset_id(entry, -1);
    if (mask & 4)
        return offset_id(entry, 1);
    return 0;
}

CellId find_next_queue_item_id(WorldRuntime& c, CellId id) {
    auto& origin = primary_map_cell(c, id);

    struct Step {
        CellLocalOffset offset;
        std::uint8_t direction;
    };

    constexpr std::array<Step, 4> steps{ { { { 0, -1 }, 16 }, { { 0, 1 }, 1 }, { { 1, 0 }, 64 }, { { -1, 0 }, 4 } } };
    for (auto step : steps) {
        const auto* neighbor = resolve_world_cell_by_scaled_local_offset(c, &origin, step.offset, 1);
        if (neighbor && neighbor->base.common.type == 3 && get_world_cell_direction(&neighbor->base) == step.direction)
            return neighbor->base.index;
    }
    return 0;
}

CellId get_back_of_queue(WorldRuntime& c, ParkObjectRuntimeView* object) {
    require_object(object);
    if (object->back_of_queue)
        return object->back_of_queue;
    auto current = resolve_queue_start_item_id(c, object);
    object->queue_size_in_cells = 0;
    object->back_of_queue = current;
    if (!current)
        return 0;
    int remaining = 1000;
    do {
        if (remaining-- <= 0)
            break;
        object->back_of_queue = current;
        current = find_next_queue_item_id(c, current);
        ++object->queue_size_in_cells;
    } while (current);
    //Exactly1000 visited cells is also a failure, even if the last nextID was0.
    //Keep the native cache/count: a subsequent cached call returns that last ID.
    return remaining <= 0 ? 0 : object->back_of_queue;
}

bool has_usable_entry_approach(WorldRuntime& c, ParkObjectRuntimeView* object) {
    require_object(object);
    if (static_cast<std::uint16_t>(object->capabilities) & 8) {
        const auto back = get_back_of_queue(c, object);
        if (!back)
            return false;
        const auto* cell = &get_world_map_cell_record_by_id(c, back)->base;
        return get_world_cell_neighbor_mask(c, cell) != get_world_cell_direction(cell);
    }
    auto& entry = get_world_map_cell_record_by_id(c, object->entry_cell)->base;
    //Invalid rotations retain the entry ID's low BYTE in the original slot.
    auto direction = static_cast<std::uint8_t>(object->entry_cell);
    match (object->angle) {
        case 0 => direction = 16;
        case 90 => direction = 4;
        case 180 => direction = 1;
        case 270 => direction = 64;
        case _ => {}
    }
    auto* definition = find_object_definition_by_id(&c.resources.object_catalog, object->definition_id);
    if (!definition)
        throw std::invalid_argument("null queue entry definition");
    direction = advance_world_cell_direction(direction, definition->cached_entry_direction);
    const auto reciprocal = opposite_world_cell_direction(direction);
    direction = opposite_world_cell_direction(reciprocal);
    const auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &entry, direction);
    if (!neighbor)
        throw std::out_of_range("non-queue entry has no approach cell");
    return neighbor->base.common.type == 1
        && (get_world_cell_neighbor_mask(c, &neighbor->base) & reciprocal)
        && (get_world_cell_neighbor_mask(c, &entry) & direction);
}

bool is_person_in_park_object_queue_state(const PersonRuntimeView* person) {
    auto state = require_person(person).state;
    if (state == PersonState::temporary_appearance)
        state = person->saved_state;
    return std::to_underlying(state) >= 14 && std::to_underlying(state) <= 17;
}

std::int32_t
get_person_park_object_queue_position(WorldRuntime& c, const ParkObjectRuntimeView* object, ThingId requested) {
    auto current = require_object(object).first_in_queue;
    std::int32_t position = 0;
    std::size_t visited = 0;
    while (current != requested && current) {
        if (visited++ >= c.entities.size())
            throw std::length_error("cyclic Person queue");
        ++position;
        const auto* person = queue_person(c, current);
        current = is_person_in_park_object_queue_state(person) ? person->queue_next : 0;
    }
    if (current)
        return position;
    if (!requested)
        return position;
    //Native reads BASE tick only for an empty diagnostic sink on this path.
    return -1;
}

PersonRuntimeView* remove_person_from_park_object_queue(WorldRuntime& c, ParkObjectRuntimeView* object, ThingId id) {
    require_object(object);
    auto* script = world_operations::find_script(c, object->script_id);
    if (world_operations::script_slot(c, script, 0) == id) {
        script = world_operations::find_script(c, object->script_id);
        world_operations::set_script_slot(c, script, 0, 0);
    }
    const auto* person = queue_person(c, id);
    const auto previous = person->queue_previous, next = person->queue_next;
    if (previous)
        queue_person(c, previous)->queue_next = next;
    else
        object->first_in_queue = next;
    if (next)
        queue_person(c, next)->queue_previous = previous;
    queue_person(c, id)->queue_previous = 0;
    auto* result = queue_person(c, id);
    result->queue_next = 0;
    return result;
}

void
decrease_person_happiness_by_configured_amount(WorldRuntime& c, PersonRuntimeView* person, HappinessChangeSize size) {
    require_person(person);
    const auto value = static_cast<double>(person->happiness) - happiness_change(c, size);
    //Original instruction execution and unordered-comparison branches route
    //NaN to0. Express the numeric result directly without x87 flag emulation.
    person->happiness = value >= 0 ? (value <= 100 ? static_cast<float>(value) : 100.0f) : 0.0f;
}

void
increase_person_happiness_by_configured_amount(WorldRuntime& c, PersonRuntimeView* person, HappinessChangeSize size) {
    require_person(person);
    const auto value = static_cast<double>(person->happiness) + happiness_change(c, size);
    person->happiness = value >= 0 ? (value <= 100 ? static_cast<float>(value) : 100.0f) : 0.0f;
}

void dismiss_person_from_park_object_queue(WorldRuntime& c, PersonRuntimeView* person, bool suppress) {
    require_person(person);
    if (!suppress) {
        if (!(person->base.header.id & 7))
            c.ports.resources.submit_mobile_entity_positioned_template_request(c,
                                                                               person,
                                                                               c.resources.kids_catalog_slot,
                                                                               128);
        decrease_person_happiness_by_configured_amount(c, person, HappinessChangeSize::medium);
    }
    person->queue_previous = person->queue_next = 0;
    person->queue_admission_delay_active = false;
    person->queue_admission_deadline_quarter_ticks = 0;
    person->been_admitted = 0;
    person->major_destination = 0;
    person->queue_position = 0;
    world_operations::enter_world_person_state(c, person, PersonState::roaming);
}

void remove_person_from_shortened_queue_if_over_capacity(WorldRuntime& c, PersonRuntimeView* person) {
    require_person(person);
    auto* payload = c.entities.at(person->major_destination).payload;
    if (!payload)
        throw std::invalid_argument("queued person has no destination payload");
    auto* object = reinterpret_cast<ParkObjectRuntimeView*>(payload);
    const auto capacity = object->queue_size_in_cells * 4u;
    const auto position =
        std::bit_cast<std::uint32_t>(get_person_park_object_queue_position(c, object, person->base.header.id));
    if (position < capacity || person->state == PersonState::wait_for_object_script)
        return;
    //Native resolves the sprite resource into unused local position/rotation
    //outputs here. Its inspected callee is a pure pool read; the dead lookup
    //carries no observable behavior into the natural x64 implementation.
    if (!(person->base.header.id % 3))
        world_operations::set_mobile_entity_thought_bubble(c,
                                                           &person->mobile.thought_history,
                                                           MobileThoughtCode::angry,
                                                           false);
    remove_person_from_park_object_queue(c, object, person->base.header.id);
    dismiss_person_from_park_object_queue(c, person, false);
    person->major_destination = 0;
    //Dismissal already entered roaming. The native outer policy does so again.
    world_operations::enter_world_person_state(c, person, PersonState::roaming);
}

void rebuild_park_object_queue_layout(WorldRuntime& c, ParkObjectRuntimeView* object) {
    require_object(object);
    object->back_of_queue = 0;
    (void)get_back_of_queue(c, object);
    const auto serial = next_world_grid_change_serial(c);
    const auto back = get_back_of_queue(c, object);
    store_world_grid_change_serial_for_cell_id(c.simulation.reference, serial, back);
    auto current = object->first_in_queue;
    std::size_t visited = 0;
    while (current) {
        if (visited++ >= c.entities.size())
            throw std::length_error("cyclic queue rebuild list");
        const bool loading = current == object->person_being_loaded;
        auto* person = queue_person(c, current);
        current = person->queue_next;
        if (!loading)
            remove_person_from_shortened_queue_if_over_capacity(c, person);
    }
    (void)has_usable_entry_approach(c, object);
    object->assigned_staff_member = 0;
}
} //namespace simcoaster
