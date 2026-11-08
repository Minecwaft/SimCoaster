#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_events.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int32_t divide(std::int32_t numerator, std::int32_t denominator) {
    if (!denominator) {
        throw std::domain_error("staff work divisor is zero");
    }
    if (numerator == std::numeric_limits<std::int32_t>::min() && denominator == -1) {
        throw std::overflow_error("staff work quotient is not representable");
    }
    return numerator / denominator;
}

std::int64_t truncate_integer(double value) {
    const auto truncated = std::trunc(value);
    if (!std::isfinite(truncated)) {
        throw std::domain_error("research numerical value is not finite");
    }
    const auto limit = std::ldexp(1.0, 63);
    if (truncated < -limit || truncated >= limit) {
        throw std::overflow_error("research numerical value is not representable");
    }
    return static_cast<std::int64_t>(truncated);
}

struct Lane {
    std::int32_t& project;
    ThingId& consultant;
    std::array<ThingId, 20>& workers;
    std::uint8_t& available;
};

Lane lane(ResearchLabRecord& lab, ResearchWorkSlot slot) {
    return match (slot) -> Lane {
        case ResearchWorkSlot::primary => {
            lab.primary_current_project, lab.consultants[0], lab.primary_staff, lab.primary_available
        };
        case ResearchWorkSlot::secondary => {
            lab.secondary_current_project, lab.consultants[1], lab.secondary_staff, lab.secondary_available
        };
        case _ => throw std::out_of_range("research work slot");
    };
}

StaffRuntimeRecord* worker(ResearchWorld& c, ThingId id) {
    return c.entities.at_payload<StaffRuntimeRecord>(id);
}

ResearchLabRecord* base_world_lab(ResearchWorld& c) {
    return c.entities.at_payload<ResearchLabRecord>(c.simulation.current->saved.research_lab);
}

std::size_t work_role(ThingKind kind) {
    return match (kind) -> std::size_t {
        case ThingKind::handyman    => 0;
        case ThingKind::mechanic    => 1;
        case ThingKind::entertainer => 2;
        case ThingKind::guard       => 3;
        case ThingKind::researcher  => 4;
        case ThingKind::gardener    => 5;
        case _                      => throw std::invalid_argument("work calculation requires a staff role");
    };
}

std::size_t requirement_kind(ThingKind kind) {
    //Staff requirement order differs from StaffType's pool index order.
    return match (kind) -> std::size_t {
        case ThingKind::gardener    => 0;
        case ThingKind::handyman    => 1;
        case ThingKind::mechanic    => 2;
        case ThingKind::entertainer => 3;
        case ThingKind::guard       => 4;
        case ThingKind::researcher  => 5;
        case _                      => 6;
    };
}
} //namespace

int calculate_staff_work_value(ResearchWorld& c, const StaffRuntimeRecord* staff) {
    //Game.exe0x62E4C0 truncates motivation and retains its unsigned low byte;
    //motivation is not clamped to a percentage before the experience penalty.
    const auto motivation = static_cast<std::uint8_t>(truncate_integer(staff->motivation));
    const auto penalty = numeric::product(c.staff.work_balance.motivation_factor, 100 - static_cast<std::int32_t>(motivation));
    const auto experience = std::max(numeric::word(staff->experience - numeric::bits(penalty)), 1);
    const auto kind = staff->base.header.kind;
    const auto rates = c.staff.work_balance.by_role.at(work_role(kind));
    const bool difference = kind == ThingKind::mechanic || kind == ThingKind::handyman;
    if (staff->salary < 10u) {
        return difference ? 200 : 1;
    }
    const auto maximum = c.staff.work_balance.max_experience;
    if (experience > maximum) {
        return rates.trained;
    }
    const auto factor = divide(numeric::word(numeric::bits(experience) << 14u), maximum);
    //Keep the two native integer interpolation formulas: their truncation
    //differs, and several shipped role endpoints run from a larger to smaller value.
    if (difference) {
        return numeric::subtract(rates.base, divide(numeric::product(numeric::subtract(rates.base, rates.trained), factor), 16384));
    }
    return divide(numeric::add(numeric::product(rates.trained, factor), numeric::product(rates.base, numeric::subtract(16384, factor))), 16384);
}

float calculate_research_slot_work_increment(ResearchWorld& c, ResearchLabRecord* lab, ResearchWorkSlot slot) {
    auto selected = lane(*lab, slot);
    float increment = 0.0f;
    for (std::size_t i = 0; i < selected.workers.size(); ++i) {
        const auto id = selected.workers[i];
        if (!id) {
            continue;
        }
        const auto* staff = worker(c, id);
        if (staff->state != StaffState::researching) {
            continue;
        }
        //Workload is a signed DWORD snapshot before calculating work. Scale is
        //read afterwards, and each contributor stores a new single-precision sum.
        const auto workload = numeric::word(lab->workload);
        const auto work = calculate_staff_work_value(c, staff);
        increment = static_cast<float>(static_cast<double>(work) * workload * c.research.balance.speed_scale_down + increment);
    }
    return increment;
}

void update_research_slot_staffing_status(ResearchWorld& c, ResearchLabRecord* lab, ResearchWorkSlot slot) {
    auto selected = lane(*lab, slot);
    if (selected.project < 0) {
        selected.available = 0;
        return;
    }
    if (slot == ResearchWorkSlot::secondary) {
        selected.available = 1;
        return;
    }
    std::array<std::int32_t, 6> available {}, required {};
    std::array<std::int32_t, 15> abilities {}, required_abilities {};
    for (auto id : lab->primary_staff) {
        if (!id) {
            continue;
        }
        const auto* staff = worker(c, id);
        const auto kind = requirement_kind(staff->base.header.kind);
        if (kind < available.size()) {
            ++available[kind];
        }
        ++abilities.at(static_cast<std::size_t>(staff->first_known_ability));
    }
    const auto& project = lab->projects.at(static_cast<std::size_t>(selected.project));
    const auto requirement = static_cast<std::int32_t>(project.staff_required);
    if (requirement) {
        ++required.at(static_cast<std::size_t>(static_cast<std::int64_t>(requirement) - 1));
        ++required_abilities.at(static_cast<std::size_t>(project.staff_ability));
    }
    selected.available = 1;
    for (std::size_t i = 0; i < available.size(); ++i) {
        if (available[i] < required[i]) {
            selected.available = 0;
        }
    }
    //NONE is counted but deliberately not required by this helper's comparison.
    for (std::size_t i = 1; i < abilities.size(); ++i) {
        if (abilities[i] < required_abilities[i]) {
            selected.available = 0;
        }
    }
}

void advance_world_research_project(ResearchWorld& c, ResearchProject* entry, float increment, ResearchWorkSlot slot) {
    if (entry->research_done >= entry->cost) {
        clear_world_research_lab_worker_ids(c, base_world_lab(c), slot);
        return;
    }
    //Unordered progress enters work, while an unordered sum never triggers the
    //upper cap or completion. Negative increments have no additional floor.
    if (static_cast<double>(increment) + entry->research_done > entry->cost) {
        increment = entry->cost - entry->research_done;
    }
    entry->research_done += increment;
    const auto amount = numeric::word(static_cast<std::uint32_t>(truncate_integer(static_cast<double>(entry->cash_per_brain_point) * increment)));
    auto* bank = c.entities.at_payload<BankRecord>(c.referenced_world()->saved.bank_account);
    charge_world_bank_account_expense(c, bank, amount);
    if (!(entry->research_done >= entry->cost)) {
        return;
    }

    world_operations::find_object_control(c, entry->object_definition_id)->available = 1;
    const std::uint32_t upgrade = entry->upgrade_level;
    world_operations::find_object_control(c, entry->object_definition_id)->research_option_index = upgrade;
    WorldEvent event { ResearchAvailabilityEvent { entry->object_definition_id, entry->category } };
    dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
    //Dispatch can mutate this entry and the shared catalog iterator. Start a
    //fresh traversal, then keep each lookup and next-entry operation live.
    for (auto* definition = first_object_catalog_entry(&c.resources.object_catalog); definition; definition = next_object_catalog_entry(&c.resources.object_catalog)) {
        if (definition->research.linked_with == entry->object_definition_id) {
            world_operations::find_object_control(c, definition->id)->available = 1;
        }
    }
    clear_world_research_lab_worker_ids(c, base_world_lab(c), slot);
}

void update_world_research_lab_state(ResearchWorld& c, ResearchLabRecord* lab) {
    if (numeric::word(c.simulation.current->saved.game_tick) % 6) {
        return;
    }
    for (auto slot : { ResearchWorkSlot::primary, ResearchWorkSlot::secondary }) {
        auto selected = lane(*lab, slot);
        if (selected.project < 0) {
            continue;
        }
        if (!selected.consultant && map_research_staff_requirement_to_staff_type_index(lab->projects.at(static_cast<std::size_t>(selected.project)).staff_required) != StaffType::unsupported_index) {
            continue;
        }
        const auto increment = calculate_research_slot_work_increment(c, lab, slot);
        //Re-read the selected project after work calculation and after any effects
        //of completing the preceding lane; do not replace it with a cached index.
        advance_world_research_project(c, &lab->projects.at(static_cast<std::size_t>(selected.project)), increment, slot);
    }
}
} //namespace simcoaster
