#include "simcoaster/entity_identity.hpp"
#include "simcoaster/world_visitor_ui.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_staff.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
std::u16string localized(WorldRuntime& c, const std::vector<std::byte>& table, std::uint32_t row, std::span<const BfstWideStringArgument> strings = {}, std::span<const BfstSignedIntegerArgument> numbers = {}) {
    auto value = format_bfst_wide_string(table, c.messages.mail_language.conversion, row, 16383, strings, numbers);
    if (!value) {
        throw std::runtime_error("Visitor UI language row " + std::to_string(row) + ": " + value.error().reason);
    }
    return std::move(*value);
}

std::int32_t happiness(const PersonRuntimeView& person) {
    //599120/5B27B0: FLD float, __ftol, then retain the low BYTE.
    return static_cast<std::uint8_t>(fixed::truncate_integer(person.happiness));
}

std::int32_t meter(std::int32_t percent) {
    return fixed::narrow(static_cast<std::int64_t>(percent) * 1024) / 100;
}

template <class F>
void guards(WorldRuntime& c, F&& callback) {
    auto* world = c.referenced_world();
    if (!world) {
        return;
    }
    auto id = world->saved.first_guard;
    for (std::size_t visited = 0; id && visited < c.entities.size(); ++visited) {
        auto* header = c.entities.at(id).payload;
        if (!header || header->kind != ThingKind::guard) {
            throw std::runtime_error("Invalid linked Guard identity in Visitor UI");
        }
        auto& guard = *reinterpret_cast<GuardRuntimeRecord*>(header);
        const auto next = guard.staff.next_staff;
        if (!header->retirement_pending && !callback(guard)) {
            return;
        }
        id = next;
    }
}

constexpr std::array<std::int32_t, 22> thought_help { -1, 0xBE, 0xBF, 0xC0, 0xC4, 0xC1, 0xC2, 0xC5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBB, 0xBA, 0xBC, 0xBD, 0xC3, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA };
} //namespace

PersonRuntimeView* find_ui_visitor(WorldRuntime& c, ThingId id) {
    return find_live_entity<PersonRuntimeView, true>(c.entities, id);
}

std::vector<ThingId> enumerate_ui_visitors(WorldRuntime& c, ThingId excluded) {
    std::vector<ThingId> result;
    std::size_t visited = 0;
    for (auto* link = c.entities.active_head; link; link = link->active_next) {
        if (++visited > c.entities.size()) {
            throw std::runtime_error("Cyclic active World list in Visitor UI");
        }
        auto* header = link->payload;
        if (header && header->kind == ThingKind::person && !header->retirement_pending && header->id != excluded) {
            result.push_back(header->id);
        }
    }
    return result;
}

std::u16string visitor_ui_name(WorldRuntime& c, const PersonRuntimeView& person) {
    const std::array numbers { BfstSignedIntegerArgument { 48, fixed::word(person.arrival_index) } };
    return localized(c, c.messages.mail_language.ui_text, person.arrival_index ? 964 : 965, {}, numbers);
}

VisitorUiMetrics visitor_ui_metrics(WorldRuntime& c, const PersonRuntimeView& person) {
    VisitorUiMetrics result;
    result.id = person.base.header.id;
    result.identity = &person.base.header;
    result.arrival_date = person.arrival_date;
    result.arrival_index = person.arrival_index;
    result.name = visitor_ui_name(c, person);
    const auto generation = static_cast<std::int32_t>(person.generation);
    if (generation >= 0 && generation <= 2) {
        result.generation = localized(c, c.messages.mail_language.ui_text, 373u + static_cast<std::uint32_t>(generation));
    }
    result.happiness_meter = meter(happiness(person));
    result.preferred_excitement_meter = meter(static_cast<std::uint8_t>(person.preferred_excitement));
    result.counters = { fixed::word(person.number_of_shops), fixed::word(person.number_of_rides), fixed::word(person.number_of_sideshows), fixed::word(person.number_of_sideshows_won) };
    auto state = person.state == PersonState::return_to_roaming ? person.saved_state : person.state;
    auto activity = static_cast<std::int32_t>(state);
    if (activity < 2 || activity == 28 || activity == 29) {
        activity = 30;
    }
    std::u16string destination;
    if (person.state == PersonState::object_interaction && person.major_destination && person.major_destination < c.entities.size()) {
        const auto* header = c.entities[person.major_destination].payload;
        if (header && header->kind == ThingKind::park_object) {
            destination = park_object_ui_display_name(*reinterpret_cast<const ParkObjectRuntimeView*>(header));
        }
    }
    const std::array strings { BfstWideStringArgument { 0, destination } };
    result.activity = localized(c, c.messages.mail_language.ui_kid_states, static_cast<std::uint32_t>(activity), strings);
    const auto thought = static_cast<std::int32_t>(person.mobile.thought_history.last_thought);
    result.thought_part = thought ? thought - 1 : 999;
    result.thought_help = thought >= 0 && static_cast<std::size_t>(thought) < thought_help.size() ? thought_help[static_cast<std::size_t>(thought)] : -1;
    const auto text_row = person.force_thought_31 ? 31 : thought;
    if (person.force_thought_31) {
        result.thought_part = 8;
        result.thought_help = 0xB7;
    }
    if (text_row < 0 || text_row > 31) {
        throw std::runtime_error("Visitor thought is outside the native table");
    }
    result.thought = localized(c, c.messages.mail_language.ui_thoughts, static_cast<std::uint32_t>(text_row));
    result.can_context = record_passes_grid_predicates(c, &person.base);
    return result;
}

VisitorUiStatistics visitor_ui_statistics(WorldRuntime& c, VisitorPopulation mode, ThingId excluded) {
    VisitorUiStatistics result;
    std::array<std::uint32_t, 31> thoughts {};
    std::int32_t total = 0, eligible = 0, happy = 0, sad = 0, sum = 0;
    for (const auto id : enumerate_ui_visitors(c, excluded)) {
        const auto& person = *find_ui_visitor(c, id);
        const auto valid = record_passes_grid_predicates(c, &person.base);
        result.current += valid ? 1 : 0;
        if (mode != VisitorPopulation::all && static_cast<std::int32_t>(person.generation) != static_cast<std::int32_t>(mode)) {
            continue;
        }
        //598C80 accumulates through six demographic enumerators, while its
        //ratios/top-thought queries accept all generations when filter=-1.
        const auto generation = static_cast<std::int32_t>(person.generation);
        if (generation >= 0 && generation <= 2) {
            ++total;
            sum = fixed::add(sum, happiness(person));
            result.averages[0] = fixed::add(result.averages[0], fixed::word(person.number_of_shops));
            result.averages[1] = fixed::add(result.averages[1], fixed::word(person.number_of_rides));
            result.averages[2] = fixed::add(result.averages[2], fixed::word(person.number_of_sideshows));
        }
        if (!valid) {
            continue;
        }
        ++eligible;
        happy += happiness(person) > 66 ? 1 : 0;
        sad += happiness(person) < 33 ? 1 : 0;
        const auto thought = static_cast<std::size_t>(person.mobile.thought_history.last_thought);
        if (thought >= thoughts.size()) {
            throw std::runtime_error("Visitor statistics thought is outside native range");
        }
        ++thoughts[thought];
    }
    if (total) {
        result.average_happiness_meter = meter(sum / total);
        for (auto& average : result.averages) {
            average /= total;
        }
    }
    if (result.current) {
        const auto good = eligible ? happy * 100 / eligible : 0;
        const auto bad = eligible ? sad * 100 / eligible : 0;
        result.population_meters = { meter(good), meter(100 - good - bad), meter(bad) };
    }
    if (const auto* world = c.referenced_world()) {
        const auto id = world->saved.park_analyser;
        if (id && id < c.entities.size() && c.entities[id].payload && c.entities[id].payload->kind == ThingKind::park_analyser) {
            result.lifetime = fixed::word(c.entities.payload<ParkAnalyserState>(id)->lifetime_visitors);
        }
    }
    thoughts[0] = 0;
    std::array<std::uint8_t, 3> slots { 0, 1, 2 };
    for (std::uint8_t thought = 3; thought < thoughts.size(); ++thought) {
        std::int32_t best = 0;
        std::optional<std::size_t> replace;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const auto difference = fixed::word(thoughts[thought] - thoughts[slots[i]]);
            if (difference > best) {
                best = difference;
                replace = i;
            }
        }
        if (replace) {
            slots[*replace] = thought;
        }
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        result.thoughts[i] = thoughts[slots[i]] ? slots[i] : 31;
    }
    return result;
}

std::int32_t visitor_ui_demographic(WorldRuntime& c, const PersonRuntimeView& person) {
    const auto female = world_operations::get_person_sprite_gender_flag(c, person);
    match (person.generation) {
        case PersonGeneration::child       => return female ? 1 : 2;
        case PersonGeneration::parent      => return female ? 4 : 3;
        case PersonGeneration::grandparent => return female ? 5 : 6;
        case _                             => return 0;
    }
}

ThingId next_ui_visitor(WorldRuntime& c, ThingId current, bool previous, std::int32_t demographic, ThingId excluded) {
    const auto people = enumerate_ui_visitors(c, excluded);
    if (people.empty()) {
        return 0;
    }
    const auto position = std::ranges::find(people, current);
    auto index = position == people.end() ? (previous ? 0u : people.size() - 1) : static_cast<std::size_t>(position - people.begin());
    for (std::size_t tried = 0; tried < people.size(); ++tried) {
        index = previous ? (index + people.size() - 1) % people.size() : (index + 1) % people.size();
        if (!demographic || visitor_ui_demographic(c, *find_ui_visitor(c, people[index])) == demographic) {
            return people[index];
        }
    }
    return 0;
}

std::array<bool, 6> visitor_ui_demographics(WorldRuntime& c, ThingId excluded) {
    std::array<bool, 6> result {};
    for (const auto id : enumerate_ui_visitors(c, excluded)) {
        const auto demographic = visitor_ui_demographic(c, *find_ui_visitor(c, id));
        if (demographic >= 1 && demographic <= 6) {
            result[static_cast<std::size_t>(demographic - 1)] = true;
        }
    }
    return result;
}

std::array<VisitorContextAction, 4> visitor_ui_context(WorldRuntime& c, const PersonRuntimeView& person) {
    bool aid = false;
    guards(c, [&](const GuardRuntimeRecord& guard) {
        aid |= world_staff_first_known_ability_matches(guard.staff, ThemeAbility::first_aid);
        return !aid;
    });
    const auto* world = c.referenced_world();
    return { { { 659, aid && person.first_aid_timeout <= 0 }, { 664, world && world->saved.first_entertainer != 0 }, { 661, world && world->saved.first_guard != 0 }, { 663, true } } };
}

bool execute_visitor_ui_context(WorldRuntime& c, PersonRuntimeView& person, std::uint32_t caption) {
    const auto actions = visitor_ui_context(c, person);
    const auto action = std::ranges::find(actions, caption, &VisitorContextAction::caption);
    if (action == actions.end() || !action->enabled) {
        return false;
    }
    bool done = false;
    if (caption == 659 || caption == 661) {
        guards(c, [&](GuardRuntimeRecord& guard) {
            done = caption == 659 ? start_guard_first_aid(c, guard, person.base.header.id) : start_guard_prosecution(c, guard, person.base.header.id);
            return !done;
        });
        if (done && caption == 659) {
            enter_world_person_state(c, person, PersonState::first_aid_recovery);
        }
    } else if (caption == 664) {
        const auto cell = static_cast<std::size_t>(person.base.y >> 8) * 128 + (person.base.x >> 8);
        auto* world = c.referenced_world();
        if (!world || !c.simulation.map_cell_records || cell >= map_cell_count || (c.simulation.map_cell_records[cell].base.common.flags & 64u)) {
            return false;
        }
        for (auto id = world->saved.first_entertainer; id;) {
            auto* header = c.entities.at(id).payload;
            if (!header || header->kind != ThingKind::entertainer) {
                throw std::runtime_error("Invalid linked Entertainer in Visitor UI");
            }
            auto& staff = reinterpret_cast<EntertainerRuntimeRecord*>(header)->staff;
            if (!header->retirement_pending && set_mobile_entity_destination_and_path(c, staff.base, staff.mobile, static_cast<std::uint16_t>((person.base.x >> 8) * 256u + 128u), static_cast<std::uint16_t>((person.base.y >> 8) * 256u + 128u))) {
                enter_world_staff_state(c, staff, StaffState::be_forced_to_destination);
                staff.forced_state = StaffState::entertaining;
                done = true;
                break;
            }
            id = staff.next_staff;
        }
    }
    //Locator is a frontend navigation action, deliberately handled by its owner.
    return done;
}
} //namespace simcoaster
