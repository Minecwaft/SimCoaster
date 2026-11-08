#include "world_staff_internal.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mail.hpp"
#include <memory>

namespace simcoaster {
using namespace staff_detail;

namespace {
struct Role {
    ThingKind kind;
    std::uint32_t sprite;
};

constexpr Role role(StaffType type) {
    return match (type) -> Role {
        case StaffType::handyman => {
            ThingKind::handyman, 9
        };
        case StaffType::mechanic => {
            ThingKind::mechanic, 10
        };
        case StaffType::entertainer => {
            ThingKind::entertainer, 8
        };
        case StaffType::guard => {
            ThingKind::guard, 11
        };
        case StaffType::researcher => {
            ThingKind::researcher, 12
        };
        case StaffType::gardener => {
            ThingKind::gardener, 18
        };
        case _ => throw std::out_of_range("hired staff role");
    };
}

void initialize_common(StaffWorld& c, StaffRuntimeRecord& s, CellId cell, const StaffCandidate& candidate) {
    initialize_world_mobile_entity_base(c, s.base, s.mobile, cell, role(candidate.type).kind);
    //The mobile constructor already established navigation at the placement
    //cell. Initialize staff members without running the deserialization reset.
    s.time_hired = 0;
    s.happiness_score = s.motivation = s.energy = 0;
    s.area.bounds = {};
    s.area.begin = s.area.end = s.area.capacity = nullptr;
    for (auto& ring : s.activity_rings) {
        ring.temporary = 0;
        ring.current_entry = -1;
        ring.num_entries = 30;
        ring.wrapped = 0;
    }
    s.salary = candidate.salary;
    s.experience = candidate.experience;
    s.agency = candidate.agency_index;
    s.area.header.bits = (s.area.header.bits & ~255u) | static_cast<std::uint32_t>(s.base.header.kind);
    s.research_mode = 0;
    world_operations::set_staff_research_mode(c, &s, 0);
    s.slot_location = -1;
    FileTime now;
    calculate_world_calendar_filetime(c, &world(c).calendar, &now);
    s.time_hired = std::uint64_t { now.low } | (std::uint64_t { now.high } << 32);
    s.rest_area = 0;
    s.happiness_score = s.motivation = s.energy = 100;
    s.jobs_done = s.time_started_idling = 0;
    s.research_room = 0;
    s.time_started_researching = 0;
    s.first_known_ability = s.currently_learning = ThemeAbility::none;
    s.second_ability_or_count.bits = 0;
    s.ability_progress = 0;
    s.area.bounds = { 0, 0, 127, 127 };
    set_mobile_entity_purpose_speed(s.mobile, PurposeSpeedMode::normal);
    s.mobile.base_speed = speed(c, s, mobile_base_speed_variants[2]);
    for (int event : { 12, 11, 10 }) {
        register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &s.base.header, event);
    }
    calculate_world_calendar_filetime(c, &world(c).calendar, &now);
    record<ParkAnalyserState>(c, world(c).saved.park_analyser).last_staff_hired = std::uint64_t { now.low } | (std::uint64_t { now.high } << 32);
    s.strike_ticks = s.researching_ticks = s.training_ticks = s.idle_ticks = s.resting_ticks = s.patrolling_ticks = s.working_ticks = 0;
    s.researching = 0;
    s.wage_demand_day = s.wage_demand_pay = 0;
}

void set_name(StaffWorld& c, StaffRuntimeRecord& s, const StaffCandidate& candidate) {
    //should_use_female_staff_name_table is a literal false in this executable.
    const auto& name = c.staff.recruitment.names.at(static_cast<std::size_t>(candidate.type)).at(candidate.name_index);
    const auto length = std::min<std::size_t>(name.size(), 31);
    std::copy_n(name.begin(), length, s.name.begin());
    s.name[length] = 0;
}
} //namespace

namespace {
ThingId create_staff(StaffWorld& c, CellId initial_cell, const StaffCandidate& candidate, bool hired) {
    const auto info = role(candidate.type);
    const auto* descriptor = record_descriptor(info.kind);
    if (!descriptor) {
        throw std::logic_error("hired staff descriptor is missing");
    }
    if (!initial_cell || initial_cell > map_cell_count) {
        throw std::out_of_range("hired staff placement cell");
    }
    //Resolve the borrowed name before publishing a record. A bad candidate must
    //not leave an allocation or a map/roster link behind.
    if (hired) {
        (void)c.staff.recruitment.names.at(static_cast<std::size_t>(candidate.type)).at(candidate.name_index);
    }
    const auto previous_hire_time = record<ParkAnalyserState>(c, world(c).saved.park_analyser).last_staff_hired;
    auto* storage = static_cast<ThingHeader*>(c.entities.allocate_payload(c.ports.memory, descriptor->allocation_size));
    if (!storage) {
        throw std::bad_alloc();
    }
    descriptor->construct(storage);
    auto& s = *reinterpret_cast<StaffRuntimeRecord*>(storage);
    bool roster_linked = false, influence_added = false;
    std::uint16_t influence_radius = 0;
    try {
        //Native initialization publishes the metadata/map registration before role
        //setup and task selection. Subscribers and paths see that same live record.
        initialize_common(c, s, initial_cell, candidate);
        auto variant = candidate.resource_variant;
        const auto choose_variant = [&] {
            const auto count = get_resource_category_appearance_variant_count(c, info.sprite);
            if (count <= 0) {
                throw std::domain_error("staff sprite category is empty");
            }
            return (static_cast<std::uint32_t>(next_world_prng_value(primary(c))) >> 2) % static_cast<std::uint32_t>(count);
        };
        //Native Researcher draws its byte variant before publishing the roster link;
        //Guard links first. Neither path consumes the candidate generator's RNG.
        if (!hired && info.kind == ThingKind::researcher) {
            variant = static_cast<std::uint8_t>(choose_variant());
        }
        s.next_staff = 0;
        auto& first = head(world(c), candidate.type);
        const auto old = first;
        first = s.base.header.id;
        if (old) {
            s.next_staff = old;
        }
        roster_linked = true;
        s.mobile.sprite_category = info.sprite;
        s.mobile.sprite_id = variant;
        if (info.kind == ThingKind::guard) {
            s.mobile.sprite_id = choose_variant();
        }
        set_world_sprite_animation_resource(c, s.mobile.animation, 3, static_cast<std::int32_t>(info.sprite), static_cast<std::int32_t>(s.mobile.sprite_id));
        match (info.kind) {
            case ThingKind::mechanic => {
                auto& m = reinterpret_cast<MechanicRuntimeRecord&>(s);
                m.object_to_repair = 0;
                m.repair_duration = 0;
                set_world_mechanic_state(c, m, StaffState::idle);
                register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &s.base.header, 10);
                choose_mechanic_repair_task(c, m);
            }
            case ThingKind::handyman => {
                auto& h = reinterpret_cast<HandymanRuntimeRecord&>(s);
                h.time_started_cleaning = 0;
                h.target_litter_cell = h.toilet_to_clean = h.bin_to_clean = 0;
                register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &s.base.header, 10);
                choose_and_start_handyman_task(c, h);
            }
            case ThingKind::gardener => {
                auto& g = reinterpret_cast<GardenerRuntimeRecord&>(s);
                g.plant_to_water = 0;
                g.time_started_watering = 0;
                register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &s.base.header, 10);
                start_world_gardener_task(c, g);
            }
            case ThingKind::entertainer => {
                s.mobile.base_speed = speed(c, s, mobile_base_speed_variants[0]);
                const auto& b = c.staff.recruitment.balance.Entertainer;
                influence_radius = static_cast<std::uint16_t>(interpolate(c, s, b.MinActiveRange, b.MaxActiveRange));
                add_world_re_cell_effect_profile_with_radius(c.simulation.region_effects, RegionEffectProfileId::entertainer, cell(s.base), influence_radius);
                influence_added = true;
                reinterpret_cast<EntertainerRuntimeRecord&>(s).time_started_entertaining = 0;
                update_entertainer_idle_and_work_state(c, reinterpret_cast<EntertainerRuntimeRecord&>(s));
            }
            case ThingKind::guard => {
                const auto& b = c.staff.recruitment.balance.Guard;
                influence_radius = static_cast<std::uint16_t>(interpolate(c, s, b.MinDetectionRange, b.MaxDetectionRange));
                add_world_re_cell_effect_profile_with_radius(c.simulation.region_effects, RegionEffectProfileId::guard, cell(s.base), influence_radius);
                influence_added = true;
                auto& g = reinterpret_cast<GuardRuntimeRecord&>(s);
                g.perpetrator = g.patient = 0;
                g.prosecution_timestamp = g.first_aid_timestamp = 0;
                (void)mark_tag_messages_by_primary_key_for_deletion(c, &record<TagSystemRecord>(c, world(c).saved.tag_system), 58);
                register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &s.base.header, 33);
                update_world_guard_idle_or_patrol_state(c, g);
            }
            case ThingKind::researcher => {
                s.research_room = 0;
                start_world_researcher_task(c, s);
                (void)update_research_lab_active_state_from_researcher_list(c, &record<ResearchLabRecord>(c, world(c).saved.research_lab));
                world_operations::set_staff_research_mode(c, &s, hired ? 0 : 1);
            }
            case _ => ;
        } if (hired) {
            set_name(c, s, candidate);
        } else if (info.kind == ThingKind::guard) {
            enter_world_staff_state(c, s, StaffState::walking_to_training_room);
        }
        return s.base.header.id;
    } catch (...) {
        const auto id = s.base.header.id;
        if (id && c.entities.at(id).payload == storage) {
            if (roster_linked) {
                auto* link = &head(world(c), candidate.type);
                while (*link && *link != id) {
                    link = &record<StaffRuntimeRecord>(c, *link).next_staff;
                }
                if (*link) {
                    *link = s.next_staff;
                }
            }
            for (auto object_id = world(c).saved.first_park_object; object_id;) {
                auto& target = object(c, object_id);
                if (target.assigned_staff_member == id) {
                    target.assigned_staff_member = 0;
                    target.time_marked_for_maintenance = 0;
                }
                object_id = target.next_park_object;
            }
            for (auto& target : world(c).map_cells) {
                if (target.litter_collector == id) {
                    target.litter_collector = 0;
                    target.litter_collection_tick = 0;
                }
            }
            if (influence_added) {
                remove_world_re_cell_effect_profile_with_radius(c.simulation.region_effects, info.kind == ThingKind::guard ? RegionEffectProfileId::guard : RegionEffectProfileId::entertainer, cell(s.base), influence_radius);
            }
            if (s.mobile.animation.sprite_script) {
                world_operations::release_resource_pool_handle(c, s.mobile.animation.sprite_script);
            }
            if (s.mobile.thought_history.thought_script) {
                world_operations::release_resource_pool_handle(c, s.mobile.thought_history.thought_script);
            }
            if (c.simulation.event_registry) {
                remove_world_used_thing_from_all_events(c, c.simulation.event_registry, storage);
            }
            unlink_world_used_thing_from_map_cell(c, &c.simulation.map_cell_records[initial_cell - 1], id);
            unlink_and_free_world_used_thing(c, &c.entities[id]);
        } else {
            c.entities.release_payload(c.ports.memory, storage);
        }
        if (!hired && info.kind == ThingKind::researcher) {
            auto* lab = c.entities.at(world(c).saved.research_lab).payload;
            if (lab && lab->kind == ThingKind::research_lab) {
                reinterpret_cast<ResearchLabRecord*>(lab)->lab_active = world(c).saved.first_researcher != 0;
            }
        }
        record<ParkAnalyserState>(c, world(c).saved.park_analyser).last_staff_hired = previous_hire_time;
        throw;
    }
}
} //namespace

ThingId create_hired_world_staff_record(StaffWorld& c, CellId cell, const StaffCandidate& candidate) {
    return create_staff(c, cell, candidate, true);
}

ThingId create_debug_world_staff_record(StaffWorld& c, CellId cell, StaffType type) {
    if (type != StaffType::guard && type != StaffType::researcher) {
        throw std::invalid_argument("developer staff constructor role");
    }
    StaffCandidate candidate {};
    candidate.type = type;
    candidate.experience = c.staff.recruitment.balance.all.MaxExperience / 8;
    return create_staff(c, cell, candidate, false);
}

bool hire_staff_from_prefix_slot(StaffWorld& c, World& owner, std::size_t index) {
    if (index >= owner.staff_candidates.size() || !owner.staff_candidates[index].valid) {
        return false;
    }
    const auto candidate = owner.staff_candidates[index];
    if (static_cast<std::uint32_t>(candidate.type) < 6u) {
        if (c.staff.placement) {
            cancel_pending_staff_placement(c);
        }
        cancel_world_staff_interaction(c);
        c.staff.placement = PendingStaffPlacement { candidate, index };
        activate_hired_staff_placement_interaction(c);
    }
    owner.staff_candidates[index].on_pointer = 1;
    recount_world_staff_counts(c, owner);
    for (std::size_t i = 0; i < owner.staff_candidates.size(); ++i) {
        auto& row = owner.staff_candidates[i];
        if (row.valid && owner.staff_pool.stop_producing.at(static_cast<std::size_t>(row.type)) && !row.on_pointer && c.staff.recruitment.selected_candidate != static_cast<std::int32_t>(i)) {
            row.valid = 0;
            c.staff.recruitment.changed_slots |= 1u << i;
        }
    }
    return true;
}

void release_world_staff_entity_links(StaffWorld& c, World& owner, bool preserve) {
    for (std::size_t i = 0; i < owner.staff_candidates.size(); ++i) {
        auto& row = owner.staff_candidates[i];
        if (!row.on_pointer) {
            continue;
        }
        if (!preserve) {
            row.valid = 0;
            c.staff.recruitment.changed_slots |= 1u << i;
        }
        row.on_pointer = 0;
    }
}

void cancel_pending_staff_placement(StaffWorld& c, bool confirmed) {
    if (!c.staff.placement) {
        return;
    }
    if (c.staff.interaction.initialized && world_staff_interaction_kind(c) == WorldInteractionHandlerKind::hired_staff) {
        std::get<HiredStaffInteraction>(c.staff.interaction.handler.value).placement_confirmed = confirmed;
        cancel_world_staff_interaction(c);
        return;
    }
    const auto resource = c.staff.placement->preview_resource;
    if (resource) {
        world_operations::release_resource_pool_handle(c, resource);
    }
    c.staff.placement->preview_resource = 0;
    release_world_staff_entity_links(c, primary(c), !confirmed);
    world_operations::cancel_advisor_action_by_id(c, &record<AdvisorRecord>(c, primary(c).saved.advisor), 254);
    c.staff.placement.reset();
}

ThingId place_pending_hired_staff_at_cell(StaffWorld& c, CellId accepted_cell) {
    if (!c.staff.placement) {
        throw std::logic_error("no pending staff placement");
    }
    const auto id = create_hired_world_staff_record(c, accepted_cell, c.staff.placement->candidate);
    cancel_pending_staff_placement(c, true);
    return id;
}
} //namespace simcoaster
