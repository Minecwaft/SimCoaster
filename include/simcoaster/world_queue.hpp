#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
enum class HappinessChangeSize : std::int32_t {
    small = 0,
    medium = 1,
    big = 2
};

struct ParkObjectQueueTarget {
    CellId cell;
    std::uint8_t x_offset, y_offset;
};

bool has_spare_park_object_queue_capacity(AttractionWorld&, ParkObjectRuntimeView&);
std::int32_t get_park_object_queue_position_limit(AttractionWorld&, const ParkObjectRuntimeView&);
bool park_object_queue_exceeds_position_limit(AttractionWorld&, ParkObjectRuntimeView&);
void append_person_to_park_object_queue(AttractionWorld&, ParkObjectRuntimeView&, ThingId);
ParkObjectQueueTarget find_park_object_queue_destination(AttractionWorld&, ParkObjectRuntimeView&, std::uint32_t position);
bool is_person_being_loaded_by_park_object(const ParkObjectRuntimeView&, ThingId);
bool is_admission_slot_mismatched_with_queue_head(AttractionWorld&, const ParkObjectRuntimeView&);
void clear_park_object_person_admission_state(AttractionWorld&, ParkObjectRuntimeView&, ThingId);
bool admit_person_to_park_object(AttractionWorld&, ParkObjectRuntimeView&, ThingId);
CellId resolve_queue_start_item_id(WorldRuntime&, const ParkObjectRuntimeView*);
CellId find_next_queue_item_id(WorldRuntime&, CellId);
CellId get_back_of_queue(WorldRuntime&, ParkObjectRuntimeView*);
bool has_usable_entry_approach(WorldRuntime&, ParkObjectRuntimeView*);
bool is_person_in_park_object_queue_state(const PersonRuntimeView*);
std::int32_t get_person_park_object_queue_position(WorldRuntime&, const ParkObjectRuntimeView*, ThingId);
PersonRuntimeView* remove_person_from_park_object_queue(WorldRuntime&, ParkObjectRuntimeView*, ThingId);
void decrease_person_happiness_by_configured_amount(WorldRuntime&, PersonRuntimeView*, HappinessChangeSize);
void increase_person_happiness_by_configured_amount(WorldRuntime&, PersonRuntimeView*, HappinessChangeSize);
void dismiss_person_from_park_object_queue(WorldRuntime&, PersonRuntimeView*, bool suppress_happiness_penalty);
void remove_person_from_shortened_queue_if_over_capacity(WorldRuntime&, PersonRuntimeView*);
void rebuild_park_object_queue_layout(WorldRuntime&, ParkObjectRuntimeView*);
} //namespace simcoaster
