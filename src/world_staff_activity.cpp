#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
//The six chart channels omit idle, although idle contributes to the divisor.
constexpr std::array chart_counters { &StaffRuntimeRecord::working_ticks, &StaffRuntimeRecord::patrolling_ticks, &StaffRuntimeRecord::resting_ticks, &StaffRuntimeRecord::training_ticks, &StaffRuntimeRecord::researching_ticks, &StaffRuntimeRecord::strike_ticks };

std::uint32_t total_ticks(const StaffRuntimeRecord& staff) {
    auto total = staff.idle_ticks;
    for (const auto member : chart_counters) {
        total += staff.*member;
    }
    return total;
}

World& base_world(StaffWorld& c) {
    return require_bound(c.simulation.current, "staff activity requires the base World");
}

float percentage(double value) {
    return value >= 0 ? (value <= 100 ? static_cast<float>(value) : 100.0f) : 0.0f;
}

std::int32_t role_wage(const StaffRecruitmentBalance& b, ThingKind kind) {
    return match (kind) -> std::int32_t {
        case ThingKind::handyman    => b.Handyman.MaxWage;
        case ThingKind::mechanic    => b.Mechanic.MaxWage;
        case ThingKind::entertainer => b.Entertainer.MaxWage;
        case ThingKind::guard       => b.Guard.MaxWage;
        case ThingKind::researcher  => b.Researcher.MaxWage;
        case ThingKind::gardener    => b.Gardener.MaxWage;
        case _                      => 0;
    };
}

std::int32_t experience_per_month(const StaffRecruitmentBalance& b, ThingKind kind) {
    //These routines always read grade zero; Gardeners share Handyman's row.
    return match (kind) -> std::int32_t {
        case ThingKind::handyman or ThingKind::gardener => b.handyman_grades[0].ExperiencePerMonth;
        case ThingKind::mechanic                        => b.mechanic_grades[0].ExperiencePerMonth;
        case ThingKind::entertainer                     => b.entertainer_grades[0].ExperiencePerMonth;
        case ThingKind::guard                           => b.guard_grades[0].ExperiencePerMonth;
        case ThingKind::researcher                      => b.researcher_grades[0].ExperiencePerMonth;
        case _                                          => 0;
    };
}

void relocate_influence(StaffWorld& c, StaffRuntimeRecord& staff, RegionEffectProfileId profile, std::int32_t minimum, std::int32_t maximum) {
    const auto current = static_cast<CellId>((static_cast<std::uint32_t>(staff.base.y >> 8) << 7) + (staff.base.x >> 8) + 1u);
    const auto previous = std::exchange(staff.mobile.last_recorded_map_id, current);
    if (previous != current) {
        const auto limit = c.staff.recruitment.balance.all.MaxExperience;
        const auto experience = fixed::word(staff.experience);
        std::int32_t radius;
        if (experience > limit) {
            radius = maximum;
        } else if (experience < 0) {
            radius = minimum;
        } else {
            if (!limit) {
                throw std::domain_error("zero maximum staff experience");
            }
            const auto fraction = fixed::word(staff.experience << 14) / limit;
            const auto first = static_cast<std::uint32_t>(maximum) * static_cast<std::uint32_t>(fraction);
            const auto second = static_cast<std::uint32_t>(minimum) * (16384u - static_cast<std::uint32_t>(fraction));
            radius = fixed::word(first + second) / 16384;
        }
        //Exchange and both region writes precede the shared resource callbacks.
        remove_world_re_cell_effect_profile_with_radius(c.simulation.region_effects, profile, previous, static_cast<std::uint16_t>(radius));
        add_world_re_cell_effect_profile_with_radius(c.simulation.region_effects, profile, current, static_cast<std::uint16_t>(radius));
    }
    update_world_staff_tick(c, staff);
}
} //namespace

StaffType world_staff_type_for_used_thing(const ThingHeader& record) {
    return match (record.kind) -> StaffType {
        case ThingKind::handyman    => StaffType::handyman;
        case ThingKind::mechanic    => StaffType::mechanic;
        case ThingKind::entertainer => StaffType::entertainer;
        case ThingKind::guard       => StaffType::guard;
        case ThingKind::researcher  => StaffType::researcher;
        case ThingKind::gardener    => StaffType::gardener;
        case _                      => StaffType::unclassified;
    };
}

StaffActivityStatus classify_staff_activity(const StaffRuntimeRecord& staff) {
    const auto state = staff.state == StaffState::frozen ? staff.saved_state : staff.state;
    return match (state) -> StaffActivityStatus {
        case StaffState::idle                                                                                                                           => StaffActivityStatus::idle;
        case StaffState::patrolling                                                                                                                     => StaffActivityStatus::looking_for_work;
        case StaffState::walking_to_rest_area or StaffState::resting                                                                                    => StaffActivityStatus::resting;
        case StaffState::walking_to_strike or StaffState::striking                                                                                      => StaffActivityStatus::on_strike;
        case StaffState::walking_to_training_room or StaffState::training or StaffState::training_paused or StaffState::walking_to_training_room_paused => StaffActivityStatus::training;
        case StaffState::picked_up                                                                                                                      => StaffActivityStatus::picked_up;
        case StaffState::frozen or StaffState::frozen_editing_patrol_area                                                                               => StaffActivityStatus::frozen;
        case StaffState::researching or StaffState::walking_to_research_room or StaffState::walking_to_help_research or StaffState::helping_research    => staff.base.header.kind == ThingKind::researcher && !staff.researching ? StaffActivityStatus::idle : StaffActivityStatus::researching;
        case _                                                                                                                                          => StaffActivityStatus::working;
    };
}

bool is_world_staff_interaction_eligible(StaffWorld& c, const StaffRuntimeRecord& staff) {
    const auto activity = classify_staff_activity(staff);
    return (record_passes_grid_predicates(c, &staff.base) || activity == StaffActivityStatus::on_strike) && activity != StaffActivityStatus::picked_up && staff.state != StaffState::repairing && staff.state != StaffState::walking_to_entrance;
}

void update_world_staff_tick(StaffWorld& c, StaffRuntimeRecord& staff) {
    update_mobile_entity_speed_and_cached_position(c, staff.mobile);
    if ((base_world(c).saved.game_tick & 3u) == (staff.base.header.id & 3u)) {
        release_expired_mobile_entity_thought_script(c, staff.mobile.thought_history);
    }
    match (classify_staff_activity(staff)) {
        case StaffActivityStatus::idle             => ++staff.idle_ticks;
        case StaffActivityStatus::looking_for_work => ++staff.patrolling_ticks;
        case StaffActivityStatus::working          => ++staff.working_ticks;
        case StaffActivityStatus::resting          => ++staff.resting_ticks;
        case StaffActivityStatus::on_strike        => ++staff.strike_ticks;
        case StaffActivityStatus::researching      => ++staff.researching_ticks;
        case StaffActivityStatus::training         => ++staff.training_ticks;
        case _                                     => {
        }
    }
}

void relocate_world_guard_record(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto& b = c.staff.recruitment.balance.Guard;
    relocate_influence(c, staff, RegionEffectProfileId::guard, b.MinDetectionRange, b.MaxDetectionRange);
}

void tick_world_entertainer_record(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto& b = c.staff.recruitment.balance.Entertainer;
    relocate_influence(c, staff, RegionEffectProfileId::entertainer, b.MinActiveRange, b.MaxActiveRange);
}

std::int32_t record_world_staff_activity(StaffRuntimeRecord& staff) {
    const auto total = fixed::word(total_ticks(staff));
    std::int32_t last_next = 0;
    for (std::size_t i = 0; i < chart_counters.size(); ++i) {
        auto& ring = staff.activity_rings[i];
        const auto numerator = fixed::word(staff.*chart_counters[i]);
        //IDIV precedes float conversion. A mixed positive interval records zeros,
        //not fractional percentages; idle has no chart but affects every sample.
        ring.temporary = total > 0 ? static_cast<float>(numerator / total) : 0.0f;
        last_next = fixed::add(ring.current_entry, 1);
        ring.current_entry = last_next;
        if (last_next >= ring.num_entries) {
            ring.current_entry = 0;
            ring.wrapped = 1;
        }
        if (ring.current_entry < 0 || static_cast<std::size_t>(ring.current_entry) >= ring.data.size()) {
            throw std::out_of_range("staff activity ring cursor");
        }
        ring.data[static_cast<std::size_t>(ring.current_entry)] = ring.temporary;
    }
    staff.idle_ticks = 0;
    for (const auto member : chart_counters) {
        staff.*member = 0;
    }
    return last_next;
}

std::int32_t sum_staff_activity_ring_samples(const StaffActivityRing& ring, double& sum, std::int32_t maximum) {
    const auto available = ring.wrapped ? ring.num_entries : fixed::add(ring.current_entry, 1);
    const auto count = std::min(maximum, available);
    sum = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        if (i < ring.num_entries && (i <= ring.current_entry || ring.wrapped)) {
            const auto sample = i <= ring.current_entry ? ring.current_entry - i : fixed::subtract(fixed::add(ring.num_entries, ring.current_entry), i);
            if (sample < 0 || static_cast<std::size_t>(sample) >= ring.data.size()) {
                throw std::out_of_range("staff activity sample index");
            }
            sum += ring.data[static_cast<std::size_t>(sample)];
        }
    }
    return count;
}

std::uint32_t grant_world_staff_experience_from_activity_mix(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto& b = c.staff.recruitment.balance;
    const auto useful = staff.working_ticks + staff.researching_ticks;
    const auto share = static_cast<float>(static_cast<double>(useful) / total_ticks(staff));
    auto gain = fixed::truncate_integer(static_cast<double>(experience_per_month(b, staff.base.header.kind)) * share);
    const auto modifier = b.agencies.at(staff.agency).ExperienceGainModifier;
    if (modifier && gain) {
        gain = fixed::truncate_integer(static_cast<double>(modifier) * share + gain);
    }
    const auto total = staff.experience + static_cast<std::uint32_t>(gain);
    staff.experience = total;
    if (total >= static_cast<std::uint32_t>(b.all.MaxExperience)) {
        staff.experience = static_cast<std::uint32_t>(b.all.MaxExperience) - 1u;
    }
    return total;
}

void update_world_staff_wage_state(StaffWorld& c, StaffRuntimeRecord& staff) {
    const auto& b = c.staff.recruitment.balance;
    const auto& agency = b.agencies.at(staff.agency);
    //Hiring multiplies salary by an agency percentage. This monthly path adds
    //InitialWageModifier directly to the rounded experience-derived target.
    const auto target = fixed::add(calculate_staff_salary_from_experience(c, fixed::word(staff.experience), role_wage(b, staff.base.header.kind)), agency.InitialWageModifier);
    if (fixed::word(staff.salary) >= target) {
        staff.wage_demand_pay = staff.wage_demand_day = 0;
        auto gain = b.all.HappyWageVal;
        if (agency.MotivationGainModifier && gain) {
            gain = fixed::word(static_cast<std::uint32_t>(gain) * (static_cast<std::uint32_t>(agency.MotivationGainModifier) + 1u));
        }
        staff.motivation = percentage(static_cast<double>(gain) + staff.motivation);
        staff.happiness_score = percentage(static_cast<double>(gain) + staff.happiness_score);
    } else {
        auto loss = b.all.UnHappyWageVal;
        if (agency.MotivationLossModifier && loss) {
            loss = fixed::word(static_cast<std::uint32_t>(loss) * (static_cast<std::uint32_t>(agency.MotivationLossModifier) + 1u));
        }
        staff.motivation = percentage(static_cast<double>(loss / -2) + staff.motivation);
        staff.happiness_score = percentage(static_cast<double>(loss) * -0.25 + staff.happiness_score);
        staff.wage_demand_pay = static_cast<std::uint32_t>(target) - staff.salary;
    }
    if (staff.wage_demand_pay && static_cast<std::uint8_t>(fixed::truncate_integer(staff.motivation)) <= b.all.LowestWageHappiness) {
        staff.wage_demand_day = static_cast<std::uint32_t>(next_crt_random(c) % 27 + 1);
    }
}

bool world_staff_first_known_ability_matches(const StaffRuntimeRecord& staff, ThemeAbility required) {
    return staff.first_known_ability == required;
}

void complete_world_staff_ability_learning(StaffRuntimeRecord& staff) {
    staff.first_known_ability = staff.currently_learning;
    staff.ability_progress = 0;
    staff.currently_learning = ThemeAbility::none;
}

bool add_world_staff_ability_learning_progress(StaffWorld& c, StaffRuntimeRecord& staff, std::uint32_t increment) {
    staff.ability_progress += increment;
    if (fixed::word(staff.ability_progress) < c.staff.recruitment.balance.all.ProgessUpperLimit) {
        return false;
    }
    complete_world_staff_ability_learning(staff);
    return true;
}
} //namespace simcoaster
