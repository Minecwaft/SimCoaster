#include "track_ride_internal.hpp"

namespace simcoaster::track_detail {
namespace {
void directed(WorldRuntime& c, const TrackRideSimulation& s, int attachment, Particle type) {
    const auto p = world_operations::get_mesh_clone_attachment_position(c, s.mesh_instance_id, attachment)
                       .value_or(std::array<float, 3>{});
    auto direction = world_operations::get_mesh_clone_attachment_forward(c, s.mesh_instance_id, attachment);
    const auto length = std::sqrt(double(direction[0]) * direction[0]
                                  + double(direction[1]) * direction[1]
                                  + double(direction[2]) * direction[2]);
    if (length == 0)
        direction = { 0, 1, 0 };
    else
        for (auto& axis : direction)
            axis = static_cast<float>(axis / length);
    (void)c.ports.effects.create_particle_effect_with_direction(c,
                                                                std::to_underlying(type),
                                                                particle_position(p),
                                                                particle_position(direction));
}

void update_sound_and_smoke(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s) {
    if (s.positioned_sound_request) {
        s.positioned_sound_request =
            c.ports.resources.update_positioned_request_position(c, s.positioned_sound_request, sound_position(s));
        const auto* object = park_object(c, r);
        s.positioned_sound_request =
            dispatch_resource_manager_operation(c,
                                                s.positioned_sound_request,
                                                get_rsse_sound_script_slot(c, object ? object->script_id : 0, 9),
                                                s.speed_magnitude / 3);
    }
    if (s.particle_runtime_handle != std::numeric_limits<std::uint32_t>::max() && s.particle_model_node_index != -1) {
        const auto p =
            world_operations::get_mesh_clone_attachment_position(c, s.mesh_instance_id, s.particle_model_node_index)
                .value_or(std::array<float, 3>{});
        c.ports.effects.set_particle_effect_position(c, s.particle_runtime_handle, particle_position(p));
    }
}

void turn(TrackRideSimulation& s, bool change) {
    auto delta = (fixed::subtract(s.desired_heading, s.heading)) & 511;
    if (delta > 256)
        delta -= 512;
    const auto previous = s.angular_velocity;
    s.angular_velocity = scale(delta, s.speed_magnitude, 1000);
    if (change)
        s.angular_velocity_change = fixed::subtract(s.angular_velocity, previous);
}

void tunnel(WorldRuntime& c, TrackRideSimulation& s) {
    if (!s.active_geometry)
        return;
    auto& m = manager(c);
    const auto index = *s.active_geometry;
    const auto& region = m.geometry.at(index);
    if (!(region.flags & 0x100u)) {
        if (has(s, Flag::render_hidden))
            hidden(c, s, false);
        return;
    }
    TrackRideAuxiliary* part = nullptr;
    for (auto& ride : m.rides)
        if (ride)
            for (auto& candidate : ride->auxiliary)
                if (std::ranges::find(candidate.geometry, index) != candidate.geometry.end()) {
                    part = &candidate;
                    break;
                }
    if (!part || part->definition_metadata_word_10 != 3500)
        return;
    const auto displacement = part->flags == 12 ? fixed::subtract(s.position_x, region.origin_x)
                                                : fixed::subtract(s.position_y, region.origin_y);
    if (part->track_cell_ordinal) {
        if (has(s, Flag::render_hidden) && ((s.flags & 0x800000u) ? displacement > 1200 : displacement < -1200))
            hidden(c, s, false);
    } else if (!has(s, Flag::render_hidden) && displacement < 1200 && displacement > -1200) {
        if (displacement < 0)
            s.flags |= 0x800000u;
        else
            s.flags &= ~0x800000u;
        hidden(c, s, true);
        auto* instance = mesh(c, part->mesh_instance_id);
        if (instance && instance->playback.controls.at(0).clip.category != 5) {
            world_operations::clear_deferred_mesh_animation(c, part->mesh_instance_id, 0);
            (void)world_operations::request_mesh_animation_control_transition(
                c,
                part->mesh_instance_id,
                static_cast<MeshAnimationRequest>(5),
                0,
                static_cast<MeshAnimationTransitionFlags>(0),
                1,
                0);
            part->runtime_flags |= 2u;
        }
    }
    if (part->runtime_flags & 2u)
        if (auto* instance = mesh(c, part->mesh_instance_id);
            instance && instance->playback.controls.at(0).deferred.clip.category == 12) {
            (void)world_operations::request_mesh_animation_control_transition(
                c,
                part->mesh_instance_id,
                static_cast<MeshAnimationRequest>(2),
                0,
                static_cast<MeshAnimationTransitionFlags>(1),
                1,
                0);
            part->runtime_flags &= ~2u;
        }
}
} //namespace

void hidden(WorldRuntime& c, TrackRideSimulation& s, bool value) {
    if (has(s, Flag::render_hidden) == value)
        return;
    if (!c.resources.meshes)
        throw std::logic_error("track ride visibility needs mesh owner");
    c.resources.meshes->set_registered(c, s.mesh_instance_id, !value);
    if (has(s, Flag::secondary_surface) || has(s, Flag::secondary_trailing))
        if (s.secondary_mesh_instance_id)
            c.resources.meshes->set_registered(c, s.secondary_mesh_instance_id, !value);
    //Native retains the attachment index while hidden, but passes a null person
    //when showing it again. Retain the actual owner explicitly on the host;
    //hidden riders own no head resource and cannot release it a second time.
    for (const auto& rider : s.riders)
        if (rider.model_object_index != -1) {
            if (value)
                world_operations::release_mesh_model_object_instance(c, s.mesh_instance_id, rider.model_object_index);
            else
                world_operations::bind_world_person_sprite_to_mesh_model_object(c,
                                                                                s.mesh_instance_id,
                                                                                rider.model_object_index,
                                                                                rider.person_id);
        }
    if (value)
        s.flags |= bit(Flag::render_hidden);
    else
        s.flags &= ~bit(Flag::render_hidden);
}

void step_motion(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s) {
    auto motion_scale = r.current_values[1];
    if (has(s, Flag::approach_damping))
        motion_scale = static_cast<std::int32_t>(std::int64_t{ motion_scale } * 7 / 10);
    else if (r.fault == TrackRideFault::broken)
        motion_scale = 1000;
    s.velocity_x = s.velocity_x_secondary = scale(s.velocity_x, motion_scale);
    s.velocity_y = s.velocity_y_secondary = scale(s.velocity_y, motion_scale);
    const auto square = fixed::add(mul(s.velocity_x, s.velocity_x), mul(s.velocity_y, s.velocity_y));
    s.speed_magnitude = fixed::truncate_integer(std::sqrt(static_cast<double>(square)));
    s.position_x = fixed::add(s.position_x, s.velocity_x);
    s.position_y = fixed::add(s.position_y, s.velocity_y);
    const bool running = state(r) == TrackRideTransition::active,
               periodic = (manager(c).simulation_step_counter & 31u) == 0;
    // clang-format off
  match(r.template_tag) {
    case -15 or -14 or -11 or -6 or -3 or -1 => {
      if(running){s.desired_heading=r.template_tag==-1?s.steering_heading:heading(s.velocity_x,s.velocity_y);turn(s,false);}else s.angular_velocity=0;
      if(running && has(s,Flag::drive_enabled) && periodic && s.model_node_index!=-1){const auto draw=private_random(c);if((draw&3u)==2 && r.template_tag!=-1 && !s.pending_effect_code){directed(c,s,s.model_node_index,Particle::sparks);s.pending_effect_code=4;}}
      update_sound_and_smoke(c,r,s);
    }
    case -13 or -12 or -8 or -5 => {
      if(std::abs(std::int64_t{s.desired_heading})>48)s.desired_heading=fixed::truncate_integer(static_cast<double>(s.desired_heading)*99.800003*0.0099999998);
      s.angular_velocity=std::clamp(s.desired_heading/128,-16,16);
      if(running && periodic && (private_random(c)&7u)==2)(void)particle(c,Particle::small_splash,root_position(c,s));
      update_sound_and_smoke(c,r,s);
    }
    case -10 or -9 or -7 or -4 => {
      if(running && periodic && s.speed_magnitude>230 && !s.pending_effect_code)s.pending_effect_code=2;
      if(s.speed_magnitude<=40 && s.mesh_animation_sequence_id==5 && !(s.flags&0x400u))animate(c,s,12);
      if(s.speed_magnitude>40 && s.mesh_animation_sequence_id==12)animate(c,s,5,true,false);
      if(running && r.fault==TrackRideFault::worn && periodic && s.speed_magnitude>80 && s.particle_model_node_index!=-1){const auto draw=private_random(c)&7u;if(draw<=1)directed(c,s,s.particle_model_node_index,Particle::sparks);else if(draw==2 && !s.pending_effect_code){directed(c,s,s.particle_model_node_index,Particle::exhaust);s.pending_effect_code=1;}}
      update_sound_and_smoke(c,r,s);s.desired_heading=heading(s.velocity_x,s.velocity_y);turn(s,true);
    }
    case _ => {s.desired_heading=heading(s.velocity_x,s.velocity_y);turn(s,true);}
  }
    // clang-format on
    s.heading = fixed::add(s.heading, s.angular_velocity) & 511;
    tunnel(c, s);
}

void process_effects(WorldRuntime& c, TrackRide& r, TrackRideSimulation& s) {
    if (s.pending_effect_code) {
        s.queued_effect_code = std::max(s.pending_effect_code, s.queued_effect_code);
        s.pending_effect_code = 0;
    }
    const auto p = sound_position(s);
    // clang-format off
  match(s.queued_effect_code) {
    case 1 => {(void)submit_positioned_template_request(c,0,c.resources.global_rides_catalog_slot,0x18u,p[0],p[1],p[2]);s.queued_effect_code=0;}
    case 2 => {if(!(private_random(c)&7u))(void)sound(c,r,s,3);s.queued_effect_code=0;}
    case 3 or 5 => {(void)submit_positioned_template_request(c,0,c.resources.global_rides_catalog_slot,0xCu,p[0],p[1],p[2]);s.queued_effect_code=0;}
    case 4 => {(void)sound(c,r,s,4);s.queued_effect_code=0;}
    case _ => {}
  }
    // clang-format on
}
} //namespace simcoaster::track_detail

namespace simcoaster {
using namespace track_detail;

void update_track_ride_simulations(WorldRuntime& c) {
    auto& m = manager(c);
    ++m.simulation_step_counter;
    auto live = simulations(m);
    for (auto [owner, s] : live) {
        s->flags &= ~bit(Flag::collision_sound_this_step);
        step_target(c, *owner, *s);
        if (has(*s, Flag::active))
            step_motion(c, *owner, *s);
    }
    const auto collidable = [&](const TrackRideSimulation& s) {
        return has(s, Flag::active)
            && s.active_geometry
            && !(m.geometry.at(*s.active_geometry).flags & 0x80000000u)
            && !has(s, Flag::render_hidden);
    };
    for (auto [owner, s] : live)
        if (has(*s, Flag::active)) {
            if (collidable(*s))
                for (auto [other_owner, other] : live)
                    if (other != s && collidable(*other)) {
                        const auto dx = fixed::subtract(other->position_x, s->position_x),
                                   dy = fixed::subtract(other->position_y, s->position_y),
                                   radius = fixed::add(s->collision_radius, other->collision_radius),
                                   distance = magnitude(dx, dy);
                        if (!distance || distance >= radius)
                            continue;
                        const auto vx = fixed::subtract(s->velocity_x_secondary, other->velocity_x_secondary),
                                   vy = fixed::subtract(s->velocity_y_secondary, other->velocity_y_secondary);
                        const auto normal = heading(dx, dy),
                                   difference = fixed::subtract(heading(vx, vy), normal) & 511;
                        if (difference < 128 || difference > 384) {
                            const auto square = fixed::add(mul(vx, vx), mul(vy, vy));
                            const auto speed = fixed::truncate_integer(std::sqrt(static_cast<double>(square)));
                            const auto projection = scale(4 * cosine(difference), speed),
                                       other_impulse = scale(projection, other_owner->current_values[3]),
                                       impulse = fixed::subtract(projection, other_impulse);
                            other->velocity_x = fixed::add(other->velocity_x, scale(other_impulse, 4 * sine(normal)));
                            other->velocity_y = fixed::add(other->velocity_y, scale(other_impulse, 4 * cosine(normal)));
                            other->flags |= bit(Flag::rebuild_target) | bit(Flag::collision_contact);
                            s->velocity_x = fixed::add(s->velocity_x, scale(impulse, 4 * sine(normal), -1024));
                            s->velocity_y = fixed::add(s->velocity_y, scale(impulse, 4 * cosine(normal), -1024));
                            if (other_impulse > 130
                                && !((s->flags | other->flags) & bit(Flag::collision_sound_this_step))) {
                                // clang-format off
          const auto effect=match(owner->template_tag)->std::uint32_t{case -15 or -14 or -6 or -3=>5;case -10 or -9 or -7 or -4=>3;case _=>0;};
                                // clang-format on
                                if (effect) {
                                    s->flags |= bit(Flag::collision_sound_this_step);
                                    other->flags |= bit(Flag::collision_sound_this_step);
                                    s->pending_effect_code = effect;
                                }
                            }
                        } else {
                            const auto error_x = fixed::subtract(dx, scale(dx, radius, distance)),
                                       error_y = fixed::subtract(dy, scale(dy, radius, distance));
                            const auto correction_x = scale(error_x, other_owner->current_values[3]),
                                       correction_y = scale(error_y, other_owner->current_values[3]);
                            other->position_x = fixed::subtract(other->position_x, correction_x);
                            other->position_y = fixed::subtract(other->position_y, correction_y);
                            s->position_x = fixed::add(s->position_x, fixed::subtract(error_x, correction_x));
                            s->position_y = fixed::add(s->position_y, fixed::subtract(error_y, correction_y));
                        }
                    }
            if (s->boundary_correction_count > 0 && s->boundary_window_steps++ >= 32) {
                if (s->boundary_correction_count > 16) {
                    TrackRideLinkNode* target = nullptr;
                    if (s->active_geometry)
                        if (const auto slot = m.geometry.at(*s->active_geometry).linked_node_slot)
                            target = node(m, *slot);
                    if (!target) {
                        const auto found = std::ranges::find_if(owner->nodes, [](const auto& n) {
                            return (n.flags & 4u) != 0;
                        });
                        if (found != owner->nodes.end())
                            target = &*found;
                    }
                    if (target) {
                        s->selected_track_node_slot = next_node(m, *target, true).pool_slot;
                        s->target_offset_y = s->target_offset_x;
                        const auto threshold = owner->simulation_target_distance_threshold;
                        if (!threshold)
                            throw std::domain_error("track ride boundary divisor");
                        s->target_offset_x = fixed::subtract(
                            threshold / 2,
                            static_cast<std::int32_t>(random_bits(c) % static_cast<std::uint32_t>(threshold)));
                    }
                    s->flags |= bit(Flag::rebuild_target);
                }
                s->boundary_correction_count = 0;
                s->boundary_window_steps = 0;
            }
            resolve_region(c, *owner, *s);
            collide_solids(c, *s);
            process_effects(c, *owner, *s);
        }
}
} //namespace simcoaster
