#include "track_ride_internal.hpp"

namespace simcoaster {
namespace track_detail {
void animate(WorldRuntime& c, TrackRideSimulation& s, std::uint32_t animation, bool loop, bool force) {
    s.mesh_animation_sequence_id = animation;
    if (!s.mesh_instance_id) {
        return;
    }
    world_operations::clear_deferred_mesh_animation(c, s.mesh_instance_id, 0);
    const auto flags = (loop ? 1u : 0u) | (force ? 2u : 0u);
    (void)world_operations::request_mesh_animation_control_transition(c, s.mesh_instance_id, static_cast<MeshAnimationRequest>(animation), 0, static_cast<MeshAnimationTransitionFlags>(flags), 1.f, 0);
}

void bind_riders(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s, bool effects) {
    int seat = 0;
    for (auto& rider : s.riders) {
        rider.seat_slot = ++seat;
        rider.model_object_index = c.ports.meshes.find_embedded_node_resource_record(c, s.mesh_instance_id, 128, seat);
        if (rider.model_object_index != -1) {
            world_operations::bind_world_person_sprite_to_mesh_model_object(c, s.mesh_instance_id, rider.model_object_index, rider.person_id);
            if (effects) {
                (void)particle(c, Particle::beam_person, root_position(c, s));
            }
        }
        ++r.attached_rider_count;
    }
}

std::uint32_t release_riders(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s, bool effects) {
    for (auto& rider : s.riders) {
        --r.attached_rider_count;
        if (rider.model_object_index != -1) {
            if (!has(s, Flag::render_hidden)) {
                world_operations::release_mesh_model_object_instance(c, s.mesh_instance_id, rider.model_object_index);
            }
            rider.model_object_index = -1;
            if (effects) {
                (void)particle(c, Particle::beam_person, root_position(c, s));
            }
        }
        r.completed_riders.insert(r.completed_riders.begin(), rider);
    }
    s.riders.clear();
    return r.attached_rider_count;
}

void release_simulation(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s) {
    if (!has(s, Flag::active)) {
        return;
    }
    (void)release_riders(c, r, s, false);
    if (has(s, Flag::primary)) {
        r.first_simulation.reset();
        s.flags &= ~bit(Flag::primary);
    }
    const bool resetting = state(r) == TrackRideTransition::reset_for_load;
    if (!resetting) {
        (void)particle(c, Particle::beam_car, root_position(c, s));
    }
    world_operations::destroy_mesh(c, s.mesh_instance_id);
    if (has(s, Flag::secondary_surface) || has(s, Flag::secondary_trailing)) {
        world_operations::destroy_mesh(c, s.secondary_mesh_instance_id);
        s.flags &= ~(bit(Flag::secondary_surface) | bit(Flag::secondary_trailing));
    }
    if (s.positioned_sound_request && !resetting) {
        if (r.template_tag == -10 || r.template_tag == -9 || r.template_tag == -7 || r.template_tag == -4) {
            s.positioned_sound_request = sound(c, r, s, 1, s.positioned_sound_request);
            if (s.positioned_sound_request) {
                c.ports.resources.update_positioned_request_position(c, s.positioned_sound_request, sound_position(s));
            }
            s.positioned_sound_request = 0;
        } else {
            c.ports.resources.submit_resource_target_parameter_request(c, s.positioned_sound_request);
        }
    }
    if (s.particle_runtime_handle != std::numeric_limits<std::uint32_t>::max()) {
        c.ports.effects.set_particle_effect_lifetime(c, s.particle_runtime_handle, -2);
        s.particle_runtime_handle = std::numeric_limits<std::uint32_t>::max();
    }
    s.flags &= ~bit(Flag::active);
    if (!active_count(r) && r.template_tag != -13 && r.template_tag != -12 && r.template_tag != -8 && r.template_tag != -5) {
        state(r, TrackRideTransition::pending_open);
    }
}
} //namespace track_detail

using namespace track_detail;

void update_track_ride_tuning(TrackRide& r, std::int32_t percentage) {
    r.interpolation_percent = std::clamp(percentage, 0, 100);
    for (std::size_t i = 0; i < 4; ++i) {
        r.current_values[i] = fixed::add(r.start_values[i], div(mul(r.interpolation_percent, fixed::subtract(r.end_values[i], r.start_values[i])), 100));
    }
}

void begin_track_ride_open(WorldRuntime& c, std::uint32_t handle) {
    auto* r = manager(c).find(handle);
    if (!r) {
        return;
    }
    const bool direct = r->template_tag == -13 || r->template_tag == -12 || r->template_tag == -8 || r->template_tag == -5;
    const bool queued = r->template_tag == -10 || r->template_tag == -9 || r->template_tag == -7 || r->template_tag == -4;
    if (direct || queued) {
        if (state(*r) != TrackRideTransition::inactive) {
            return;
        }
        auto boundary = std::ranges::find_if(r->nodes, [](const auto& n) {
            return (n.flags & 4u) != 0;
        });
        if (boundary == r->nodes.end()) {
            throw std::logic_error("track ride has no cycle boundary");
        }
        auto& target = next_node(manager(c), *boundary, true);
        r->transition_node = static_cast<std::size_t>(&target - r->nodes.data());
    }
    state(*r, direct ? TrackRideTransition::active : TrackRideTransition::pending_open);
    update_track_ride_tuning(*r, r->interpolation_percent);
}

void close_track_ride(WorldRuntime& c, std::uint32_t handle, bool release_vehicles) {
    auto& m = manager(c);
    auto* r = m.find(handle);
    if (!r) {
        return;
    }
    if (release_vehicles) {
        for (auto [owner, s] : simulations(m)) {
            if (owner == r) {
                release_simulation(c, *r, *s);
            }
        }
    }
    if (state(*r) == TrackRideTransition::inactive) {
        return;
    }
    state(*r, TrackRideTransition::inactive);
    r->completed_riders.insert(r->completed_riders.end(), r->queued_riders.begin(), r->queued_riders.end());
    r->queued_riders.clear();
    if (r->template_tag == -15 || r->template_tag == -14 || r->template_tag == -11 || r->template_tag == -6 || r->template_tag == -3 || r->template_tag == -1) {
        for (auto& s : r->simulations) {
            if (has(s, Flag::active)) {
                s.close_budget_remaining = 0;
                s.flags = (s.flags & ~(bit(Flag::initial_first) | bit(Flag::follow_peer) | bit(Flag::follow_route) | bit(Flag::random_node) | bit(Flag::return_or_unload) | bit(Flag::open_approach) | bit(Flag::drive_enabled) | bit(Flag::rebuild_target))) | 0x10020u;
            }
        }
    }
}

void start_track_ride_simulations(WorldRuntime& c, std::uint32_t handle) {
    auto* r = manager(c).find(handle);
    if (!r) {
        return;
    }
    if (r->template_tag == -13 || r->template_tag == -12 || r->template_tag == -8 || r->template_tag == -5 || r->template_tag == -2) {
        return;
    }
    if (state(*r) != TrackRideTransition::pending_open) {
        return;
    }
    state(*r, TrackRideTransition::active);
    for (auto& s : r->simulations) {
        if (has(s, Flag::active)) {
            s.flags &= 0xFFF7FFBFu;
            s.close_budget_remaining = r->initial_close_budget;
            setup_type(c, *r, s);
            if (r->template_tag == -1 && has(s, Flag::drive_enabled)) {
                animate(c, s, 5, true, false);
            }
        }
    }
}

void set_track_ride_fault(WorldRuntime& c, std::uint32_t handle, TrackRideFault fault) {
    auto* r = manager(c).find(handle);
    if (!r) {
        return;
    }
    const auto previous = r->fault;
    r->fault = fault;
    if (r->template_tag < -15 || (r->template_tag > -3 && r->template_tag != -1)) {
        return;
    }
    for (auto& s : r->simulations) {
        if (has(s, Flag::active)) {
            if (fault == TrackRideFault::broken && has(s, Flag::drive_enabled) && s.particle_model_node_index != -1 && s.particle_runtime_handle == std::numeric_limits<std::uint32_t>::max()) {
                const auto p = world_operations::get_mesh_clone_attachment_position(c, s.mesh_instance_id, s.particle_model_node_index).value_or(std::array<float, 3> {});
                s.particle_runtime_handle = particle(c, Particle::smoke, p);
                if (r->template_tag == -1) {
                    animate(c, s, 12);
                }
            } else if (fault == TrackRideFault::none && previous == TrackRideFault::broken && s.particle_runtime_handle != std::numeric_limits<std::uint32_t>::max()) {
                c.ports.effects.set_particle_effect_lifetime(c, s.particle_runtime_handle, -2);
                s.particle_runtime_handle = std::numeric_limits<std::uint32_t>::max();
                if (r->template_tag == -1 && has(s, Flag::drive_enabled)) {
                    animate(c, s, 5, true, false);
                }
            }
        }
    }
}

void set_track_ride_close_budget(WorldRuntime& c, std::uint32_t handle, std::int32_t budget) {
    if (auto* r = manager(c).find(handle)) {
        r->initial_close_budget = budget;
    }
}

namespace {
std::size_t rider_count(const TrackRideManager& m) {
    std::size_t count = 0;
    for (const auto& r : m.rides) {
        if (r) {
            count += r->queued_riders.size() + r->completed_riders.size();
            for (const auto& s : r->simulations) {
                if (has(s, Flag::active)) {
                    count += s.riders.size();
                }
            }
        }
    }
    return count;
}
} //namespace

int enqueue_track_ride_rider(WorldRuntime& c, std::uint32_t handle, std::uint32_t person) {
    auto& m = manager(c);
    auto* r = m.find(handle);
    if (!r || state(*r) == TrackRideTransition::inactive || rider_count(m) >= 1024) {
        return 0;
    }
    r->queued_riders.insert(r->queued_riders.begin(), TrackRideRider { person });
    return 1;
}

std::uint32_t dequeue_track_ride_rider(WorldRuntime& c, std::uint32_t handle) {
    auto* r = manager(c).find(handle);
    if (!r || r->completed_riders.empty()) {
        return 0;
    }
    const auto id = r->completed_riders.front().person_id;
    r->completed_riders.erase(r->completed_riders.begin());
    return id;
}

TrackRideSimulation* create_track_ride_simulation(WorldRuntime& c, std::uint32_t handle, std::uint32_t flags) {
    auto& m = manager(c);
    auto* r = m.find(handle);
    if (!r || state(*r) == TrackRideTransition::inactive) {
        return nullptr;
    }
    const auto count = active_count(*r);
    if (count >= r->simulation_record_limit || count >= 64) {
        return nullptr;
    }
    const auto available = first_free_track_ride_simulation_slot(m);
    if (!available) {
        return nullptr;
    }
    //Reserve before taking the queue: a host allocation failure must leave its
    //riders and the manager's live identities owned by their previous records.
    r->simulations.reserve(r->simulations.size() + 1);
    const auto previous_primary = r->first_simulation;
    const auto previous_attached = r->attached_rider_count;
    //Keep inactive vector entries stable: another owner's new row can reuse the
    //same original pool identity without leaving a second allocated identity.
    for (auto& prior : m.rides) {
        if (prior) {
            for (auto& old : prior->simulations) {
                if (!has(old, Flag::active) && old.pool_slot == *available) {
                    old.pool_slot = -1;
                }
            }
        }
    }
    TrackRideSimulation s {};
    s.pool_slot = *available;
    s.position_x = r->simulation_spawn_x;
    s.position_y = r->simulation_spawn_y;
    s.collision_radius = r->collision_radius;
    s.selected_peer_simulation_pool_index = s.selected_track_node_slot = -1;
    s.particle_runtime_handle = std::numeric_limits<std::uint32_t>::max();
    s.model_node_index = s.particle_model_node_index = -1;
    s.flags = bit(Flag::active) | bit(Flag::drive_enabled) | (count ? 0u : bit(Flag::initial_first));
    s.close_budget_remaining = r->initial_close_budget;
    s.active_geometry = find_region(m, s.position_x, s.position_y);
    s.riders = std::move(r->queued_riders);
    r->queued_riders.clear();
    r->simulations.push_back(std::move(s));
    auto& row = r->simulations.back();
    if (flags) {
        row.selected_peer_simulation_pool_index = row.selected_track_node_slot = 0;
        row.flags |= flags;
        return &row;
    }
    try {
        if (!r->mesh_template_variant_count) {
            throw std::domain_error("track ride has no mesh variants");
        }
        row.flags |= bit(Flag::needs_type_setup);
        row.mesh_template_index = static_cast<std::uint32_t>(fixed::add(r->first_mesh_template_index, static_cast<int>(count + 1) % r->mesh_template_variant_count));
        const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, static_cast<std::uint16_t>(r->object_definition_id));
        if (!definition) {
            throw std::logic_error("track ride object definition");
        }
        row.mesh_instance_id = world_operations::create_mesh_instance_and_register_slot(c, { std::bit_cast<std::int32_t>(definition->additional_mesh_template_ids.at(row.mesh_template_index)), 257u, {}, 0, 0, nullptr, 0, 0, -1, 0 });
        if (!row.mesh_instance_id) {
            throw std::runtime_error("track ride mesh creation failed");
        }
        row.mesh_animation_sequence_id = 12;
        (void)world_operations::request_mesh_animation_control_transition(c, row.mesh_instance_id, MeshAnimationRequest::clear, 0, MeshAnimationTransitionFlags::force, 1.f, 0);
        if (!r->first_simulation) {
            r->first_simulation = r->simulations.size() - 1;
            row.flags |= bit(Flag::primary);
        }
        bind_riders(c, *r, row, false);
        setup_type(c, *r, row);
        row.flags |= bit(Flag::spawn_effect_pending);
        return &row;
    } catch (...) {
        if (row.secondary_mesh_instance_id) {
            world_operations::destroy_mesh(c, row.secondary_mesh_instance_id);
        }
        if (row.mesh_instance_id) {
            world_operations::destroy_mesh(c, row.mesh_instance_id);
        }
        for (auto& rider : row.riders) {
            rider.model_object_index = -1;
            rider.seat_slot = 0;
        }
        r->queued_riders = std::move(row.riders);
        r->simulations.pop_back();
        r->first_simulation = previous_primary;
        r->attached_rider_count = previous_attached;
        throw;
    }
}

int attach_queued_track_ride_riders(WorldRuntime& c, std::uint32_t handle) {
    auto& m = manager(c);
    auto* r = m.find(handle);
    if (!r || state(*r) == TrackRideTransition::inactive) {
        return 0;
    }
    for (auto [owner, s] : simulations(m)) {
        if (owner == r && s->riders.empty()) {
            s->riders = std::move(r->queued_riders);
            r->queued_riders.clear();
            s->flags |= 0x84000u;
            s->close_budget_remaining = r->initial_close_budget;
            bind_riders(c, *r, *s, true);
            setup_type(c, *r, *s);
            return 1;
        }
    }
    return 0;
}

int remove_track_ride_simulation(WorldRuntime& c, std::uint32_t handle, bool prefer_empty) {
    auto& m = manager(c);
    auto* r = m.find(handle);
    if (!r) {
        return 0;
    }
    for (auto [owner, s] : simulations(m)) {
        if (owner == r && (!prefer_empty || s->riders.empty())) {
            release_simulation(c, *r, *s);
            return 1;
        }
    }
    //Native fallback removes an occupied vehicle but returns zero to its caller.
    if (prefer_empty) {
        (void)remove_track_ride_simulation(c, handle, false);
    }
    return 0;
}
} //namespace simcoaster
