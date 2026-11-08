#include "rsse_tour_internal.hpp"

namespace simcoaster::tour_detail {
namespace {
void flying_animation(ScriptWorld& c, RsseTourRecord& r, RsseTourEntry& e) {
    animation(c, e, e.particle_transition_pending ? 9 : 5, 0, 1);
    sound_parameter(c, r, e, e.particle_transition_pending ? 60 : 30);
}

int distance_to_target(const RsseTourEntry& e) {
    return distance(fixed::subtract(e.current_x, e.target_x), fixed::subtract(e.current_z, e.target_z));
}

void state_transition(ScriptWorld& c, RsseTourRecord& r, RsseTourEntry& e, std::size_t index) {
    // clang-format off
  match(e.element_state){
    case State::docked=>{
      e.pitch/=2;sound_parameter(c,r,e,80);
      if(r.docked_element_ready || e.particle_transition_pending || (!r.tour_enabled && !index && r.element_count==1))return;
      e.element_state=State::taking_off;e.propulsion_setting=fixed::add(fixed::word(r.base_propulsion_setting),random(c)%100);e.flight_time_remaining_ticks=r.flight_duration_ticks;
      const auto heading=r.fixed_approach_heading_enabled?r.departure_heading:e.heading;
      e.target_x=fixed::add(r.reference_x,scale(20000,cosine(c,heading)));e.target_z=fixed::add(r.reference_z,scale(20000,sine(c,heading)));
      e.target_y=std::max({fixed::add(r.reference_y,5000),fixed::add(terrain_height(c,e.target_x,e.target_z),5000),14000});
    }
    case State::taking_off=>{
      animation(c,e,4,0,1);sound_parameter(c,r,e,15);
      if(e.current_y>=e.target_y){r.landing_approach_reserved=0;if(e.retire_when_empty)e.element_state=State::seeking_approach;
        else {e.element_state=State::roaming;const auto snap=[](int value){auto result=mul(value/20000,20000);return value<0?fixed::subtract(result,20000):result;};e.previous_target_x=fixed::subtract(snap(e.target_x),20000);e.previous_target_y=e.target_y;e.previous_target_z=snap(e.target_z);}
      }
    }
    case State::roaming=>{
      flying_animation(c,r,e);
      if(e.particle_transition_pending){if(r.active_effect_mode==Effect::fire)e.flight_time_remaining_ticks=0;e.propulsion_setting=fixed::word(r.base_propulsion_setting)/2;}
      if(e.flight_time_remaining_ticks<=0 || e.retire_when_empty){e.element_state=State::seeking_approach;advance_target(c,e);e.target_y=14000;}
      else if(distance_to_target(e)<5000)advance_target(c,e);
    }
    case State::seeking_approach=>{
      flying_animation(c,r,e);
      if(distance_to_target(e)<5000){if(!e.retire_when_empty || e.signed_passenger_count){e.element_state=State::holding;choose_holding_target(r,e);}
        else {e.element_state=State::retiring;random_outside_target(c,e);e.propulsion_setting=mul(2,fixed::word(r.base_propulsion_setting));}
      }
    }
    case State::holding=>{
      flying_animation(c,r,e);
      if(!e.signed_passenger_count && e.retire_when_empty){e.element_state=State::seeking_approach;choose_holding_target(r,e);e.target_y=14000;}
      if(distance_to_target(e)<5000)choose_holding_target(r,e);
      if(!r.landing_approach_reserved){r.landing_approach_reserved=1;
        e.approach_heading=r.fixed_approach_heading_enabled?r.departure_heading:fixed::truncate_integer(std::atan2(static_cast<double>(fixed::subtract(r.reference_z,e.current_z)),static_cast<double>(fixed::subtract(r.reference_x,e.current_x)))*652.);
        e.element_state=r.fixed_approach_heading_enabled?State::outer_approach:State::final_approach;
        const auto radius=mul(r.approach_radius,r.fixed_approach_heading_enabled?6:1);
        e.target_x=fixed::subtract(r.reference_x,scale(radius,cosine(c,e.approach_heading)));e.target_z=fixed::subtract(r.reference_z,scale(radius,sine(c,e.approach_heading)));e.target_y=std::max(fixed::add(r.reference_y,5000),14000);
      }
    }
    case State::outer_approach=>{
      flying_animation(c,r,e);
      if(distance_to_target(e)<5000){e.element_state=State::final_approach;e.target_x=fixed::subtract(r.reference_x,scale(r.approach_radius,cosine(c,e.approach_heading)));e.target_z=fixed::subtract(r.reference_z,scale(r.approach_radius,sine(c,e.approach_heading)));e.target_y=fixed::add(r.reference_y,5000);}
      if(e.retire_when_empty && !e.signed_passenger_count){e.element_state=State::seeking_approach;e.target_y=14000;r.landing_approach_reserved=0;}
    }
    case State::final_approach=>{
      flying_animation(c,r,e);
      if(distance_to_target(e)<5000){e.element_state=State::landing;e.propulsion_setting=fixed::word(r.base_propulsion_setting)/(r.short_approach_enabled?2:1);e.target_x=r.reference_x;e.target_y=r.reference_y;e.target_z=r.reference_z;}
    }
    case State::landing=>{
      if(e.current_y>r.reference_y){animation(c,e,6);sound_parameter(c,r,e,e.particle_transition_pending?60:30);}
      else {e.current_y=r.reference_y;e.element_state=State::docked;r.docked_element_ready=1;e.roll=0;e.propulsion_setting=e.vertical_velocity=0;if(e.signed_passenger_count>0)e.signed_passenger_count=fixed::negate(e.signed_passenger_count);animation(c,e,3);sound_parameter(c,r,e,80);}
    }
    case State::retiring=>{flying_animation(c,r,e);if(distance_to_target(e)<5000){e.element_state=State::inactive;release_element(c,r,e);}}
    case _=>{}
  }
    // clang-format on
}

void separate(ScriptWorld& c, RsseTourRecord& r, RsseTourEntry& e, std::size_t index) {
    for (std::size_t other_index = index + 1; other_index < r.entries.size(); ++other_index) {
        auto& other = r.entries[other_index];
        if (static_cast<std::uint32_t>(other.element_state) < std::to_underlying(State::taking_off))
            continue;
        const auto dx = fixed::subtract(e.current_x, other.current_x),
                   dz = fixed::subtract(e.current_z, other.current_z);
        if (distance(dx, dz) < scale(15000, r.separation_scale)
            && fixed::absolute(fixed::subtract(other.current_y, e.current_y)) < scale(3200, r.separation_scale)) {
            if (e.element_state == State::landing)
                other.vertical_velocity = fixed::add(other.vertical_velocity, 40);
            else if (other.element_state == State::landing)
                e.vertical_velocity = fixed::add(e.vertical_velocity, 40);
            else {
                if (e.forward_speed <= other.forward_speed)
                    e.forward_speed = fixed::subtract(e.forward_speed, e.forward_speed / 16);
                else
                    other.forward_speed = fixed::subtract(other.forward_speed, other.forward_speed / 16);
                if (e.current_y > other.current_y)
                    e.vertical_velocity = fixed::add(e.vertical_velocity, 40);
                else
                    other.vertical_velocity = fixed::add(other.vertical_velocity, 40);
            }
        }
        if (distance(dx, dz, fixed::subtract(e.current_y, other.current_y)) < scale(3000, r.separation_scale)) {
            if (e.current_y <= other.current_y)
                other.current_y = fixed::add(other.current_y, 500);
            else
                e.current_y = fixed::add(e.current_y, 500);
            e.roll = fixed::add(e.roll, 1000);
            other.roll = fixed::subtract(other.roll, 1000);
        }
    }
    (void)c;
}
} //namespace

void step_element(ScriptWorld& c, RsseTourRecord& r, std::size_t index) {
    auto& e = r.entries[index];
    e.previous_x = e.current_x;
    e.previous_y = e.current_y;
    e.previous_z = e.current_z;
    e.previous_heading = e.heading;
    e.previous_pitch = e.pitch;
    e.previous_roll = e.roll;
    if (index && fixed::word(r.element_count) > fixed::word(r.target_element_count) && r.element_count > 1)
        e.retire_when_empty = 1;
    state_transition(c, r, e, index);
    //Even the retiring-to-inactive transition reaches the native motion tail.
    e.forward_speed = scale(mul(e.element_state == State::docked ? 125 : 225, e.forward_speed), 4);
    e.heading_change = scale(31, e.heading_change, 32);
    e.flight_time_remaining_ticks = fixed::subtract(e.flight_time_remaining_ticks, 1);
    e.forward_speed =
        fixed::add(e.forward_speed,
                   fixed::add(e.propulsion_setting, scale(e.propulsion_setting, r.speed_adjustment_tenths, 10)) / 10);
    e.velocity_x = scale(e.forward_speed, cosine(c, e.heading));
    e.velocity_z = scale(e.forward_speed, sine(c, e.heading));
    e.current_x = fixed::add(e.current_x, e.velocity_x);
    e.current_z = fixed::add(e.current_z, e.velocity_z);
    if (e.element_state != State::docked) {
        separate(c, r, e, index);
        auto delta = fixed::subtract(fixed::truncate_integer(
                                         std::atan2(static_cast<double>(fixed::subtract(e.target_z, e.current_z)),
                                                    static_cast<double>(fixed::subtract(e.target_x, e.current_x)))
                                         * 652.),
                                     e.heading)
                   & 4095;
        if (delta >= 2048)
            delta -= 4095;
        e.heading_change = fixed::add(delta, e.heading_change) / 2;
        const auto turn = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(e.heading_change));
        e.heading = fixed::add(e.heading, turn / 10) & 4095;
        if (e.element_state == State::landing) {
            if (e.pitch < 60)
                e.pitch = fixed::add(e.pitch, 2);
            const auto distance_xy =
                mul(distance(fixed::subtract(e.current_x, e.target_x), fixed::subtract(e.current_z, e.target_z)),
                    r.short_approach_enabled ? 2 : 1);
            e.current_y =
                fixed::subtract(fixed::add(fixed::add(mul(7, e.current_y), distance_xy), r.reference_y), 3000) / 8;
        } else {
            const auto dy = fixed::subtract(e.target_y, e.current_y),
                       acceleration = std::clamp(dy / 20, -16, 16) * (fixed::absolute(dy) <= 10000 ? 1 : 3);
            e.vertical_velocity = fixed::add(e.vertical_velocity, acceleration);
            e.vertical_velocity = scale(7, std::clamp(e.vertical_velocity, -512, 512), 8);
            e.current_y = fixed::add(e.current_y, mul(3, e.vertical_velocity));
            e.pitch = scale(e.vertical_velocity, r.attitude_scale);
        }
        auto desired_roll =
            scale(r.attitude_scale, fixed::subtract(e.heading_change / -2, e.heading_change / 4)) & 4095;
        const auto previous_roll = e.roll & 4095;
        if (desired_roll - previous_roll > 2048)
            desired_roll -= 4096;
        else if (desired_roll - previous_roll < -2048)
            desired_roll += 4096;
        e.roll = (fixed::add(desired_roll, mul(7, previous_roll)) / 8) & 4095;
    } else if (e.pitch > 0)
        e.pitch = fixed::subtract(e.pitch, 50);
    if (e.positioned_sound_request)
        e.positioned_sound_request =
            c.ports.resources.update_positioned_request_position(c, e.positioned_sound_request, sound_position(e));
}
} //namespace simcoaster::tour_detail

namespace simcoaster {
void step_rsse_tours(ScriptWorld& c) {
    for (std::uint32_t handle = 1; handle < c.scripts.tours.slots.size(); ++handle)
        if (auto* r = c.scripts.tours.slots[handle].get()) {
            if (fixed::word(r->element_count) < fixed::word(r->target_element_count))
                (void)spawn_rsse_tour_element(c, handle);
            for (std::size_t index = 0; index < r->entries.size(); ++index)
                if (tour_detail::active(r->entries[index]))
                    tour_detail::step_element(c, *r, index);
        }
}
} //namespace simcoaster
