#include "simcoaster/world_coaster_guest_restore.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
bool in_train_car(const RideTrainState& state, ThingId person) {
    //The native +0xF4 count is the saved/active train count, not the allocated
    //maximum. Scan that prefix and only each train's configured cars. Native
    //entries are DWORDs compared through their low WORD; passenger_count does
    //not bound the two per-car arrays.
    for (const auto& train : std::span { state.trains }.first(state.active_train_count)) {
        for (const auto& car : std::span { train.cars }.first(train.car_count)) {
            for (const auto* ids : { &car.persons_a, &car.persons_b }) {
                if (std::ranges::any_of(*ids, [person](std::uint32_t id) {
                        return static_cast<ThingId>(id) == person;
                    })) {
                    return true;
                }
            }
        }
    }
    return false;
}
} //namespace

void queue_ride_peep_ids_absent_from_train_cars(AttractionWorld& c, WorldCoasterBinding& ride) {
    //These bindings are restored after geometry/train construction. A missing
    //owner or script cannot supply the native owner/slot contract; defer repair
    //instead of using the original routine's uninitialized slot temporaries.
    if (!ride.park_object) {
        return;
    }
    const auto* script = find_rsse_script_state_by_id(c, ride.script_id);
    if (!script) {
        return;
    }
    const auto primary = get_rsse_script_state_slot(c, script, 0);
    const auto secondary = get_rsse_script_state_slot(c, script, 1);
    const auto owner = ride.park_object->base.header.id;
    auto& state = ride.train_state;
    std::size_t visited = 0;
    for (auto* row = c.entities.active_head; row; row = row->active_next) {
        if (visited++ >= thing_slot_count) {
            throw std::length_error("cyclic coaster guest active list");
        }
        if (!row->payload || row->payload->kind != ThingKind::person) {
            continue;
        }
        const auto& person = *reinterpret_cast<const PersonRuntimeView*>(row->payload);
        //This is the raw state, not temporary_appearance's saved_state. Native
        //visitor enumeration includes retirement-pending active Person records.
        if (person.major_destination != owner || person.state != PersonState::object_interaction) {
            continue;
        }
        const auto id = person.base.header.id;
        //The slot operands remain signed DWORDs: truncating them to ThingId would
        //incorrectly exclude a visitor whose ID matches only their low WORD.
        if (static_cast<std::int32_t>(id) == primary || static_cast<std::int32_t>(id) == secondary || in_train_car(state, id)) {
            continue;
        }
        //Native 0x478090 assumes fresh queues. Keep the host repair idempotent if
        //called again after a partial restore; this does not change native saves.
        if (std::ranges::contains(state.queue_a.ids, id) || std::ranges::contains(state.queue_b.ids, id) || std::ranges::contains(state.exit_overflow, id)) {
            continue;
        }
        //Native 0x47C4A0 uses physical occupancy (queued + auxiliary), not the
        //logical admission limit. A full A falls through to B; two full queues
        //leave the visitor untouched for a later retry.
        if (!state.queue_a.push(id)) {
            enqueue_completed_ride_exit(state, id);
        }
    }
}

void reconstruct_coaster_guest_queues(AttractionWorld& c) {
    if (c.resources.meshes) {
        if (auto* coasters = c.resources.meshes->coasters()) {
            for (auto& ride : coasters->bindings) {
                queue_ride_peep_ids_absent_from_train_cars(c, ride);
                //A closed ride can be saved after entering lifecycle8, while its
                //completed-exit queue is still nonempty. The wire image drops that
                //queue, so 0x478090 recovers its visitors into A. Mode8 never boards
                //A; reapply native 0x475570's same8 transition, which drains A to B
                //and leaves cars, playback and clocks unchanged. This continuation is
                //another explicit host restore repair, not a verified loader call.
                if ((ride.train_state.detail_state_flags & 15u) == std::to_underlying(RideDetailState::passenger_mode_8)) {
                    transition_coaster_detail_state(c, *c.resources.meshes, ride, RideDetailState::passenger_mode_8);
                }
            }
        }
    }
}
} //namespace simcoaster
