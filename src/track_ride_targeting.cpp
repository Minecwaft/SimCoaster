#include "track_ride_internal.hpp"

namespace simcoaster::track_detail {
namespace {
constexpr auto mode_mask = bit(Flag::initial_first)
                         | bit(Flag::follow_peer)
                         | bit(Flag::follow_route)
                         | bit(Flag::random_node)
                         | bit(Flag::return_or_unload)
                         | bit(Flag::open_approach);
constexpr auto reset_mask = mode_mask | bit(Flag::rebuild_target) | bit(Flag::collision_contact);

bool free_family(const TrackRide& r) {
    return r.template_tag == -15
        || r.template_tag == -14
        || r.template_tag == -11
        || r.template_tag == -6
        || r.template_tag == -3
        || r.template_tag == -1;
}

bool parked_family(const TrackRide& r) {
    return r.template_tag == -10 || r.template_tag == -9 || r.template_tag == -7 || r.template_tag == -4;
}

bool water_family(const TrackRide& r) {
    return r.template_tag == -13 || r.template_tag == -12 || r.template_tag == -8 || r.template_tag == -5;
}

TrackRideLinkNode& transition_node(TrackRide& r) {
    if (!r.transition_node)
        throw std::logic_error("track ride has no transition node");
    return r.nodes.at(*r.transition_node);
}

void next_target(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s, TrackRideLinkNode& current) {
    auto& n = next_node(manager(c), current, true);
    s.selected_track_node_slot = n.pool_slot;
    s.target_offset_y = s.target_offset_x;
    const auto threshold = static_cast<std::uint32_t>(r.simulation_target_distance_threshold);
    if (!threshold)
        throw std::domain_error("track ride target-offset divisor");
    s.target_offset_x = fixed::word(static_cast<std::uint32_t>(r.simulation_target_distance_threshold / 2)
                                    - random_bits(c) % threshold);
    s.flags |= bit(Flag::rebuild_target);
}

void start_motion_sound(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s) {
    s.positioned_sound_request = sound(c, r, s, 0);
    if (s.positioned_sound_request)
        s.positioned_sound_request =
            c.ports.resources.update_positioned_request_position(c, s.positioned_sound_request, sound_position(s));
}
} //namespace

void setup_type(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s) {
    auto& m = manager(c);
    if (free_family(r)) {
        if (has(s, Flag::needs_type_setup)) {
            if (r.geometry.empty())
                throw std::logic_error("track ride spawn has no region");
            const auto radius = static_cast<std::uint32_t>(m.geometry.at(r.geometry.front()).extent_x);
            if (!radius)
                throw std::domain_error("track ride spawn radius divisor");
            for (unsigned attempt = 0; attempt < 100; ++attempt) {
                const auto distance = static_cast<std::int32_t>(random_bits(c) % radius),
                           angle = static_cast<std::int32_t>(random_bits(c) & 511u);
                s.position_x = fixed::add(r.simulation_spawn_x, mul(distance, cosine(angle)) / 256);
                s.position_y = fixed::add(r.simulation_spawn_y, mul(distance, sine(angle)) / -256);
                if (attempt == 99)
                    break;
                bool collision = false;
                for (const auto [owner, other] : simulations(m)) {
                    (void)owner;
                    if (other == &s)
                        continue;
                    if (magnitude(fixed::subtract(other->position_x, s.position_x),
                                  fixed::subtract(other->position_y, s.position_y))
                        < fixed::add(s.collision_radius, other->collision_radius)) {
                        collision = true;
                        break;
                    }
                }
                if (!collision)
                    break;
            }
            s.flags &= ~bit(Flag::drive_enabled);
            s.heading = s.steering_heading = static_cast<std::int32_t>(random_bits(c) & 511u);
            s.particle_model_node_index =
                c.ports.meshes.find_embedded_node_resource_record(c, s.mesh_instance_id, 256, 2);
            s.model_node_index = c.ports.meshes.find_embedded_node_resource_record(c, s.mesh_instance_id, 256, 1);
            if (r.template_tag == -1) {
                s.flags |= bit(Flag::four_point_bob) | bit(Flag::secondary_trailing);
                const auto* d = find_object_definition_by_id(&c.resources.object_catalog,
                                                             static_cast<std::uint16_t>(r.object_definition_id));
                if (!d)
                    throw std::logic_error("track ride secondary mesh definition");
                s.secondary_mesh_instance_id = world_operations::create_mesh_instance_and_register_slot(
                    c,
                    { std::bit_cast<std::int32_t>(d->additional_mesh_template_ids[0]),
                      257u,
                      {},
                      0,
                      0,
                      nullptr,
                      0,
                      0,
                      -1,
                      0 });
            }
            s.boundary_correction_count = -1;
        }
        if (!s.positioned_sound_request
            && (s.flags & 0x404000u) == 0x404000u
            && state(r) == TrackRideTransition::active
            && !has(s, Flag::return_or_unload))
            start_motion_sound(c, r, s);
        if (!(s.flags & (bit(Flag::follow_peer) | bit(Flag::collision_contact)))
            && (random_bits(c) & 15u) < 3u
            && active_count(r) > 1) {
            //The native temporary chain prepends candidates in pool order. Host
            //pointers remain local; its fourth saved pointer word is now ignored.
            std::vector<TrackRideSimulation*> candidates;
            for (auto [owner, other] : simulations(m))
                if (owner == &r
                    && other != &s
                    && !has(*other, Flag::return_or_unload)
                    && s.active_geometry
                    && other->active_geometry
                    && overlaps(m.geometry.at(*s.active_geometry), m.geometry.at(*other->active_geometry)))
                    candidates.push_back(other);
            if (!candidates.empty()) {
                const auto selected = static_cast<std::size_t>(random_bits(c) % candidates.size());
                auto* peer = candidates[candidates.size() - selected - 1];
                s.selected_peer_simulation_pool_index = peer->pool_slot;
                s.retarget_budget_remaining = 90;
                s.flags = (s.flags & ~(mode_mask | bit(Flag::collision_contact) | bit(Flag::needs_type_setup)))
                        | bit(Flag::follow_peer)
                        | bit(Flag::retarget_on_contact);
                return;
            }
        }
        TrackRideLinkNode* target{};
        std::uint32_t advance{};
        if (has(s, Flag::follow_route) && (random_bits(c) & 15u) < 4u) {
            target = node(m, s.selected_track_node_slot);
            advance = 1;
        } else {
            if (r.nodes.empty())
                throw std::logic_error("track ride has no targeting nodes");
            advance = random_bits(c) % static_cast<std::uint32_t>(r.nodes.size());
            target = &r.nodes.front();
        }
        if (!target)
            throw std::logic_error("track ride targeting references a missing node");
        s.flags = (s.flags & ~(reset_mask | bit(Flag::needs_type_setup)))
                | bit(Flag::follow_route)
                | bit(Flag::rebuild_target)
                | bit(Flag::retarget_on_contact);
        while (advance--)
            target = &next_node(m, *target);
        s.selected_track_node_slot = target->pool_slot;
        s.retarget_budget_remaining = 3;
        return;
    }
    if (parked_family(r)) {
        if ((r.template_tag == -10 || r.template_tag == -9) && has(s, Flag::needs_type_setup))
            s.flags |= 0x3C00u;
        s.flags &= ~(mode_mask | bit(Flag::collision_contact));
        s.retarget_budget_remaining = 0;
        if (has(s, Flag::needs_type_setup)) {
            start_motion_sound(c, r, s);
            s.particle_model_node_index =
                c.ports.meshes.find_embedded_node_resource_record(c, s.mesh_instance_id, 256, 1);
            if (has(s, Flag::secondary_surface)) {
                const auto* d = find_object_definition_by_id(&c.resources.object_catalog,
                                                             static_cast<std::uint16_t>(r.object_definition_id));
                if (!d)
                    throw std::logic_error("track ride surface mesh definition");
                s.secondary_mesh_instance_id = world_operations::create_mesh_instance_and_register_slot(
                    c,
                    { std::bit_cast<std::int32_t>(d->additional_mesh_template_ids[0]),
                      257u,
                      {},
                      0,
                      0,
                      nullptr,
                      0,
                      0,
                      -1,
                      0 });
            }
        }
        if (state(r) == TrackRideTransition::pending_open) {
            auto remaining = active_count(r);
            TrackRideLinkNode* target = r.nodes.empty() ? nullptr : &r.nodes.front();
            while (remaining) {
                if (!target || !(target->flags & 2u))
                    throw std::logic_error("track ride parking node contract");
                if (!--remaining)
                    break;
                target = &next_node(m, *target);
            }
            if (!target)
                throw std::logic_error("track ride parking nodes are empty");
            s.selected_track_node_slot = target->pool_slot;
            s.flags = (s.flags | bit(Flag::follow_route) | bit(Flag::open_approach) | bit(Flag::rebuild_target))
                    & ~bit(Flag::needs_type_setup);
            return;
        }
        if (state(r) == TrackRideTransition::active) {
            s.flags |= bit(Flag::follow_route) | bit(Flag::rebuild_target);
            if (s.selected_track_node_slot < 0 || static_cast<std::size_t>(s.selected_track_node_slot) >= node_count(m))
                s.selected_track_node_slot = transition_node(r).pool_slot;
            s.retarget_budget_remaining = 0;
            s.thrust_variation = div(r.current_values[0], 9) * (3 - static_cast<std::int32_t>(random_bits(c) & 7u));
        }
        s.flags &= ~bit(Flag::needs_type_setup);
        return;
    }
    if (water_family(r)) {
        s.retarget_budget_remaining = 0;
        s.flags &= ~(mode_mask | bit(Flag::collision_contact));
        if (has(s, Flag::needs_type_setup)) {
            start_motion_sound(c, r, s);
            s.selected_track_node_slot = transition_node(r).pool_slot;
        }
        s.flags |= bit(Flag::follow_route) | bit(Flag::rebuild_target);
        if (s.selected_track_node_slot < 0 || static_cast<std::size_t>(s.selected_track_node_slot) >= node_count(m))
            s.selected_track_node_slot = transition_node(r).pool_slot;
        s.retarget_budget_remaining = 0;
        s.thrust_variation = 0;
        s.flags &= ~bit(Flag::needs_type_setup);
        return;
    }
    if (r.template_tag == -2) {
        auto* current = has(s, Flag::follow_route) ? node(m, s.selected_track_node_slot)
                      : r.nodes.empty()            ? nullptr
                                                   : &r.nodes.front();
        if (!current)
            throw std::logic_error("track ride looping node");
        s.flags = (s.flags & ~(reset_mask | bit(Flag::needs_type_setup)))
                | bit(Flag::follow_route)
                | bit(Flag::rebuild_target);
        s.selected_track_node_slot = next_node(m, *current).pool_slot;
        s.retarget_budget_remaining = 0;
        return;
    }
    s.flags = (s.flags & ~(mode_mask | bit(Flag::rebuild_target) | bit(Flag::needs_type_setup)))
            | bit(Flag::random_node)
            | bit(Flag::rebuild_target);
    const auto count = node_count(m);
    if (!count)
        throw std::domain_error("track ride global node divisor");
    s.selected_track_node_slot = static_cast<std::int32_t>(random_bits(c) % count);
}

void step_target(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s) {
    auto& m = manager(c);
    s.simulation_tick_count = fixed::add(s.simulation_tick_count, 1);
    if (s.close_budget_remaining >= 0 && state(r) == TrackRideTransition::active && r.fault != TrackRideFault::broken) {
        s.close_budget_remaining = fixed::subtract(s.close_budget_remaining, 1);
        if (!s.close_budget_remaining) {
            if (free_family(r))
                s.flags &= ~bit(Flag::drive_enabled);
            s.flags = (s.flags & ~(mode_mask | bit(Flag::rebuild_target)))
                    | bit(Flag::return_or_unload)
                    | bit(Flag::rebuild_target);
        }
    }
    if (has(s, Flag::follow_peer)) {
        s.retarget_budget_remaining = fixed::subtract(s.retarget_budget_remaining, 1);
        auto* peer = simulation(m, s.selected_peer_simulation_pool_index);
        if (s.retarget_budget_remaining <= 0
            || !peer
            || (peer->flags & (bit(Flag::active) | bit(Flag::return_or_unload) | bit(Flag::collision_contact)))
                   != bit(Flag::active)) {
            setup_type(c, r, s);
            return;
        }
        s.flags |= bit(Flag::rebuild_target);
        const auto speed = std::max(magnitude(s.velocity_x, s.velocity_y), 1);
        const auto horizon = std::min(div(magnitude(fixed::subtract(s.position_x, peer->position_x),
                                                    fixed::subtract(s.position_y, peer->position_y)),
                                          speed),
                                      24);
        auto x = fixed::add(peer->position_x, mul(horizon, peer->velocity_x)),
             y = fixed::add(peer->position_y, mul(horizon, peer->velocity_y));
        if (!peer->active_geometry)
            throw std::logic_error("track ride peer has no region");
        if (!contains(m.geometry.at(*peer->active_geometry), x, y) && !find_region(m, x, y)) {
            const auto nearest = nearest_region(m, *peer->active_geometry, x, y, true);
            if (!nearest)
                throw std::logic_error("track ride peer prediction has no region");
            const auto& region = m.geometry.at(*nearest);
            const auto dx = fixed::subtract(x, region.origin_x) / 8, dy = fixed::subtract(y, region.origin_y) / 8,
                       denominator = fixed::add(magnitude(dx, dy), 32);
            x = fixed::add(region.origin_x, div(mul(dx, region.extent_x), denominator));
            y = fixed::add(region.origin_y, div(mul(dy, region.extent_x), denominator));
        }
        const auto distance = steer(c, r, s, x, y);
        if (has(s, Flag::cross_region_waypoint) && distance <= r.simulation_target_distance_threshold)
            s.flags &= ~bit(Flag::cross_region_waypoint);
        return;
    }
    if (has(s, Flag::return_or_unload)) {
        if (free_family(r)) {
            if (!release_riders(c, r, s, true)) {
                state(r, TrackRideTransition::pending_open);
                s.flags &= ~mode_mask;
                if (s.mesh_animation_sequence_id != 12)
                    animate(c, s, 12);
            }
            if (s.positioned_sound_request)
                c.ports.resources.submit_resource_target_parameter_request(c, s.positioned_sound_request);
        } else {
            const auto distance = steer(c, r, s, r.simulation_target_x, r.simulation_target_y);
            if (has(s, Flag::cross_region_waypoint) && distance <= r.simulation_target_distance_threshold)
                s.flags = (s.flags & ~bit(Flag::cross_region_waypoint)) | bit(Flag::rebuild_target);
            else if (distance < r.simulation_target_distance_threshold)
                release_simulation(c, r, s);
        }
        return;
    }
    if (!(s.flags & (bit(Flag::follow_route) | bit(Flag::random_node) | bit(Flag::open_approach))))
        return;
    auto* target = node(m, s.selected_track_node_slot);
    if (!target || !(target->flags & 1u))
        return;
    const auto distance = steer(c, r, s, target->x, target->y);
    if (has(s, Flag::cross_region_waypoint) && distance <= r.simulation_target_distance_threshold) {
        s.flags = (s.flags & ~bit(Flag::cross_region_waypoint)) | bit(Flag::rebuild_target);
        return;
    }
    if ((s.flags & (bit(Flag::retarget_on_contact) | bit(Flag::collision_contact)))
        == (bit(Flag::retarget_on_contact) | bit(Flag::collision_contact))) {
        setup_type(c, r, s);
        s.flags |= bit(Flag::rebuild_target);
        return;
    }
    if (distance > r.simulation_target_distance_threshold)
        return;
    s.retarget_budget_remaining = fixed::subtract(s.retarget_budget_remaining, 1);
    if (!s.retarget_budget_remaining) {
        setup_type(c, r, s);
        s.flags |= bit(Flag::rebuild_target);
        return;
    }
    if (has(s, Flag::follow_route)) {
        if (has(s, Flag::open_approach)) {
            if (distance <= r.simulation_target_distance_threshold / 4) {
                s.flags |= bit(Flag::approach_damping) | bit(Flag::rebuild_target);
                s.selected_track_node_slot = transition_node(r).pool_slot;
                return;
            }
            s.flags |= bit(Flag::rebuild_target);
            return;
        }
        if (target->flags & 4u) {
            bool keep_running =
                state(r) != TrackRideTransition::pending_close && state(r) != TrackRideTransition::inactive;
            if (keep_running) {
                s.close_budget_remaining = fixed::add(s.close_budget_remaining, 1);
                keep_running = s.close_budget_remaining != 0;
            }
            if (keep_running)
                setup_type(c, r, s);
            else {
                if (parked_family(r) && state(r) == TrackRideTransition::active)
                    state(r, TrackRideTransition::pending_close);
                s.target_offset_x = s.target_offset_y = 0;
                s.flags = (s.flags & ~(mode_mask & ~bit(Flag::return_or_unload)))
                        | bit(Flag::return_or_unload)
                        | bit(Flag::rebuild_target);
            }
        }
        if (target->geometry) {
            if (!contains(m.geometry.at(*target->geometry), s.position_x, s.position_y)) {
                s.target_offset_x = s.target_offset_y = 0;
                s.flags |= bit(Flag::rebuild_target);
                return;
            }
            s.active_geometry = target->geometry;
        }
        next_target(c, r, s, *target);
        return;
    }
    if (has(s, Flag::random_node)) {
        const auto count = node_count(m);
        if (!count)
            throw std::domain_error("track ride target node divisor");
        s.selected_track_node_slot = static_cast<std::int32_t>(random_bits(c) % count);
    }
    s.flags |= bit(Flag::rebuild_target);
}
} //namespace simcoaster::track_detail
