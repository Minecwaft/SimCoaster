#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
enum class ParkAwardType : std::int32_t {
    coaster_height,
    coaster_speed,
    hygiene,
    decoration,
    security,
    training,
    gokart_speed,
    water_length
};
enum class ParkAwardClaim : std::int32_t {
    unchanged,
    new_award,
    transferred
};

struct ParkAwardCandidate {
    ThingId thing {};
    std::int32_t score {};
};

ParkAwardClaim claim_or_replace_global_park_award(ThemeGlobalInfoManager&, ParkAwardType, std::int32_t score);
ParkAwardCandidate select_coaster_park_award_candidate(EconomyWorld&, bool speed, ThingId definition_filter = 0);
ParkAwardCandidate select_track_park_award_candidate(EconomyWorld&, std::int32_t track_type);
std::int32_t count_park_object_covered_cells(EconomyWorld&);
std::int32_t calculate_park_decoration_percentage(EconomyWorld&);
std::int32_t calculate_secured_park_cell_percentage(EconomyWorld&);
std::int32_t calculate_staff_training_percentage(EconomyWorld&);
void notify_park_award(EconomyWorld&, ParkAwardType, ParkAwardClaim);
bool notify_new_park_award(EconomyWorld&, ParkAwardType, std::int32_t score);
void evaluate_world_park_awards(EconomyWorld&);
} //namespace simcoaster
