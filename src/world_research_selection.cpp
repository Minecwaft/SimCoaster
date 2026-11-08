#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_research.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
std::int32_t project_count(const ResearchLabRecord& lab) {
    const auto count = std::bit_cast<std::int32_t>(lab.num_projects);
    if (count <= 0) {
        return 0;
    }
    if (lab.num_projects > lab.projects.size()) {
        throw std::length_error("research project count exceeds capacity");
    }
    return count;
}

bool unfinished(const ResearchProject& entry) {
    return !(entry.research_done >= entry.cost);
}

template <class Lab>
auto& group(Lab& lab, ResearchCategory category) {
    return lab.groups.at(static_cast<std::size_t>(category));
}

bool eligible(const ResearchLabRecord& lab, const ResearchProject& entry, ResearchCategory category) {
    return entry.category == category && entry.research_group <= group(lab, category).highest_group_available && unfinished(entry);
}

ResearchCatalogDefinition& definition(ResearchWorld& c, std::uint16_t id) {
    return require_bound<std::out_of_range>(find_object_definition_by_id(&c.resources.object_catalog, id), "research object definition was not found");
}

bool available_parent(ResearchWorld& c, const ResearchProject& entry) {
    auto parent = static_cast<std::uint16_t>(definition(c, entry.object_definition_id).add_on.upgrades_id);
    if (!parent) {
        parent = entry.object_definition_id;
    }
    auto* control = world_operations::find_object_control(c, parent);
    if (!control) {
        throw std::out_of_range("research parent object control was not found");
    }
    return control->available != 0;
}

bool available(ResearchWorld& c, const ResearchLabRecord& lab, const ResearchProject& entry, ResearchCategory category) {
    if (!eligible(lab, entry, category)) {
        return false;
    }
    //Count/Nth perform this lookup for every category. Category4's fallback
    //performs a second lookup; cycling deliberately ignores UpgradesAnyRide.
    const bool any_ride = definition(c, entry.object_definition_id).add_on.upgrades_any_ride == 1;
    return category != ResearchCategory::upgrades || any_ride || available_parent(c, entry);
}

std::int32_t retain_or_clear(ResearchLabRecord& lab, ResearchCategory category) {
    auto& current = group(lab, category).current_project;
    if (current >= 0) {
        const auto& entry = lab.projects.at(static_cast<std::size_t>(current));
        if (entry.research_done >= entry.cost) {
            current = -1;
        }
    }
    return current;
}

std::int32_t integer_cost(float cost) {
    const auto truncated = std::trunc(static_cast<double>(cost));
    if (!std::isfinite(truncated)) {
        throw std::domain_error("research selection cost is not finite");
    }
    const auto limit = std::ldexp(1.0, 63);
    if (truncated < -limit || truncated >= limit) {
        throw std::overflow_error("research selection cost is not representable");
    }
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(truncated)));
}
} //namespace

std::int32_t select_next_research_category_project(ResearchWorld& c, ResearchLabRecord* lab, ResearchCategory category) {
    //Native cycle helpers return void; the selected index is a convenience for
    //the reconstructed API, not a recovered EAX contract.
    std::int32_t selected = -1;
    //The live count, tier and current selection may change during control lookup.
    for (std::int32_t i = 0; i < project_count(*lab); ++i) {
        const auto& entry = lab->projects.at(static_cast<std::size_t>(i));
        if (!eligible(*lab, entry, category)) {
            continue;
        }
        if (category == ResearchCategory::upgrades && !available_parent(c, entry)) {
            continue;
        }
        const auto current = group(*lab, category).current_project;
        if (i < current) {
            if (selected < 0) {
                selected = i;
            }
        } else if (i > current) {
            selected = i;
            break;
        }
    }
    if (selected < 0) {
        return retain_or_clear(*lab, category);
    }
    return group(*lab, category).current_project = selected;
}

std::int32_t select_previous_research_category_project(ResearchWorld& c, ResearchLabRecord* lab, ResearchCategory category) {
    std::int32_t selected = -1;
    //Unlike Next, the reverse scan captures its upper bound once.
    for (std::int32_t i = project_count(*lab) - 1; i >= 0; --i) {
        const auto& entry = lab->projects.at(static_cast<std::size_t>(i));
        if (!eligible(*lab, entry, category)) {
            continue;
        }
        if (category == ResearchCategory::upgrades && !available_parent(c, entry)) {
            continue;
        }
        const auto current = group(*lab, category).current_project;
        if (i > current) {
            if (selected < 0) {
                selected = i;
            }
        } else if (i < current) {
            selected = i;
            break;
        }
    }
    if (selected < 0) {
        return retain_or_clear(*lab, category);
    }
    return group(*lab, category).current_project = selected;
}

std::int32_t choose_research_project_in_cost_band(ResearchWorld& c, const ResearchLabRecord* lab, ResearchProjectCostBand band, std::uint32_t type, ResearchCategory category) {
    const auto count = project_count(*lab);
    bool has_unfinished = false;
    for (std::int32_t i = 0; i < count; ++i) {
        const auto& entry = lab->projects[static_cast<std::size_t>(i)];
        if (entry.category == category && unfinished(entry)) {
            has_unfinished = true;
        }
    }
    if (!has_unfinished) {
        return -2;
    }
    std::array<std::int32_t, 16> indices {}, costs {}, ranked {};
    std::size_t candidates = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        const auto& entry = lab->projects[static_cast<std::size_t>(i)];
        if (entry.type == type && entry.category == category && unfinished(entry) && candidates < indices.size()) {
            indices[candidates] = i;
            costs[candidates] = integer_cost(entry.cost);
            ++candidates;
        }
    }
    if (!candidates) {
        return -1;
    }
    if (candidates == 1) {
        return indices[0];
    }
    std::size_t best = 0;
    for (std::size_t rank = 0; rank < candidates; ++rank) {
        std::int32_t greatest = 0;
        for (std::size_t i = 0; i < candidates; ++i) {
            if (costs[i] > greatest) {
                greatest = costs[i];
                best = i;
            }
        }
        //Keep the prior best when no positive cost remains. Sorting a permutation
        //would change the observed selection for zero/negative/wrapped costs.
        ranked[rank] = indices[best];
        costs[best] = -1;
    }
    const auto half = static_cast<int>(candidates / 2);
    return match (band) -> std::int32_t {
        case ResearchProjectCostBand::lower  => ranked[candidates / 2 + static_cast<std::size_t>(next_crt_random(c) % half)];
        case ResearchProjectCostBand::middle => ranked[candidates / 4 + static_cast<std::size_t>(next_crt_random(c) % half)];
        case ResearchProjectCostBand::higher => ranked[static_cast<std::size_t>(next_crt_random(c) % half)];
        case _                               => -1;
    };
}

std::int32_t count_available_research_projects(ResearchWorld& c, const ResearchLabRecord* lab, ResearchCategory category) {
    std::int32_t count = 0;
    for (std::int32_t i = 0; i < project_count(*lab); ++i) {
        if (available(c, *lab, lab->projects.at(static_cast<std::size_t>(i)), category)) {
            ++count;
        }
    }
    return count;
}

std::int32_t find_nth_available_research_project(ResearchWorld& c, const ResearchLabRecord* lab, ResearchCategory category, std::int32_t ordinal) {
    std::int32_t i = 0, matched = -1;
    for (; i < project_count(*lab); ++i) {
        if (matched == ordinal) {
            break;
        }
        if (available(c, *lab, lab->projects.at(static_cast<std::size_t>(i)), category)) {
            ++matched;
        }
    }
    //Native tests the match on the following iteration. A match on the final
    //physical row therefore returns -1, even though Count includes that row.
    return i < project_count(*lab) ? i - 1 : -1;
}
} //namespace simcoaster
