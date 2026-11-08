#include "world_staff_internal.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/resource_variant.hpp"
#include "simcoaster/rsse_actions.hpp"
#include "simcoaster/rsse_state.hpp"

namespace simcoaster {
using namespace staff_detail;

namespace staff_detail {
ThingId claim_owner(StaffWorld& c, ParkObjectRuntimeView& target) {
    const auto id = target.assigned_staff_member;
    if (!id) {
        return 0;
    }
    if (world(c).saved.game_tick <= target.time_marked_for_maintenance + 100u) {
        return id;
    }
    const auto& owner = record<StaffRuntimeRecord>(c, id);
    const bool reciprocal = match (owner.base.header.kind) -> bool {
        case ThingKind::mechanic => reinterpret_cast<const MechanicRuntimeRecord&>(owner).object_to_repair == target.base.header.id;
        //This native predicate tests the toilet target only; a bin target alone
        //does not keep an otherwise expired handyman assignment alive.
        case ThingKind::handyman => reinterpret_cast<const HandymanRuntimeRecord&>(owner).toilet_to_clean == target.base.header.id;
        case ThingKind::gardener => reinterpret_cast<const GardenerRuntimeRecord&>(owner).plant_to_water == target.base.header.id;
        case _                   => false;
    };
    if (!reciprocal) {
        release_claim(c, target);
    }
    return target.assigned_staff_member;
}
} //namespace staff_detail

namespace {
void close_admission(StaffWorld& c, ParkObjectRuntimeView& o) {
    //This native tail does not eject the people still in the queue.
    o.can_load = 0;
    o.person_being_loaded = 0;
    world_operations::set_script_slot(c, script(c, o), 6, 1);
    world_operations::select_park_object_mesh_animation_variant(c, o.mesh_instance_id, ParkObjectMeshVariant::closed);
}

std::uint8_t clamp_setting(std::uint8_t requested, std::int32_t minimum, std::int32_t maximum) {
    if (requested < minimum) {
        return static_cast<std::uint8_t>(minimum);
    }
    if (requested > maximum) {
        return static_cast<std::uint8_t>(maximum);
    }
    return requested;
}

bool weather_closes(StaffWorld& c, ParkObjectRuntimeView& o) {
    const auto& b = get_park_object_balance(c, o).WeatherModifier;
    return match (record<WeatherRecord>(c, primary(c).saved.weather).type) -> std::int32_t {
        case WeatherCategory::heatwave => b.ClosedDuringHeatWave;
        case WeatherCategory::rain     => b.ClosedDuringRain;
        case WeatherCategory::storm    => b.ClosedDuringStorm;
        case WeatherCategory::snow     => b.ClosedDuringSnow;
        case _                         => 0;
    } == 1;
}

bool can_remain_open(StaffWorld& c, ParkObjectRuntimeView& o) {
    return o.state != ParkObjectState::broken_down && o.state != ParkObjectState::condemned && !o.requested_service && o.state != ParkObjectState::upgrade_requested && has_usable_entry_approach(c, &o) && !is_park_object_restricted_by_hot_dog_scare_challenge(c, o) && !world(c).saved.park_closed && !weather_closes(c, o) && (get_park_object_balance(c, o).Bumper.WhichTrackType != 3 || is_track_type_3_park_object_runtime_openable(c, &o));
}
} //namespace

int handle_park_object_maintenance_request(StaffWorld& c, ParkObjectRuntimeView& o, bool requested) {
    if (requested) {
        if (!o.requested_service) {
            o.requested_service = 1;
            if (!has(o, ParkObjectCapabilities::holds_litter) && !has(o, ParkObjectCapabilities::requires_watering)) {
                close_admission(c, o);
            }
        }
    } else if (o.requested_service) {
        o.requested_service = 0;
        if (!has(o, ParkObjectCapabilities::holds_litter) && !has(o, ParkObjectCapabilities::requires_watering)) {
            open_park_object(c, o);
        }
        if (o.assigned_staff_member) {
            enter_world_staff_state(c, record<StaffRuntimeRecord>(c, o.assigned_staff_member), StaffState::idle);
            o.assigned_staff_member = 0;
            o.time_marked_for_maintenance = 0;
        }
    }
    return 1;
}

void complete_park_object_mechanic_task(StaffWorld& c, ParkObjectRuntimeView& o, ThingId) {
    auto* state = script(c, o);
    o.repair_state = 100;
    world_operations::set_script_slot(c, state, 8, 0);
    o.assigned_staff_member = 0;
    o.requested_service = 0;
    if (o.state != ParkObjectState::upgrade_requested) {
        (void)world_operations::script_slot(c, state, 7);
        (void)world_operations::script_slot(c, state, 4);
        world_operations::set_script_slot(c, state, 4, 0);
        world_operations::configure_mesh_instance_deformation_animation(c, o.mesh_instance_id);
    } else {
        ++o.current_upgrade;
        if (o.upgrade_balloon_sprite) {
            release_resource_variant(c, o.upgrade_balloon_sprite);
        }
        const auto first = static_cast<std::uint32_t>(resolve_queue_start_item_id(c, &o)) - 1u;
        float x = static_cast<float>(first & 127u) + .5f;
        const auto second = static_cast<std::uint32_t>(resolve_queue_start_item_id(c, &o)) - 1u;
        float z = static_cast<float>(second >> 7) + .5f;
        if (o.angle == 0 || o.angle == 90 || o.angle == 180 || o.angle == 270) {
            x *= 10;
            z *= 10;
        }
        if (!state) {
            throw std::logic_error("mechanic upgrade has no RSSE state");
        }
        spawn_rsse_upgrade_effect(c, *state);
        if (o.current_upgrade == 1 || o.current_upgrade == 2) {
            o.upgrade_balloon_sprite = world_operations::allocate_resource_variant(c, { ResourceProgramFamily::upgrade_balloon, 0 }, { 16, 0, 0, std::bit_cast<std::uint32_t>(x), 0, std::bit_cast<std::uint32_t>(z) });
        }
        const auto& definition = get_park_object_balance(c, o);
        const auto& upgrade = definition.Upgrades.at(o.current_upgrade);
        (void)charge_world_bank_account_expense(c, &record<BankRecord>(c, world(c).saved.bank_account), upgrade.CostOfUpgrade);
        if (upgrade.InitSpeed > 0) {
            set_rsse_script_state_operating_speed(c, script(c, o), static_cast<std::int16_t>(upgrade.InitSpeed));
            o.operating_speed = static_cast<std::uint32_t>(upgrade.InitSpeed);
        }
        if (upgrade.InitCapacity > 0) {
            auto capacity = static_cast<std::uint8_t>(upgrade.InitCapacity);
            if (fixed::add(definition.UsageInfo.MinCapacity, definition.UsageInfo.MaxCapacity) > 0) {
                capacity = clamp_setting(capacity, definition.UsageInfo.MinCapacity, definition.UsageInfo.MaxCapacity);
            }
            world_operations::set_script_slot(c, script(c, o), 2, capacity);
            o.operating_capacity = capacity;
        }
        if (upgrade.InitDuration > 0) {
            const auto duration = clamp_setting(static_cast<std::uint8_t>(upgrade.InitDuration), definition.UsageInfo.MinDuration, definition.UsageInfo.MaxDuration);
            world_operations::set_script_slot(c, script(c, o), 3, duration);
            o.operating_duration = duration;
        }
        (void)mark_matching_mail_for_deletion(c, &record<TagSystemRecord>(c, world(c).saved.tag_system), 14, o.definition_id);
    }
    open_park_object(c, o);
    if (!can_remain_open(c, o) && !is_ride_restricted_by_safety_review_challenge(c, o)) {
        close_admission(c, o);
    }
    if (weather_closes(c, o)) {
        close_admission(c, o);
        o.closed_due_to_weather = 1;
    }
    if (get_park_object_balance(c, o).Bumper.WhichTrackType == 3 && !is_track_type_3_park_object_runtime_openable(c, &o)) {
        close_admission(c, o);
    }
}
} //namespace simcoaster
