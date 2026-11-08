#include "simcoaster/world_person.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_save_fields.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
static_assert(std::is_standard_layout_v<PersonRuntimeView> && std::is_trivially_destructible_v<PersonRuntimeView>);
static_assert(save::record_width<FixedPointVector2> == 8 && save::record_width<MobileEntityActionHistoryEntry> == 4);

std::int32_t get_resource_category_variant_count(const VisitorQuery& c, std::uint32_t category) {
    if (category >= c.resources.resource_category_variants.size()) {
        throw std::out_of_range("resource category metadata is unavailable");
    }
    return c.resources.resource_category_variants[category].loaded_resource_count;
}

std::int32_t get_resource_category_appearance_variant_count(const VisitorQuery& c, std::uint32_t category) {
    const auto loaded = get_resource_category_variant_count(c, category);
    const auto& counts = c.resources.variants.appearance_variant_counts;
    return counts.empty() ? loaded : std::min(loaded, counts.at(category));
}

bool SaveServices::request_mobile_entity_path(WorldRuntime& c, MapObjectBase& base, MobileEntityState& state, std::uint16_t x, std::uint16_t y) {
    return request_mobile_entity_path_to_destination(c, base, state, x, y);
}

void initialize_mobile_navigation_state(MobileNavigationState& state) {
    state.mass = 0x10000;
    state.radius = 13107;
    state.position = {};
    state.velocity = {};
    state.max_force = 26214;
    state.max_speed = 13107;
    state.local_xaxis = {};
    state.local_yaxis = {};
    state.steering_target_position = nullptr;
    state.unidentified_state = 0;
    state.formation_position = {};
    state.force = {};
    state.path_target_position = {};
    state.path_total_count = 0;
    state.path_buffer_count = 0;
    state.path_count = 0;
    state.path_finished = 1;
    state.subpath_buffer.fill({});
    state.path_total_distance = 0;
    state.path_subpath_distance = 0;
    state.path_tail_distance = 0;
    state.path_last_progress = 0;
    state.path_stuck_buffer = 0;
    state.navigation_mode = 0;
    state.cannot_reach_destination = 0;
    //The native constructor leaves path_timestamp and subpath_distance untouched.
}

void initialize_person_for_restore(PersonRuntimeView& person) {
    person.mobile.thought_history = {};
    initialize_mobile_navigation_state(person.mobile.navigation);
    person.happiness = 0;
    person.needs = {};
    person.happiness_at_queue_entry = 0;
    person.food_score = 0;
    person.drink_score = 0;
    person.bought_score = 0;
    person.won_score = 0;
    person.rides_score = 0;
    person.force_thought_31 = 0;
    //This is the loader's partial constructor. It does not spawn a guest, choose
    //random appearance/needs, register creation events or increment population.
}

int transfer_mobile_thought_history(MobileEntityThoughtHistory& state, SaveArchive archive) {
    if (archive.schema > 1) {
        return 0;
    }
    if (!archive.fields<0, 1>(state)) {
        return 1;
    }
    for (auto& entry : state.event_history) {
        if (!archive.field(entry, "mEventHistory[i]")) {
            return 1;
        }
    }
    return !archive.fields<2>(state);
}

int transfer_mobile_navigation_state(MobileNavigationState& state, SaveArchive archive) {
    if (archive.schema > 1) {
        return 0;
    }
    if (!archive.fields<0, 2>(state)) {
        return 1;
    }
    if (!archive.writing) {
        state.unidentified_state = 0;
    }
    if (!archive.fields<2, 22>(state)) {
        return 1;
    }
    for (std::size_t i = 0; i < state.subpath_buffer.size(); ++i) {
        if (!archive.field(state.subpath_buffer[i], "subpath_buffer[p]") || !archive.field(state.subpath_distance[i], "subpath_dist[p]")) {
            return 1;
        }
    }
    if (!archive.writing) {
        state.steering_target_position = nullptr;
    }
    return !archive.fields<22>(state);
}

int transfer_mobile_entity_state(VisitorWorld& c, MapObjectBase& base, MobileEntityState& state, SaveArchive archive) {
    if (!transfer_map_object_base_state(&base, archive) || archive.record(state.animation)) {
        return 1;
    }
    if (archive.schema <= 1) {
        if (!archive.fields<0, 7>(state) || transfer_mobile_navigation_state(state.navigation, archive) || !archive.fields<7, 15>(state)) {
            return 1;
        }
        if (!archive.writing) {
            const auto divisor = static_cast<std::uint32_t>(get_resource_category_variant_count(c, state.sprite_category));
            if (!divisor) {
                throw std::domain_error("resource category has zero loaded variants");
            }
            state.sprite_id %= divisor;
        }
        if (!archive.fields<15>(state) || transfer_mobile_thought_history(state.thought_history, archive)) {
            return 1;
        }
    }
    //The native logger after a false result discards its arguments and returns.
    //Repath itself still runs on writes and skipped schemas; failures do not
    //turn an otherwise successful transfer into a codec error.
    (void)archive.services.request_mobile_entity_path(c, base, state, state.accurate_destination_x, state.accurate_destination_y);
    return 0;
}

int transfer_world_person_state(VisitorWorld& c, PersonRuntimeView& person, SaveArchive archive) {
    if (transfer_mobile_entity_state(c, person.base, person.mobile, archive)) {
        return 1;
    }
    if (archive.schema > 1) {
        return 0;
    }
    if (!archive.fields<0, 11>(person) || !archive.meter(person.happiness) || !archive.meter(person.needs.hunger) || !archive.fields<11, 13>(person) || !archive.meter(person.needs.litter_meter) || !archive.fields<13, 23>(person)) {
        return 1;
    }
    for (std::size_t i = 0; i < person.previous_rides.size(); ++i) {
        if (!archive.field(person.previous_rides[i], "mPreviousRides[i]") || !archive.field(person.previous_temporary_rides[i], "mPreviousTemporaryRides[i]")) {
            return 1;
        }
    }
    if (!archive.fields<23, 39>(person) || !archive.meter(person.needs.thirst) || !archive.fields<39, 41>(person) || !archive.meter(person.needs.fatigue_effect_meter) || !archive.meter(person.needs.toilet_need) || !archive.meter(person.needs.sickness)) {
        return 1;
    }
    for (auto entry : { std::pair { "msFood", &person.food_score }, std::pair { "msDrink", &person.drink_score }, std::pair { "msBought", &person.bought_score }, std::pair { "msWon", &person.won_score }, std::pair { "msRides", &person.rides_score } }) {
        if (!archive.meter(*entry.second, entry.first)) {
            return 1;
        }
    }
    return !archive.fields<41>(person);
}
} //namespace simcoaster
