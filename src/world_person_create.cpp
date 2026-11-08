#include "world_person_internal.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_objects.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr std::array<std::int32_t, 30> prank_by_generation_and_id{ 1, 1, 2, 2, 3, 3, 4, 4, 6, 6, 8, 8, 8, 8, 7,
                                                                   7, 7, 7, 6, 6, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6 };

std::uint32_t random_bits(World& world) {
    return static_cast<std::uint32_t>(next_world_prng_value(world));
}

std::int32_t signed_remainder(std::int32_t value, std::int32_t divisor) {
    if (!divisor || (value == std::numeric_limits<std::int32_t>::min() && divisor == -1))
        throw std::domain_error("invalid Person random divisor");
    return value % divisor;
}

void initialize_person_fields(PersonRuntimeView& person) {
    person.happiness = 0;
    person.cash = person.cash_spent = 0;
    person.needs = {};
    person.exit_level = 0;
    person.number_of_rides = person.number_of_shops = person.number_of_sideshows = person.number_of_sideshows_won = 0;
    person.arrival_date = 0;
    person.major_destination = person.saved_major_destination = person.photo_booth = 0;
    person.person_type = person.queue_position = 0;
    person.queue_move_delay = person.been_admitted = person.time_started_idling = 0;
    person.park_opening_waiting_time = person.paid_admission = person.time_of_last_spot_animation = 0;
    person.happiness_at_queue_entry = 0;
    person.umbrella_state = PersonUmbrellaState::none;
    person.appearance_state_deadline_quarter_ticks = 0;
    person.umbrella_script = 0;
    person.exact_sprite_type_index = 0;
    person.queue_admission_delay_active = 0;
    person.queue_admission_deadline_quarter_ticks = 0;
    person.balloon_script = person.remaining_balloon_life = person.toilet_booby_flag = 0;
    person.last_position_x = person.last_position_y = 0;
    person.state = person.saved_state = PersonState::roaming;
    person.info_desk_timer = 0;
    person.queue_next = person.queue_previous = 0;
    person.food_score = person.drink_score = person.bought_score = person.won_score = person.rides_score = 0;
    person.last_day_checked = std::numeric_limits<std::uint32_t>::max();
}

void initialize_person_sprite_geometry(VisitorWorld& c, PersonRuntimeView& person) {
    const auto index = person.mobile.sprite_id + 4u * static_cast<std::uint32_t>(person.generation);
    person.exact_sprite_type_index = index;
    const auto& config = c.visitors.environment.PersonSpriteUmbrellaModifiers.at(index);
    for (std::size_t i = 0; i < person.sprite_geometry_x.size(); ++i) {
        const auto degrees = fixed::truncate_integer(static_cast<double>(i * 45) + config.FPHandPosAngle) % 360;
        //The original truncates the scaled radians again before its sine/cosine calls.
        const auto radians = fixed::truncate_integer(
            static_cast<double>(degrees) * (static_cast<double>(3.1415927f) * 2.) * static_cast<double>(0.0027777778f));
        person.sprite_geometry_x[i] = static_cast<float>(
            std::sin(static_cast<double>(radians)) * config.FPHandPosDistance + config.FPCentreOffsetX);
        person.sprite_geometry_y[i] = static_cast<float>(
            std::cos(static_cast<double>(radians)) * config.FPHandPosDistance + config.FPCentreOffsetZ);
    }
}
} //namespace

void initialize_world_mobile_entity_base(VisitorWorld& c,
                                         MapObjectBase& base,
                                         MobileEntityState& mobile,
                                         CellId initial_cell,
                                         ThingKind kind) {
    auto& world = person_detail::base_world(c);
    initialize_world_map_object_base(c, &base, initial_cell, static_cast<std::uint32_t>(kind));
    initialize_mobile_sprite_animation(mobile.animation);
    mobile.sprite_angle = mobile.sprite_id = mobile.sprite_category = 0;
    mobile.sprite_under_ride_control = mobile.no_floating_things_on_ride = 0;
    mobile.count = 0;
    mobile.thought_history = {};
    mobile.base_speed = mobile.purpose_speed = mobile.adjustor_speed = 0;
    mobile.previous_speed = 0;
    mobile.last_recorded_map_id = 0;
    mobile.destination_set_successfully = 0;
    initialize_mobile_navigation_state(mobile.navigation);
    mobile.previous_x = mobile.previous_y = 0;
    mobile.stranded_time = 0;
    mobile.base_speed = mobile_base_speed_variants[random_bits(world) % mobile_base_speed_variants.size()];
    mobile.purpose_speed = mobile_purpose_speed_values[0];
    register_world_used_thing_for_event(c, get_world_event_subscription_registry(c), &base.header, 10);
    mobile.last_recorded_map_id = initial_cell;
    mobile.navigation.position = { static_cast<std::int32_t>(base.x) * 256, static_cast<std::int32_t>(base.y) * 256 };
    mobile.navigation.velocity = {};
    mobile.previous_x = mobile.navigation.position.x;
    mobile.previous_y = mobile.navigation.position.y;
}

void initialize_world_person_record(VisitorWorld& c, PersonRuntimeView& person, CellId initial_cell) {
    auto& world = person_detail::base_world(c);
    auto& mobile = person.mobile;
    const auto& info = c.visitors.needs_balance.PeepInfo;
    const auto& config = c.visitors.creation_balance;
    initialize_world_mobile_entity_base(c, person.base, mobile, initial_cell, ThingKind::person);
    initialize_person_fields(person);
    ++c.visitors.count;
    person.force_thought_31 = 0;
    person.force_prank = 0;
    person.first_aid_timeout = 0;
    person.arrival_index = 0;
    person.arrival_date = world.saved.game_tick;
    const auto variation = fixed::narrow(static_cast<std::int64_t>(info.ExitLevelVar) * 2);
    person.exit_level =
        fixed::add(info.ExitLevel,
                   fixed::subtract(signed_remainder(next_world_prng_value(world), variation), info.ExitLevelVar));
    const auto rows = static_cast<std::int32_t>(config.PeepTypesCount) / 3;
    const auto row = signed_remainder(next_world_prng_value(world), rows);
    const auto roll = random_bits(world) % 100u;
    std::uint8_t generation;
    if (config.SpriteTest.OverrideGeneration)
        generation = static_cast<std::uint8_t>(config.SpriteTest.Generation);
    else if (roll < static_cast<std::uint32_t>(info.PercentageOfKids))
        generation = 0;
    else if (roll < static_cast<std::uint32_t>(fixed::add(info.PercentageOfKids, info.PercentageOfParents)))
        generation = 1;
    else if (roll < static_cast<std::uint32_t>(fixed::add(fixed::add(info.PercentageOfKids, info.PercentageOfParents),
                                                          info.PercentageOfGrandparents)))
        generation = 2;
    else
        generation = static_cast<std::uint8_t>(row);
    person.person_type = static_cast<std::uint8_t>(row + static_cast<std::int32_t>(generation) * rows);
    const auto& type = config.PeepTypes.at(person.person_type);
    person.generation = static_cast<PersonGeneration>(type.Generation);
    const auto cash_span = fixed::add(fixed::narrow(static_cast<std::int64_t>(info.StartingCashVarPc) * 2), 1);
    const auto percent =
        fixed::add(fixed::subtract(signed_remainder(next_world_prng_value(world), cash_span), info.StartingCashVarPc),
                   100);
    const auto product = fixed::narrow(static_cast<std::int64_t>(type.StartingCash) * percent);
    person.cash = std::max(product / 100, 0);
    person.preferred_excitement = static_cast<std::uint32_t>(type.PreferredExcitement);
    person.boredom_threshold = static_cast<std::uint32_t>(type.BoredomThreshold);
    mobile.base_speed = static_cast<std::uint16_t>(
        fixed::truncate_integer(static_cast<double>(mobile.base_speed) * type.SpeedMultiplier));
    person.happiness = 50;
    person.needs.thirst = static_cast<float>(random_bits(world) % 50u);
    person.needs.hunger = static_cast<float>(random_bits(world) % 50u);
    person.needs.toilet_need = static_cast<float>(random_bits(world) % 30u);
    person.needs.litter_meter = person.needs.sickness = person.needs.fatigue_effect_meter = 0;
    (void)next_world_prng_value(world);
    if (random_bits(world) % 100u >= static_cast<std::uint32_t>(info.PrankeryLikelihood))
        person.prank = PersonPrank::none;
    else {
        const auto lookup = static_cast<std::uint32_t>(person.generation) * 10u + person.base.header.id % 10u;
        person.prank = static_cast<PersonPrank>(prank_by_generation_and_id.at(lookup));
    }
    person.queue_position = 0;
    person.been_admitted = 0;
    person.major_destination = person.saved_major_destination = person.photo_booth = 0;
    person.queue_next = person.queue_previous = 0;
    person.time_of_last_spot_animation = 0;
    world.saved.random_seed = person.base.header.id;
    if (person.generation == PersonGeneration::child)
        mobile.sprite_category = 0;
    else if (person.generation == PersonGeneration::parent)
        mobile.sprite_category = 1;
    else if (person.generation == PersonGeneration::grandparent)
        mobile.sprite_category = 2;
    if (!config.SpriteTest.OverrideSpriteID) {
        const auto count = get_resource_category_appearance_variant_count(c, mobile.sprite_category);
        if (!count)
            throw std::domain_error("empty Person sprite category");
        mobile.sprite_id = (random_bits(person_detail::primary_world(c)) >> 2) % static_cast<std::uint32_t>(count);
    } else if (person.generation == PersonGeneration::child)
        mobile.sprite_id = static_cast<std::uint32_t>(config.SpriteTest.KidSpriteID);
    else if (person.generation == PersonGeneration::parent)
        mobile.sprite_id = static_cast<std::uint32_t>(config.SpriteTest.ParentSpriteID);
    else if (person.generation == PersonGeneration::grandparent)
        mobile.sprite_id = static_cast<std::uint32_t>(config.SpriteTest.GrandparentSpriteID);
    initialize_person_sprite_geometry(c, person);
    set_world_sprite_animation_resource(c,
                                        mobile.animation,
                                        1,
                                        static_cast<std::int32_t>(mobile.sprite_category),
                                        static_cast<std::int32_t>(mobile.sprite_id));
    set_mobile_entity_purpose_speed(mobile, PurposeSpeedMode::boost_25, person.umbrella_state);
    if (record_passes_grid_predicates(c, &person.base)) {
        person.arrival_index = increment_world_visitor_count(world);
        world_operations::enter_world_person_state(c, &person, PersonState::roaming);
    } else {
        (void)next_world_prng_value(world);
        (void)next_world_prng_value(world);
        const auto booth_count = c.visitors.environment.FixedItemInfo.NumTicketBooths;
        if (!booth_count)
            throw std::domain_error("no ticket booths for arriving Person");
        auto candidate = static_cast<std::uint32_t>(person.base.header.id) % static_cast<std::uint32_t>(booth_count);
        for (std::int32_t i = 0; i < booth_count; ++i) {
            if (try_set_mobile_entity_path_to_cell(c,
                                                   person.base,
                                                   mobile,
                                                   get_ticket_booth_cell(c.visitors.environment, candidate)))
                break;
            candidate ^= 1u;
        }
        person.park_opening_waiting_time = 0;
        world_operations::enter_world_person_state(c, &person, PersonState::walk_to_entrance);
        update_mobile_entity_render_pose(c, person.base, mobile, 0);
        (void)create_runtime_template_at_mobile_entity_resource_position(c, mobile, 19, 0, 0);
    }
    person.paid_admission = 0;
    person.previous_rides.fill(0);
    person.previous_temporary_rides.fill(0);
    WorldEvent event{ PersonCreatedEvent{ person.base.header.id } };
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        &event,
                                        deliver_world_record_event,
                                        nullptr);
}

bool create_world_person_record(VisitorWorld& c) {
    auto& world = person_detail::base_world(c);
    const auto selector = world.saved.arrival_vehicle_size_1 ? BusStopSelector::a
                        : world.saved.arrival_vehicle_size_2 ? BusStopSelector::b
                        : world.saved.arrival_vehicle_size_3 ? BusStopSelector::c
                                                             : BusStopSelector::a;
    const auto cell = get_bus_stop_cell(c.visitors.environment, selector);
    auto* storage = c.entities.allocate_payload(c.ports.memory, sizeof(PersonRuntimeView));
    if (!storage)
        return true;
    auto* person = std::construct_at(static_cast<PersonRuntimeView*>(storage));
    const auto previous_count = c.visitors.count;
    try {
        initialize_world_person_record(c, *person, cell);
    } catch (...) {
        const auto id = person->base.header.id;
        if (id && c.entities.at(id).payload == &person->base.header) {
            if (person->mobile.animation.sprite_script)
                world_operations::release_resource_pool_handle(c, person->mobile.animation.sprite_script);
            if (c.simulation.event_registry)
                remove_world_used_thing_from_all_events(c, c.simulation.event_registry, &person->base.header);
            if (c.simulation.map_cell_records && cell && cell <= map_cell_count)
                unlink_world_used_thing_from_map_cell(c, &c.simulation.map_cell_records[cell - 1], id);
            if (c.visitors.count != previous_count)
                --c.visitors.count;
            unlink_and_free_world_used_thing(c, &c.entities[id]);
        } else
            c.entities.release_payload(c.ports.memory, person);
        throw;
    }
    return true;
}

void world_operations::builtin::create_arrival_person(WorldRuntime& c) {
    (void)create_world_person_record(c);
}
} //namespace simcoaster
