#include "rsse_tour_internal.hpp"

namespace simcoaster::tour_detail {
void animation(ScriptWorld& c, RsseTourEntry& e, int selector, std::uint32_t variant, int loop) {
    if (!e.mesh_instance_handle || (e.cached_animation_selector == selector && e.cached_animation_value == variant && e.cached_animation_mode == loop)) {
        return;
    }
    (void)world_operations::request_mesh_animation_control_transition(c, e.mesh_instance_handle, static_cast<MeshAnimationRequest>(selector), variant, loop ? MeshAnimationTransitionFlags::loop : static_cast<MeshAnimationTransitionFlags>(0), 1.f, 0);
    e.cached_animation_selector = selector;
    e.cached_animation_value = variant;
    e.cached_animation_mode = loop;
}

void release_passenger(ScriptWorld& c, RsseTourEntry& e, int ordinal) {
    if (!e.mesh_object_count || !e.mesh_instance_handle) {
        return;
    }
    const auto node = fixed::add(fixed::add(fixed::word(e.mesh_object_start), ordinal) % static_cast<int>(e.mesh_object_count), 1);
    const auto index = c.ports.meshes.find_embedded_node_resource_record(c, e.mesh_instance_handle, 128, node);
    if (index != -1) {
        world_operations::release_mesh_model_object_instance(c, e.mesh_instance_handle, index);
    }
}

void release_element(ScriptWorld& c, RsseTourRecord& tour, RsseTourEntry& e) {
    if (e.particle_runtime_handle) {
        c.ports.effects.set_particle_effect_lifetime(c, e.particle_runtime_handle, -2);
    }
    e.particle_runtime_handle = 0;
    world_operations::destroy_mesh(c, e.mesh_instance_handle);
    e.mesh_instance_handle = 0;
    --tour.element_count;
    if (e.positioned_sound_request) {
        c.ports.resources.request_target_recycle(c, e.positioned_sound_request);
    }
    e.positioned_sound_request = 0;
}

void sound_parameter(ScriptWorld& c, const RsseTourRecord& r, RsseTourEntry& e, int value) {
    const auto operation = get_rsse_sound_script_slot(c, r.script_state_id, 10);
    if (e.positioned_sound_request) {
        e.positioned_sound_request = dispatch_resource_manager_operation(c, e.positioned_sound_request, operation, value);
    }
}

void random_outside_target(ScriptWorld& c, RsseTourEntry& e) {
    const auto& bounds = c.scripts.tours.bounds;
    e.target_y = 14000;
    if (bounds.max_x <= bounds.min_x || bounds.max_z <= bounds.min_z) {
        throw std::domain_error("TOUR bounds are empty");
    }
    if (random(c) & 1) {
        e.target_x = fixed::add(bounds.min_x, mul(100, random(c)) % fixed::subtract(bounds.max_x, bounds.min_x));
        e.target_z = (random(c) & 1) ? fixed::add(bounds.max_z, 45000) : fixed::subtract(bounds.min_z, 45000);
    } else {
        e.target_z = fixed::add(bounds.min_z, mul(100, random(c)) % fixed::subtract(bounds.max_z, bounds.min_z));
        e.target_x = (random(c) & 1) ? fixed::add(bounds.max_x, 45000) : fixed::subtract(bounds.min_x, 45000);
    }
}
} //namespace simcoaster::tour_detail

namespace simcoaster {
using namespace tour_detail;

void initialize_rsse_tours(ScriptWorld& c) {
    auto& m = c.scripts.tours;
    if (m.initialized) {
        return;
    }
    m.initialized = true;
    m.restore_init_flag = 1;
    for (auto& slot : m.slots) {
        slot.reset();
    }
    for (std::size_t i = 0; i < m.sine_table.size(); ++i) {
        m.sine_table[i] = fixed::truncate_integer(std::sin(static_cast<double>(2 * i) * .00076698974609375) * 1024. + .5);
    }
}

void update_rsse_tour_bounds(ScriptWorld& c) {
    const auto& map = c.visitors.environment.MapInfo;
    c.scripts.tours.bounds = { fixed::subtract(mul(3000, map.HeightfieldWidth), 10500), fixed::add(mul(3000, map.HeightfieldXStart), 10500), fixed::subtract(mul(3000, map.HeightfieldHeight), 10500), fixed::add(mul(3000, map.HeightfieldYStart), 10500) };
}

std::uint32_t create_rsse_tour(ScriptWorld& c, std::uint16_t owner, std::uint32_t script, int x, int y, int z, int heading, int short_approach) {
    initialize_rsse_tours(c);
    std::uint32_t handle = 1;
    for (; handle < c.scripts.tours.slots.size() && c.scripts.tours.slots[handle]; ++handle) {}
    if (handle >= c.scripts.tours.slots.size()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    auto r = std::make_unique<RsseTourRecord>();
    r->reference_x = x;
    r->reference_y = y;
    r->reference_z = z;
    r->separation_scale = r->attitude_scale = 1024;
    r->departure_heading = fixed::subtract(1024, heading);
    r->owner_used_thing_id = owner;
    r->short_approach_enabled = short_approach;
    r->script_state_id = script;
    r->target_element_count = r->requested_element_count = 1;
    r->flight_duration_ticks = 300;
    r->landing_approach_reserved = 1;
    r->tour_enabled = 1;
    r->base_propulsion_setting = 300;
    r->speed_adjustment_tenths = 5;
    c.scripts.tours.slots[handle] = std::move(r);
    try {
        (void)spawn_rsse_tour_element(c, handle);
    } catch (...) {
        c.scripts.tours.slots[handle].reset();
        throw;
    }
    auto& tour = *c.scripts.tours.slots[handle];
    tour.approach_radius = short_approach ? 2500 : 5000;
    tour.active_effect_mode = Effect::normal;
    return handle;
}

int spawn_rsse_tour_element(ScriptWorld& c, std::uint32_t handle) {
    auto* r = find(c, handle);
    if (!r) {
        return 0;
    }
    auto selected = std::ranges::find(r->entries, State::inactive, &RsseTourEntry::element_state);
    if (selected == r->entries.end()) {
        return 0;
    }
    auto& e = *selected;
    const auto prior = e;
    const auto prior_ready = r->docked_element_ready;
    const auto variation = random(c) % 100;
    e.positioned_sound_request = 0;
    e.owner_handle = handle;
    e.roll = e.pitch = e.vertical_velocity = e.heading_change = e.forward_speed = e.velocity_x = e.reserved_velocity_y = e.velocity_z = 0;
    e.element_state = State::seeking_approach;
    e.particle_transition_pending = 0;
    e.retire_when_empty = 0;
    e.signed_passenger_count = 0;
    e.particle_runtime_handle = 0;
    e.initialize_render_pose = 1;
    e.mesh_instance_handle = 0;
    e.propulsion_setting = fixed::add(fixed::word(r->base_propulsion_setting), variation);
    random_outside_target(c, e);
    e.current_x = e.target_x;
    e.current_y = e.target_y;
    e.current_z = e.target_z;
    e.flight_time_remaining_ticks = 0;
    e.previous_target_x = fixed::subtract(e.target_x, 20000);
    e.previous_target_y = e.target_y;
    e.previous_target_z = e.target_z;
    ++r->element_count;
    e.cached_animation_selector = e.cached_animation_mode = -1;
    e.cached_animation_value = std::numeric_limits<std::uint32_t>::max();
    try {
        const auto p = sound_position(e);
        const auto key = get_rsse_sound_script_slot(c, r->script_state_id, 0);
        if (key) {
            e.positioned_sound_request = submit_positioned_template_request(c, 0, c.resources.local_rides_catalog_slot, static_cast<std::uint32_t>(key), p[0], p[1], p[2]);
        }
        e.mesh_object_start = static_cast<std::uint32_t>(random(c));
        if (r->element_count == 1) {
            e.element_state = State::docked;
            e.current_x = r->reference_x;
            e.current_y = r->reference_y;
            e.current_z = r->reference_z;
            e.propulsion_setting = 0;
            e.heading = r->departure_heading;
            r->docked_element_ready = 1;
        }
        const auto* header = c.entities.at(r->owner_used_thing_id).payload;
        if (!header || header->kind != ThingKind::park_object) {
            throw std::logic_error("TOUR owner is not a park object");
        }
        const auto* object = reinterpret_cast<const ParkObjectRuntimeView*>(header);
        const auto* d = find_object_definition_by_id(&c.resources.object_catalog, object->definition_id);
        if (!d) {
            throw std::logic_error("TOUR owner definition");
        }
        e.mesh_instance_handle = world_operations::create_mesh_instance_and_register_slot(c, { std::bit_cast<std::int32_t>(d->additional_mesh_template_ids[0]), 257u, {}, 0, 0, nullptr, 0, 0, -1, 0 });
        if (!e.mesh_instance_handle) {
            throw std::runtime_error("TOUR element mesh creation failed");
        }
        animation(c, e, 0);
        e.mesh_object_count = 0;
        for (int node = 1; c.ports.meshes.find_embedded_node_resource_record(c, e.mesh_instance_handle, 128, node) != -1; ++node) {
            ++e.mesh_object_count;
        }
    } catch (...) {
        if (e.mesh_instance_handle) {
            world_operations::destroy_mesh(c, e.mesh_instance_handle);
        }
        if (e.positioned_sound_request) {
            c.ports.resources.request_target_recycle(c, e.positioned_sound_request);
        }
        --r->element_count;
        r->docked_element_ready = prior_ready;
        e = prior;
        throw;
    }
    return 1;
}

int board_rsse_tour_passenger(ScriptWorld& c, std::uint32_t handle, std::uint32_t person) {
    auto* r = find(c, handle);
    if (!r) {
        return 0;
    }
    auto found = std::ranges::find(r->entries, State::docked, &RsseTourEntry::element_state);
    if (found == r->entries.end()) {
        return 0;
    }
    auto& e = *found;
    if (e.signed_passenger_count < 0) {
        return 0;
    }
    //Native writes beyond its nine physical slots on malformed input. Bound the
    //host storage without retaining that memory-corruption behavior.
    if (static_cast<std::size_t>(e.signed_passenger_count) >= e.passenger_ids.size()) {
        return 0;
    }
    const auto count = fixed::add(e.signed_passenger_count, 1);
    if (e.mesh_object_count && e.mesh_instance_handle) {
        const auto node = fixed::add(fixed::add(fixed::word(e.mesh_object_start), count) % static_cast<int>(e.mesh_object_count), 1);
        const auto index = c.ports.meshes.find_embedded_node_resource_record(c, e.mesh_instance_handle, 128, node);
        if (index != -1) {
            world_operations::bind_world_person_sprite_to_mesh_model_object(c, e.mesh_instance_handle, index, person);
        }
    }
    e.passenger_ids[static_cast<std::size_t>(e.signed_passenger_count)] = person;
    e.signed_passenger_count = count;
    r->onboard_passenger_count = fixed::add(r->onboard_passenger_count, 1);
    return 1;
}

std::uint32_t unload_rsse_tour_passenger(ScriptWorld& c, std::uint32_t handle, bool force) {
    auto* r = find(c, handle);
    if (!r) {
        return 0;
    }
    auto selected = std::ranges::find_if(r->entries, [&](const auto& e) {
        return force ? e.signed_passenger_count > 0 : e.element_state == State::docked;
    });
    if (selected == r->entries.end() || (!force && selected->signed_passenger_count >= 0)) {
        return 0;
    }
    auto& e = *selected;
    const auto count = fixed::absolute(e.signed_passenger_count);
    if (count <= 0 || static_cast<std::size_t>(count) > e.passenger_ids.size()) {
        throw std::out_of_range("TOUR passenger storage");
    }
    release_passenger(c, e, count);
    e.signed_passenger_count = fixed::add(e.signed_passenger_count, force ? -1 : 1);
    r->onboard_passenger_count = fixed::subtract(r->onboard_passenger_count, 1);
    return e.passenger_ids[static_cast<std::size_t>(fixed::absolute(e.signed_passenger_count))];
}

void set_rsse_tour_enabled(ScriptWorld& c, std::uint32_t handle, bool enabled) {
    auto* r = find(c, handle);
    if (!r) {
        return;
    }
    if (enabled) {
        r->tour_enabled = 1;
        r->target_element_count = r->requested_element_count;
    } else if (r->active_effect_mode != Effect::fire) {
        r->target_element_count = 1;
        for (auto& e : r->entries) {
            if (active(e)) {
                e.flight_time_remaining_ticks = 0;
            }
            if (e.element_state == State::docked && e.signed_passenger_count > 0) {
                e.signed_passenger_count = fixed::negate(e.signed_passenger_count);
            }
        }
        r->tour_enabled = 0;
    }
}

void set_rsse_tour_element_count(ScriptWorld& c, std::uint32_t handle, std::uint32_t count) {
    if (auto* r = find(c, handle)) {
        r->requested_element_count = count;
        if (r->tour_enabled) {
            r->target_element_count = count;
        }
    }
}

int start_rsse_tour(ScriptWorld& c, std::uint32_t handle) {
    auto* r = find(c, handle);
    if (!r || (!r->tour_enabled && !r->onboard_passenger_count && fixed::word(r->element_count) <= 1) || !r->docked_element_ready) {
        return 0;
    }
    r->docked_element_ready = 0;
    return 1;
}

bool rsse_tour_busy(const ScriptQuery& c, std::uint32_t handle) {
    const auto* r = find(c, handle);
    return r && (r->onboard_passenger_count || !r->docked_element_ready || r->element_count != 1);
}

void set_rsse_tour_effect(ScriptWorld& c, std::uint32_t handle, Effect effect) {
    auto* r = find(c, handle);
    if (!r || r->active_effect_mode == effect) {
        return;
    }
    r->active_effect_mode = effect;
    auto& e = r->entries[0];
    const auto expire = [&] {
        if (e.particle_runtime_handle) {
            c.ports.effects.set_particle_effect_lifetime(c, e.particle_runtime_handle, -2);
        }
    };
    // clang-format off
  match(effect){
    case Effect::normal=>{
      r->target_element_count=r->requested_element_count;
      if(e.particle_transition_pending){animation(c,e,10);e.particle_transition_pending=0;const auto p=sound_position(e);(void)submit_positioned_template_request(c,0,c.resources.global_rides_catalog_slot,0xBBu,p[0],p[1],p[2]);}
      if(e.propulsion_setting)e.propulsion_setting=fixed::add(fixed::word(r->base_propulsion_setting),random(c)%100);
      expire();e.particle_runtime_handle=0;
    }
    case Effect::smoke=>{r->target_element_count=r->requested_element_count;expire();e.particle_runtime_handle=c.ports.effects.create_particle_effect(c,0x27u,{e.current_x,e.current_y,e.current_z},0);}
    case Effect::fire or Effect::fire_force_unload=>{
      e.particle_transition_pending=1;e.flight_time_remaining_ticks=0;expire();e.particle_runtime_handle=c.ports.effects.create_particle_effect(c,0x28u,{scale(e.current_x,1024,300),scale(e.current_y,1024,300),scale(e.current_z,1024,300)},0);
      if(effect==Effect::fire_force_unload){while(unload_rsse_tour_passenger(c,handle,true)){}r->target_element_count=1;}
    }
    case _=>{}
  }
    // clang-format on
}
} //namespace simcoaster
