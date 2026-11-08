#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_staff.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
ParkObjectRuntimeView& object_by_id(AttractionWorld& c, ThingId id) {
    auto* payload = c.entities.at(id).payload;
    if (!payload)
        throw std::invalid_argument("null park-object roster payload");
    return *reinterpret_cast<ParkObjectRuntimeView*>(payload); //Native performs no type check.
}

CellId cell(const ParkObjectRuntimeView& object) {
    return static_cast<CellId>(((object.base.y >> 8) << 7) + (object.base.x >> 8) + 1);
}
} //namespace

void unlink_park_object_from_world_list(AttractionWorld& c, World& world, const ParkObjectRuntimeView& object) {
    const auto id = object.base.header.id;
    if (world.saved.first_park_object == id) {
        world.saved.first_park_object = object_by_id(c, id).next_park_object;
        return;
    }
    auto previous = world.saved.first_park_object;
    for (std::size_t visited = 0; visited < c.entities.size(); ++visited) {
        auto& predecessor = object_by_id(c, previous);
        if (predecessor.next_park_object == id) {
            predecessor.next_park_object = object_by_id(c, id).next_park_object;
            return;
        }
        previous = predecessor.next_park_object;
    }
    throw std::length_error("cyclic park-object roster");
}

std::uint32_t cleanup_world_park_object_instance(AttractionWorld& c, ParkObjectRuntimeView& object) {
    auto* control = find_and_cache_world_object_control(c.simulation.current->object_controls, object.definition_id);
    if (!control)
        throw std::out_of_range("park-object control row");
    --control->live_instance_count;
    unlink_park_object_from_world_list(c, *c.simulation.current, object);
    //Native624280 clears only matching group headers through populate(...,0),
    //retaining count/requirement rows. Activation normally invokes it before
    //retirement becomes possible. The host also repairs direct/AfterShape
    //retirement so a deferred payload ID cannot remain a registry owner.
    for (std::uint32_t index = 0; index < c.attractions.special_ride_group_count; ++index) {
        auto& group = c.attractions.special_ride_groups.at(index);
        if (group.thing_id == object.base.header.id)
            populate_special_ride_registry_entry(c, group, 0);
    }
    WorldEvent event{ ParkObjectRemovedEvent{ object.base.header.id } };
    auto* registry = get_world_event_subscription_registry(c);
    dispatch_world_event_to_subscribers(c, registry, &event, deliver_world_record_event, nullptr);
    auto* definition = get_park_object_definition(c, &object);
    if (!definition)
        throw std::out_of_range("park-object refund definition");
    const auto percent = get_park_object_scrap_value_percent(c, &object);
    //Native loads cost after the scrap query, wraps the DWORD product, and
    //performs unsigned division. Keep the captured definition pointer alive.
    const auto amount = definition->research.upgrades[0].cost_of_upgrade * std::bit_cast<std::uint32_t>(percent) / 100u;
    auto* bank = c.entities.at_payload<BankRecord>(c.simulation.current->saved.bank_account);
    if (!bank)
        throw std::invalid_argument("null cleanup bank account");
    (void)apply_bank_balance_and_profit_delta(c, bank, std::bit_cast<std::int32_t>(amount));
    const auto has = [&](ParkObjectCapabilities flag) {
        return (std::to_underlying(object.capabilities) & std::to_underlying(flag)) != 0;
    };
    const auto remove = [&](RegionEffectProfileId id) {
        (void)remove_world_re_cell_effect_profile(c.simulation.region_effects, id, cell(object));
    };
    if (has(ParkObjectCapabilities::provides_security))
        remove(RegionEffectProfileId::security_camera);
    if (has(ParkObjectCapabilities::is_fireworks)) {
        auto* script = world_operations::find_script(c, object.script_id);
        if (world_operations::script_slot(c, script, 0) < 2)
            remove(RegionEffectProfileId::fireworks);
    }
    if (has(ParkObjectCapabilities::is_effector_feature))
        remove(RegionEffectProfileId::other_feature_effector);
    if (has(ParkObjectCapabilities::provides_relief))
        remove(park_object_requires_staff_service(c, object) ? RegionEffectProfileId::dirty_toilet
                                                             : RegionEffectProfileId::clean_toilet);
    if (has(ParkObjectCapabilities::holds_litter))
        remove(park_object_requires_staff_service(c, object) ? RegionEffectProfileId::dirty_bin
                                                             : RegionEffectProfileId::clean_bin);
    if (has(ParkObjectCapabilities::requires_watering)) {
        auto* script = world_operations::find_script(c, object.script_id);
        match (world_operations::script_slot(c, script, 0)) {
            case 0 => remove(RegionEffectProfileId::fresh_plant);
            case 1 => remove(RegionEffectProfileId::dry_plant);
            case 2 => remove(RegionEffectProfileId::very_dry_plant);
            case 3 => remove(RegionEffectProfileId::dead_plant);
            case _ => {}
        }
    }
    if (object.photo_booth) {
        auto& booth = object_by_id(c, object.photo_booth);
        const auto center = static_cast<std::uint32_t>(cell(booth)) - 1u;
        c.view.secondary_cursor_record_snapshot = &booth.base.header;
        c.ports.construction.refresh_world_interaction_origin_state(c, true);
        c.ports.construction.set_world_interaction_mode(c, WorldInteractionMode::remove_park_object, true);
        (void)c.ports.construction.dispatch_world_interaction(
            c,
            { static_cast<std::int32_t>(center & 127u), static_cast<std::int32_t>(center >> 7) },
            WorldInputModifierMask::none,
            false);
    }
    definition = get_park_object_definition(c, &object);
    if (!definition)
        throw std::out_of_range("park-object photo-booth definition");
    if (definition->is_photo_booth)
        object_by_id(c, object.upgrade_parent).photo_booth = 0;
    if (object.upgrade_balloon_sprite)
        world_operations::release_resource_pool_handle(c, object.upgrade_balloon_sprite);
    const auto flags = c.simulation.current->saved.state == WorldState::clearing ? RsseScriptCleanupFlags::none
                     : c.construction.action_recording_disabled ? RsseScriptCleanupFlags::detach_people
                                                                : static_cast<RsseScriptCleanupFlags>(7);
    world_operations::destroy_rsse_script_state_by_id(c, object.script_id, flags);
    if (c.view.secondary_cursor_record_snapshot == &object.base.header)
        c.view.secondary_cursor_record_snapshot = nullptr;
    if (c.construction.park_object == &object)
        c.construction.park_object = nullptr;
    if (object.history)
        c.ports.memory.release(Allocation::park_history, object.history);
    //Native leaves its history pointer unchanged; payload storage remains live
    //until the pending-removal queue drains after this cleanup phase.
    unlink_world_used_thing_map_and_event_lists(c, &object.base.header);
    return --c.attractions.park_object_record_count;
}

void world_operations::builtin::record_cleanup(WorldRuntime& c, RecordCleanup operation, ThingHeader* record) {
    if (operation == RecordCleanup::person) {
        cleanup_world_person_record(c, *reinterpret_cast<PersonRuntimeView*>(record));
        return;
    }
    if (is_staff_record_kind(record->kind)) {
        cleanup_world_staff_record(c, *reinterpret_cast<StaffRuntimeRecord*>(record));
        return;
    }
    if (operation != RecordCleanup::park_object)
        throw std::logic_error("entity cleanup has not been reconstructed");
    (void)cleanup_world_park_object_instance(c, *reinterpret_cast<ParkObjectRuntimeView*>(record));
}
} //namespace simcoaster
