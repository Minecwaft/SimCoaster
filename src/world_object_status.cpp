#include "simcoaster/layout_replay.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <cwctype>
#include <stdexcept>

#include "world_object_access.hpp"

namespace simcoaster {
namespace {
using namespace object_access;

StaffRuntimeRecord& staff(WorldRuntime& c, ThingId id) {
    if (!id || id >= c.entities.size()) {
        throw std::out_of_range("Park-object UI staff claim ID");
    }
    auto* header = c.entities[id].payload;
    if (!header || header->id != id || header->retirement_pending || !is_staff_record_kind(header->kind)) {
        throw std::out_of_range("Park-object UI staff claim is no longer live");
    }
    return *reinterpret_cast<StaffRuntimeRecord*>(header);
}

bool reciprocal_claim(const StaffRuntimeRecord& worker, ThingId target) {
    return match (worker.base.header.kind) -> bool {
        case ThingKind::mechanic => reinterpret_cast<const MechanicRuntimeRecord&>(worker).object_to_repair == target;
        //62B280 deliberately tests the toilet field for a Handyman, including
        //bin-cleaning state. Do not silently replace it with bin_to_clean.
        case ThingKind::handyman => reinterpret_cast<const HandymanRuntimeRecord&>(worker).toilet_to_clean == target;
        case ThingKind::gardener => reinterpret_cast<const GardenerRuntimeRecord&>(worker).plant_to_water == target;
        case _                   => false;
    };
}

ThingId claim(WorldRuntime& c, ParkObjectRuntimeView& object) {
    if (!object.assigned_staff_member) {
        return 0;
    }
    auto* primary = c.referenced_world();
    if (!primary) {
        throw std::logic_error("Park-object UI primary World is not bound");
    }
    //5E37F0 uses the primary World's unsigned clock and wrapping addition.
    if (primary->saved.game_tick <= object.time_marked_for_maintenance + std::uint32_t { 100 }) {
        return object.assigned_staff_member;
    }
    auto& worker = staff(c, object.assigned_staff_member);
    if (!reciprocal_claim(worker, object.base.header.id)) {
        enter_world_staff_state(c, worker, StaffState::idle);
        object.assigned_staff_member = 0;
        object.time_marked_for_maintenance = 0;
    }
    return object.assigned_staff_member;
}

bool actively_servicing(WorldRuntime& c, ParkObjectRuntimeView& object) {
    const auto id = claim(c, object);
    if (!id) {
        return false;
    }
    const auto& worker = staff(c, id);
    if (!reciprocal_claim(worker, object.base.header.id)) {
        return false;
    }
    return match (worker.base.header.kind) -> bool {
        case ThingKind::mechanic => worker.state == StaffState::repairing;
        case ThingKind::gardener => worker.state == StaffState::watering_plant;
        case ThingKind::handyman => worker.state == StaffState::cleaning_toilet || worker.state == StaffState::cleaning_bin;
        case _                   => false;
    };
}

bool striking(WorldRuntime& c, StaffType role) {
    const auto& control = record<StaffHqRecord>(c, world(c).saved.staff_hq, ThingKind::staff_hq);
    return control.strikes.at(static_cast<std::size_t>(role)).strike_active != 0;
}
} //namespace

ParkObjectUiStatus classify_park_object_ui_status(WorldRuntime& c, ParkObjectRuntimeView& object, std::int32_t mode) {
    if (&record<ParkObjectRuntimeView>(c, object.base.header.id, ThingKind::park_object) != &object) {
        throw std::out_of_range("Park-object UI identity was replaced");
    }
    auto& current_world = world(c);
    const auto* definition = get_park_object_definition(c, &object);
    if (!definition) {
        throw std::out_of_range("Park-object UI definition is missing");
    }
    const auto has = [&](ParkObjectCapabilities flag) {
        return (std::to_underlying(object.capabilities) & std::to_underlying(flag)) != 0;
    };
    if (has(ParkObjectCapabilities::requires_watering) && world_operations::script_slot(c, world_operations::find_script(c, object.script_id), 4)) {
        return ParkObjectUiStatus::dead;
    }
    if (object.closed_due_to_weather) {
        match (record<WeatherRecord>(c, current_world.saved.weather, ThingKind::weather).type) {
            case WeatherCategory::heatwave => return ParkObjectUiStatus::closed_heatwave;
            case WeatherCategory::rain     => return ParkObjectUiStatus::closed_rain;
            case WeatherCategory::storm    => return ParkObjectUiStatus::closed_storm;
            case WeatherCategory::snow     => return ParkObjectUiStatus::closed_snowstorm;
            case _                         => {
            }
        }
    }
    if (object.state == ParkObjectState::condemned) {
        return ParkObjectUiStatus::condemned;
    }
    if (definition->which_track_type > 0 && definition->which_track_type <= 2) {
        if (!object.is_track_ride_valid) {
            return ParkObjectUiStatus::track_incomplete;
        }
    } else if (definition->which_track_type == 3) {
        const auto* opening = world_operations::find_mesh_runtime_opening_state(c, object.mesh_instance_id);
        if (!opening || !(opening->placement_flags & 1u) || (opening->placement_flags & 2u)) {
            return ParkObjectUiStatus::track_incomplete;
        }
        if (opening->geometry_peer_reference_count) {
            return ParkObjectUiStatus::track_invalid;
        }
    }
    if (object.state == ParkObjectState::upgrade_requested) {
        if (!current_world.saved.first_mechanic) {
            return ParkObjectUiStatus::upgrade_no_engineers;
        }
        if (striking(c, StaffType::mechanic)) {
            return ParkObjectUiStatus::upgrade_engineers_on_strike;
        }
        if (!claim(c, object)) {
            return ParkObjectUiStatus::upgrade_waiting_for_engineer;
        }
        return actively_servicing(c, object) ? ParkObjectUiStatus::upgrading : ParkObjectUiStatus::upgrade_engineer_assigned;
    }
    if (object.requested_service) {
        if (has(ParkObjectCapabilities::provides_relief)) {
            if (!current_world.saved.first_handyman) {
                return ParkObjectUiStatus::cleaning_no_janitors;
            }
            if (striking(c, StaffType::handyman)) {
                return ParkObjectUiStatus::cleaning_janitors_on_strike;
            }
            if (!claim(c, object)) {
                return ParkObjectUiStatus::cleaning_waiting_for_janitor;
            }
            return actively_servicing(c, object) ? ParkObjectUiStatus::being_cleaned : ParkObjectUiStatus::cleaning_janitor_assigned;
        }
        if (!current_world.saved.first_mechanic) {
            return ParkObjectUiStatus::repair_no_engineers;
        }
        if (striking(c, StaffType::mechanic)) {
            return ParkObjectUiStatus::repair_engineers_on_strike;
        }
        if (!claim(c, object)) {
            return ParkObjectUiStatus::repair_waiting_for_engineer;
        }
        return actively_servicing(c, object) ? ParkObjectUiStatus::being_repaired : ParkObjectUiStatus::repair_engineer_assigned;
    }
    if (object.state == ParkObjectState::broken_down) {
        return actively_servicing(c, object) ? ParkObjectUiStatus::being_repaired : ParkObjectUiStatus::broken_down;
    }
    if (has(ParkObjectCapabilities::has_queue) && !has_usable_entry_approach(c, &object)) {
        return object.can_load ? ParkObjectUiStatus::entry_not_connected : ParkObjectUiStatus::closed_entry_not_connected;
    }
    if (definition->which_ui_type == ObjectUiCategory::rides && static_cast<std::uint16_t>(definition->add_on.upgrades_id) == 0 && definition->add_on.upgrade_type != 2 && !has_clear_entry_exit_path(c, object)) {
        return ParkObjectUiStatus::exit_not_connected;
    }
    if (!mode) {
        return ParkObjectUiStatus::closed;
    }
    if (mode <= 1) {
        return ParkObjectUiStatus::open;
    }
    if (current_world.saved.park_closed) {
        return ParkObjectUiStatus::park_closed;
    }
    return object.can_load ? ParkObjectUiStatus::open : ParkObjectUiStatus::closed;
}
} //namespace simcoaster
