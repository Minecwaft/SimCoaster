#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_staff_interaction.hpp"
#include "world_staff_internal.hpp"

namespace simcoaster {
using namespace staff_detail;

void cleanup_world_staff_record(StaffWorld& c, StaffRuntimeRecord& s) {
    const auto association = world_staff_association_state(s);
    //Unlike ordinary room exit this must neither allocate a replacement sprite
    //nor choose new work. Event33 has already removed research/HQ references.
    detach_staff_room_occupancy(c, s);
    s.rest_area = s.research_room = 0;
    s.currently_learning = s.going_to_learn = ThemeAbility::none;
    if (c.staff.interaction.initialized) {
        bool cancel = false;
        const auto control_id = primary(c).saved.staff_hq;
        if (control_id && control_id < c.entities.size()) {
            auto* value = c.entities[control_id].payload;
            if (value && value->kind == ThingKind::staff_hq) {
                auto& control = *reinterpret_cast<StaffHqRecord*>(value);
                if (control.staff_member_picked_up == s.base.header.id) {
                    control.staff_member_picked_up = 0;
                    cancel = std::holds_alternative<HeldStaffInteraction>(c.staff.interaction.handler.value);
                }
            }
        }
        if (auto* editor = std::get_if<StaffAreaSelectionInteraction>(&c.staff.interaction.handler.value); editor && editor->target == s.base.header.id) {
            //Do not let the handler destructor freeze/drop/recreate this actor.
            editor->target = 0;
            editor->panel_open = false;
            cancel = true;
        }
        if (cancel) {
            cancel_world_staff_interaction(c);
        }
        if (c.staff.interaction.training_picker.staff == s.base.header.id) {
            c.staff.interaction.training_picker = {};
        }
        for (auto& pending : c.staff.interaction.training_pending) {
            if (pending.staff == s.base.header.id) {
                pending = {};
                if (c.ui.panels.training_room.active_row_count > 0) {
                    --c.ui.panels.training_room.active_row_count;
                }
            }
        }
    }
    const auto release_owned_claim = [&](ThingId id) {
        if (!id || id >= c.entities.size()) {
            return;
        }
        auto* value = c.entities[id].payload;
        if (!value || value->kind != ThingKind::park_object || value->retirement_pending) {
            return;
        }
        auto& target = *reinterpret_cast<ParkObjectRuntimeView*>(value);
        if (target.assigned_staff_member == s.base.header.id) {
            target.assigned_staff_member = 0;
            target.time_marked_for_maintenance = 0;
        }
    };
    const auto role = world_staff_type_for_used_thing(s.base.header);
    auto* link = &head(world(c), role);
    std::size_t scanned = 0;
    while (*link != s.base.header.id) {
        if (!*link || scanned++ >= c.entities.size()) {
            throw std::logic_error("retired staff is absent from its roster");
        }
        link = &record<StaffRuntimeRecord>(c, *link).next_staff;
    }
    *link = s.next_staff; //Native unlink retains the departing record's own link.
    match (s.base.header.kind) {
        case ThingKind::mechanic => {
            auto& target = reinterpret_cast<MechanicRuntimeRecord&>(s).object_to_repair;
            release_owned_claim(std::exchange(target, 0));
        }
        case ThingKind::handyman => {
            auto& h = reinterpret_cast<HandymanRuntimeRecord&>(s);
            if (h.target_litter_cell) {
                auto& target = world(c).map_cells.at(h.target_litter_cell - 1);
                if (target.litter_collector == s.base.header.id) {
                    target.litter_collector = 0;
                    target.litter_collection_tick = 0;
                }
                h.target_litter_cell = 0;
            }
            release_owned_claim(std::exchange(h.toilet_to_clean, 0));
            release_owned_claim(std::exchange(h.bin_to_clean, 0));
        }
        case ThingKind::gardener => {
            auto& target = reinterpret_cast<GardenerRuntimeRecord&>(s).plant_to_water;
            release_owned_claim(std::exchange(target, 0));
        }
        case ThingKind::entertainer or ThingKind::guard => {
            const bool guard = s.base.header.kind == ThingKind::guard;
            const auto radius = guard ? interpolate(c, s, c.staff.recruitment.balance.Guard.MinDetectionRange, c.staff.recruitment.balance.Guard.MaxDetectionRange) : interpolate(c, s, c.staff.recruitment.balance.Entertainer.MinActiveRange, c.staff.recruitment.balance.Entertainer.MaxActiveRange);
            const auto old = std::exchange(s.mobile.last_recorded_map_id, cell(s.base));
            remove_world_re_cell_effect_profile_with_radius(c.simulation.region_effects, guard ? RegionEffectProfileId::guard : RegionEffectProfileId::entertainer, old, static_cast<std::uint16_t>(radius));
            if (guard) {
                auto& g = reinterpret_cast<GuardRuntimeRecord&>(s);
                if (association == StaffState::walking_to_entrance && g.perpetrator && g.perpetrator < c.entities.size()) {
                    auto* target = c.entities[g.perpetrator].payload;
                    if (target && target->kind == ThingKind::person && !target->retirement_pending) {
                        release_escorted_person(c, *reinterpret_cast<PersonRuntimeView*>(target), cell(s.base));
                    }
                }
                g.perpetrator = g.patient = 0;
            }
        }
        case _ => ;
    } s.state = StaffState::idle;
    unlink_world_used_thing_map_and_event_lists(c, &s.base.header);
    //The deferred free sees only ThingKind::retired, so its concrete-Staff
    //fallback cannot discover this owned range after normal retirement.
    release_staff_membership_storage(c, s);
    if (s.mobile.animation.sprite_script) {
        release_world_sprite_animation_resource_handle(c, s.mobile.animation);
    }
    if (s.mobile.thought_history.thought_script) {
        world_operations::release_resource_pool_handle(c, s.mobile.thought_history.thought_script);
    }
    if (s.base.header.kind == ThingKind::researcher) {
        auto* lab = c.entities.at(world(c).saved.research_lab).payload;
        if (lab && lab->kind == ThingKind::research_lab && !lab->retirement_pending) {
            (void)update_research_lab_active_state_from_researcher_list(c, reinterpret_cast<ResearchLabRecord*>(lab));
        }
    }
}
} //namespace simcoaster
