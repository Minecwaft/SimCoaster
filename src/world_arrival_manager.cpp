#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
namespace {
World& base_world(VisitorWorld& c) {
    return require_bound(c.simulation.current, "arrival manager requires the base World");
}

template <class T>
T& record(VisitorWorld& c, ThingId id) {
    auto* payload = c.entities.at(id).payload;
    if (!payload)
        throw std::logic_error("arrival manager record is missing");
    return *reinterpret_cast<T*>(payload);
}

std::uint32_t elapsed_quarters(const World& world, std::uint32_t tick) {
    return (world.saved.game_tick >> 2) - (tick >> 2);
}

ParkAnalyserState& analyser(VisitorWorld& c, World& world) {
    return record<ParkAnalyserState>(c, world.saved.park_analyser);
}
} //namespace

std::uint32_t
get_cached_eligible_person_record_count(VisitorWorld& c, ParkAnalyserState& analyser, std::uint32_t refresh_quarters) {
    auto& metric = analyser.metrics[0];
    const auto& world = base_world(c);
    if (!metric.cache_valid || elapsed_quarters(world, metric.last_calculation_tick) > refresh_quarters) {
        metric.cached_value_bits = count_eligible_person_records(c, 4);
        metric.cache_valid = 1;
        metric.last_calculation_tick = world.saved.game_tick;
    }
    return metric.cached_value_bits;
}

std::uint32_t calculate_world_arrival_passenger_count(VisitorWorld& c) {
    auto& world = base_world(c);
    if (world.saved.state == WorldState::transition || c.ports.assets.selected_resource_source(c) == 3)
        return 0;
    const auto population = get_cached_eligible_person_record_count(c, analyser(c, world), 20);
    const auto& arrival = c.visitors.arrivals;
    const auto divisor = std::max(arrival.PointsPerVisitor, 1);
    const auto value = fixed::add(calculate_park_attraction_value(c), arrival.NewParkBonus);
    const auto type = record<WeatherRecord>(c, world.saved.weather).type;
    const auto percent = match (type) -> std::int32_t {
        case WeatherCategory::heatwave => arrival.HeatwavePercentageModifier;
        case WeatherCategory::rain     => arrival.RainPercentageModifier;
        case WeatherCategory::storm    => arrival.StormPercentageModifier;
        case WeatherCategory::snow     => arrival.SnowPercentageModifier;
        case _                         => 100;
    };
    const auto proposed = fixed::truncate_integer(
        static_cast<double>(value) * (static_cast<double>(percent) * static_cast<double>(.01f)) / divisor);
    auto result = std::max(arrival.MinPeople, proposed);
    if (!(c.visitors.arrival_mode_initialized & 1)) {
        c.visitors.arrival_mode_initialized |= 1;
        initialize_game_mode_selector(c);
    }
    const auto cap = c.mode.game == GameMode::online ? 500u : 1500u;
    if (static_cast<std::uint32_t>(result) + population > cap)
        result = std::bit_cast<std::int32_t>(cap - population);
    return std::bit_cast<std::uint32_t>(result);
}

bool is_person_departure_routing_at_bus_stop_a(VisitorWorld& c, const World& world) {
    const auto id = get_bus_stop_cell(c.visitors.environment, BusStopSelector::a);
    if (!id || id > world.map_cells.size())
        throw std::out_of_range("bus stop cell");
    const auto occupant = world.map_cells[id - 1].who;
    if (!occupant)
        return false;
    const auto* payload = c.entities.at(occupant).payload;
    if (!payload)
        throw std::logic_error("bus stop occupant is missing");
    return payload->kind == ThingKind::person
        && reinterpret_cast<const PersonRuntimeView*>(payload)->state == PersonState::departure_routing;
}

void update_bus_arrival_state(VisitorWorld& c, ArrivalState& arrival) {
    auto& world = base_world(c);
    if (!arrival.offloading
        && elapsed_quarters(world, arrival.time_signature)
               > static_cast<std::uint32_t>(c.visitors.arrivals.TimeBetweenArrivals)) {
        arrival.people_on_bus = calculate_world_arrival_passenger_count(c);
        arrival.offloading = 1;
        auto& history = analyser(c, world).tiny_history;
        history.current_entry = fixed::add(history.current_entry, 1);
        if (history.current_entry >= history.num_entries) {
            history.current_entry = 0;
            history.wrapped = 1;
        }
        if (history.current_entry < 0)
            throw std::out_of_range("arrival history index");
        history.data.at(static_cast<std::size_t>(history.current_entry)) = arrival.people_on_bus;
    }
    if (arrival.offloading) {
        const auto status = get_current_arrival_vehicle_status(c, world);
        if (status == 2) {
            if (std::bit_cast<std::int32_t>(arrival.people_on_bus) <= 0) {
                arrival.time_signature = world.saved.game_tick;
                arrival.offloading = 0;
                world_operations::update_arrival_vehicle(c, world, 0);
            } else {
                world_operations::create_arrival_person(c);
                --arrival.people_on_bus;
                ++analyser(c, world).long_histories[17].temporary;
            }
        } else if (status == -1 || status == 6) {
            const auto count = std::bit_cast<std::int32_t>(arrival.people_on_bus);
            world_operations::update_arrival_vehicle(c, world, count > 60 ? 3 : count > 35 ? 2 : 1);
        }
    }
    if (is_person_departure_routing_at_bus_stop_a(c, world)) {
        const auto status = get_current_arrival_vehicle_status(c, world);
        if (status == -1 || status == 0 || (status == 2 && !arrival.people_on_bus))
            world_operations::update_arrival_vehicle(c, world, 0);
    } else if (get_current_arrival_vehicle_status(c, world) == 4)
        world_operations::update_arrival_vehicle(c, world, 0);
}

void world_operations::builtin::arrivals(WorldRuntime& c, ArrivalState* arrival) {
    if (!arrival)
        throw std::invalid_argument("null World arrival state");
    update_bus_arrival_state(c, *arrival);
}
} //namespace simcoaster
