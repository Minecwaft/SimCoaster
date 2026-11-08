#include "simcoaster/require_bound.hpp"
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
struct RoleSettings {
    std::int32_t average, pool_maximum, minimum, park_maximum, wage;
    std::uint32_t category;
    ThingKind kind;
};

RoleSettings settings(const StaffRecruitmentBalance& b, StaffType role) {
    const auto& p = b.pool;
    return match (role) -> RoleSettings {
        case StaffType::handyman => RoleSettings{ p.AvgExperienceOfHandymen, p.MaxHandymen,      p.MinHandymenInPool,
                                                  p.MaxHandymenInPark,       b.Handyman.MaxWage, 9,
                                                  ThingKind::handyman };
        case StaffType::mechanic => RoleSettings{ p.AvgExperienceOfMechanics, p.MaxMechanics,     p.MinMechanicsInPool,
                                                  p.MaxMechanicsInPark,       b.Mechanic.MaxWage, 10,
                                                  ThingKind::mechanic };
        case StaffType::entertainer =>
            RoleSettings{ p.AvgExperienceOfEntertainers, p.MaxEntertainers,     p.MinEntertainersInPool,
                          p.MaxEntertainersInPark,       b.Entertainer.MaxWage, 8,
                          ThingKind::entertainer };
        case StaffType::guard =>
            RoleSettings{ p.AvgExperienceOfGuards, p.MaxGuards, p.MinGuardsInPool, p.MaxGuardsInPark,
                          b.Guard.MaxWage,         11,          ThingKind::guard };
        case StaffType::researcher =>
            RoleSettings{ p.AvgExperienceOfResearchers, p.MaxResearchers,     p.MinResearchersInPool,
                          p.MaxResearchersInPark,       b.Researcher.MaxWage, 12,
                          ThingKind::researcher };
        //All three Handyman aliases are native behavior; separate Gardener SAM
        //keys exist, but this recruitment path never reads them.
        case StaffType::gardener => RoleSettings{ p.AvgExperienceOfHandymen, p.MaxHandymen,      p.MinHandymenInPool,
                                                  p.MaxGardenersInPark,      b.Gardener.MaxWage, 18,
                                                  ThingKind::gardener };
        case _                   => throw std::out_of_range("staff recruitment role");
    };
}

std::size_t index(StaffType type) {
    const auto value = std::to_underlying(type);
    if (value < 0 || value >= 6)
        throw std::out_of_range("staff role index");
    return static_cast<std::size_t>(value);
}

World& primary(StaffWorld& c) {
    return require_bound(c.simulation.reference, "staff recruitment needs the primary World");
}

std::uint32_t draw(StaffWorld& c) {
    return static_cast<std::uint32_t>(next_world_prng_value(primary(c)));
}

void changed(StaffWorld& c, std::size_t slot) {
    if (slot >= 32)
        throw std::out_of_range("staff candidate slot");
    c.staff.recruitment.changed_slots |= 1u << slot;
}

std::u16string_view name(const StaffQuery& c, const StaffCandidate& candidate) {
    return c.staff.recruitment.names.at(index(candidate.type)).at(candidate.name_index);
}

bool name_used(StaffWorld& c, const World& world, std::u16string_view proposed) {
    for (const auto& candidate : world.staff_candidates)
        if (candidate.valid && name(c, candidate) == proposed)
            return true;
    for (auto* entry = c.entities.active_head; entry; entry = entry->active_next)
        if (entry->payload && is_staff_record_kind(entry->payload->kind)) {
            const auto& text = reinterpret_cast<const StaffRuntimeRecord*>(entry->payload)->name;
            if (std::u16string_view{ text.data(),
                                     static_cast<std::size_t>(std::ranges::find(text, u'\0') - text.begin()) }
                == proposed)
                return true;
        }
    return false;
}

std::size_t valid_count(const World& world) {
    return static_cast<std::size_t>(std::ranges::count_if(world.staff_candidates, [](const StaffCandidate& row) {
        return row.valid != 0;
    }));
}

bool add_if_allowed(StaffWorld& c, World& world, StaffType role) {
    if (world.staff_pool.stop_producing[index(role)])
        return false;
    const auto slot = find_free_world_staff_candidate_slot(world);
    if (slot == world.staff_candidates.size())
        return false;
    initialize_world_staff_candidate(c, world.staff_candidates[slot], role, slot);
    return true;
}
} //namespace

std::size_t find_free_world_staff_candidate_slot(const World& world) {
    return static_cast<std::size_t>(std::ranges::find_if(world.staff_candidates,
                                                         [](const StaffCandidate& row) {
                                                             return !row.valid;
                                                         })
                                    - world.staff_candidates.begin());
}

std::size_t count_world_staff_candidates(const World& world, StaffType role) {
    return static_cast<std::size_t>(std::ranges::count_if(world.staff_candidates, [=](const StaffCandidate& row) {
        return row.valid && row.type == role;
    }));
}

void recount_world_staff_counts(StaffWorld& c, World& world) {
    world.staff_pool.linked_count.fill(0);
    world.staff_pool.stop_producing.fill(0);
    for (auto* entry = c.entities.active_head; entry; entry = entry->active_next)
        if (entry->payload && is_staff_record_kind(entry->payload->kind))
            for (std::size_t i = 0; i < 6; ++i) {
                const auto role = settings(c.staff.recruitment.balance, static_cast<StaffType>(i));
                if (entry->payload->kind == role.kind) {
                    auto& count = world.staff_pool.linked_count[i];
                    ++count;
                    if (numeric::word(count) >= role.park_maximum) {
                        world.staff_pool.stop_producing[i] = 1;
                    }
                    break;
                }
            }
}

std::int32_t
calculate_staff_salary_from_experience(const StaffQuery& c, std::int32_t experience, std::int32_t maximum_wage) {
    const auto maximum = c.staff.recruitment.balance.all.MaxExperience;
    std::int32_t wage = 0;
    if (experience > maximum)
        wage = maximum_wage;
    else if (experience >= 0) {
        if (!maximum)
            throw std::domain_error("zero maximum staff experience");
        const auto fraction = numeric::word(static_cast<std::uint32_t>(experience) << 14) / maximum;
        wage = numeric::word(static_cast<std::uint32_t>(maximum_wage) * static_cast<std::uint32_t>(fraction)) / 16384;
    }
    return std::max(10, wage - wage % 20);
}

void initialize_world_staff_candidate(StaffWorld& c, StaffCandidate& candidate, StaffType role, std::size_t slot) {
    auto& runtime = c.staff.recruitment;
    const auto& balance = runtime.balance;
    const auto policy = settings(balance, role);
    const auto promotion = get_or_create_theme_global_info_manager(c).current_promotion_index;
    std::size_t agencies = 0;
    while (agencies < balance.agencies.size() && balance.agencies[agencies].RequiredPromotionLevel <= promotion)
        ++agencies;
    if (!agencies || !balance.pool.AvgWageDifference || !(balance.pool.StaffTimeoutTime >> 1))
        throw std::domain_error("invalid staff recruitment balance");
    const auto variants = get_resource_category_appearance_variant_count(c, policy.category);
    if (variants <= 0 || runtime.names[index(role)].size() < 35)
        throw std::logic_error("staff resource variants or name table are missing");
    candidate.type = role;
    candidate.agency_index = static_cast<std::uint32_t>(next_crt_random(c)) % static_cast<std::uint32_t>(agencies);
    candidate.resource_variant = static_cast<std::uint8_t>((draw(c) >> 2) % static_cast<std::uint32_t>(variants));
    auto experience = numeric::word(draw(c) % static_cast<std::uint32_t>(balance.pool.AvgWageDifference) - static_cast<std::uint32_t>(balance.pool.AvgWageDifference / 2) + static_cast<std::uint32_t>(policy.average));
    const auto& agency = balance.agencies[candidate.agency_index];
    if (agency.InitialExperienceModifier && experience)
        experience = numeric::word(static_cast<std::uint32_t>(experience) + static_cast<std::uint32_t>(numeric::word(static_cast<std::uint32_t>(experience) * static_cast<std::uint32_t>(agency.InitialExperienceModifier)) / 100));
    experience = std::min(experience, balance.all.MaxExperience);
    candidate.experience = static_cast<std::uint32_t>(experience);
    candidate.salary = static_cast<std::uint32_t>(calculate_staff_salary_from_experience(c, experience, policy.wage));
    if (agency.InitialWageModifier && candidate.salary)
        candidate.salary += static_cast<std::uint32_t>(agency.InitialWageModifier) * candidate.salary / 100u;
    for (unsigned attempt = 0; attempt < 15; ++attempt) {
        candidate.name_index = draw(c) % 35u;
        if (!name_used(c, primary(c), name(c, candidate)))
            break;
    }
    candidate.valid = 1;
    changed(c, slot);
    candidate.creation_tick = primary(c).saved.game_tick;
    candidate.timeout = static_cast<std::uint32_t>(balance.pool.StaffTimeoutTime)
                      + draw(c) % static_cast<std::uint32_t>(balance.pool.StaffTimeoutTime >> 1);
}

void replenish_world_staff_pool(StaffWorld& c, World& world) {
    constexpr std::array order{ StaffType::mechanic,
                                StaffType::handyman,
                                StaffType::gardener,
                                StaffType::entertainer,
                                StaffType::guard };
    for (;;) {
        const auto before = valid_count(world);
        bool deficit = false;
        for (const auto role : order) {
            const auto total = static_cast<std::uint32_t>(count_world_staff_candidates(world, role))
                             + world.staff_pool.linked_count[index(role)];
            if (numeric::word(total) < settings(c.staff.recruitment.balance, role).minimum) {
                deficit = true;
                (void)add_if_allowed(c, world, role);
            }
        }
        const auto researchers = static_cast<std::uint32_t>(count_world_staff_candidates(world, StaffType::researcher))
                               + world.staff_pool.linked_count[index(StaffType::researcher)];
        if (numeric::word(researchers) < settings(c.staff.recruitment.balance, StaffType::researcher).minimum) {
            (void)add_if_allowed(c, world, StaffType::researcher);
        } else if (!deficit)
            return;
        //Native repeats forever when a target cannot be reached. Return once
        //production gates or a full pool prevent every attempted addition.
        if (valid_count(world) == before)
            return;
    }
}

bool generate_opening_staff_pool(StaffWorld& c, World& world) {
    if (world.staff_pool.opening_pool_generated)
        return false;
    const auto& p = c.staff.recruitment.balance.pool;
    const std::array counts{ p.BeginningNumberOfHandymen,     p.BeginningNumberOfMechanics,
                             p.BeginningNumberOfEntertainers, p.BeginningNumberOfGuards,
                             p.BeginningNumberOfResearchers,  p.BeginningNumberOfHandymen };
    constexpr std::array order{ StaffType::handyman,    StaffType::mechanic, StaffType::gardener,
                                StaffType::entertainer, StaffType::guard,    StaffType::researcher };
    for (const auto role : order)
        for (std::int32_t i = 0; i < counts[index(role)]; ++i)
            if (!add_if_allowed(c, world, role))
                break;
    world.staff_pool.opening_pool_generated = 1;
    return true;
}

void initialize_world_opening_staff_pool(StaffWorld& c, World& world) {
    (void)generate_opening_staff_pool(c, world);
    world.staff_pool.last_update_tick = primary(c).saved.game_tick;
}

void maintain_world_staff_pool(StaffWorld& c, World& world) {
    const auto now = primary(c).saved.game_tick;
    for (std::size_t i = 0; i < world.staff_candidates.size(); ++i) {
        auto& candidate = world.staff_candidates[i];
        if (candidate.valid
            && now - candidate.creation_tick > 4u * candidate.timeout
            && !candidate.on_pointer
            && c.staff.recruitment.selected_candidate != static_cast<std::int32_t>(i)) {
            candidate.valid = 0;
            changed(c, i);
        }
    }
    const auto& balance = c.staff.recruitment.balance;
    if (now - world.staff_pool.last_update_tick
        <= 4u * static_cast<std::uint32_t>(balance.pool.TimeBetweenStaffUpdates))
        return;
    auto additions = std::min(static_cast<std::uint32_t>(balance.pool.MaxNumberOfStaffPerUpdate),
                              static_cast<std::uint32_t>(world.staff_candidates.size() - valid_count(world)));
    recount_world_staff_counts(c, world);
    std::array<std::int32_t, 6> deficits;
    std::int32_t total = 0;
    for (std::size_t i = 0; i < deficits.size(); ++i) {
        deficits[i] = world.staff_pool.stop_producing[i] ? 0 : numeric::word(static_cast<std::uint32_t>(settings(balance, static_cast<StaffType>(i)).pool_maximum) - static_cast<std::uint32_t>(count_world_staff_candidates(world, static_cast<StaffType>(i))));
        total = numeric::word(static_cast<std::uint32_t>(total) + static_cast<std::uint32_t>(deficits[i]));
    }
    while (additions && total > 0) {
        auto roll = numeric::word(draw(c) % static_cast<std::uint32_t>(total));
        std::size_t selected = 0;
        while (selected < deficits.size() && roll >= deficits[selected]) {
            roll = numeric::word(static_cast<std::uint32_t>(roll) - static_cast<std::uint32_t>(deficits[selected]));
            ++selected;
        }
        if (selected == deficits.size())
            throw std::logic_error("invalid weighted staff deficits");
        if (!add_if_allowed(c, world, static_cast<StaffType>(selected)))
            break;
        --total;
        --additions;
        --deficits[selected];
    }
    replenish_world_staff_pool(c, world);
    world.staff_pool.last_update_tick = primary(c).saved.game_tick;
}

void world_operations::builtin::staff_pool(WorldRuntime& c, World* world) {
    if (!world)
        throw std::invalid_argument("null staff pool World");
    maintain_world_staff_pool(c, *world);
}
} //namespace simcoaster
