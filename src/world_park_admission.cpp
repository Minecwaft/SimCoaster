#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_objects.hpp"
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
ParkObjectRuntimeView& park_object(WorldRuntime& c, ThingId id) {
    auto* payload = c.entities.at(id).payload;
    if (!payload || payload->kind != ThingKind::park_object)
        throw std::logic_error("park lifecycle object is missing");
    return *reinterpret_cast<ParkObjectRuntimeView*>(payload);
}

void gate_command(WorldRuntime& c, World& world, ParkGateCommand command) {
    auto& gates = park_object(c, world.saved.park_gates);
    auto* script = world_operations::find_script(c, gates.script_id);
    (void)world_operations::script_slot(c, script, 1);
    //Preserve the native second lookup after the observable status query.
    world_operations::set_script_slot(c,
                                      world_operations::find_script(c, gates.script_id),
                                      0,
                                      std::to_underlying(command));
}

void update_objects(WorldRuntime& c, World& world, bool open) {
    std::size_t visited = 0;
    for (auto id = world.saved.first_park_object; id;) {
        if (visited++ >= thing_slot_count)
            throw std::length_error("cyclic park lifecycle object list");
        auto& object = park_object(c, id);
        if ((std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::is_choosable)) != 0) {
            if (open) {
                if (is_park_object_open_eligible(c, object))
                    open_park_object(c, object);
            } else
                close_park_object(c, object);
        }
        id = object.next_park_object;
    }
}
} //namespace

void handle_world_park_open_close(WorldRuntime& c, World& world, bool should_open, bool transition_mode) {
    if (transition_mode) {
        gate_command(c, world, ParkGateCommand::transition);
        return;
    }
    if (should_open) {
        if (world.saved.park_closed) {
            world.saved.park_closed = 0;
            gate_command(c, world, ParkGateCommand::open);
            update_objects(c, world, true);
        }
    } else if (!world.saved.park_closed) {
        world.saved.park_closed = 1;
        if (!count_eligible_person_records(c, 4) && world.saved.park_gates) {
            auto& gates = park_object(c, world.saved.park_gates);
            if (world_operations::script_slot(c, world_operations::find_script(c, gates.script_id), 1) == 1)
                gate_command(c, world, ParkGateCommand::close);
        }
        update_objects(c, world, false);
    }
    WorldEvent event{ Tag19Event{ should_open ? 3u : 4u } };
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &event,
                                        deliver_world_record_event,
                                        nullptr);
}
} //namespace simcoaster
