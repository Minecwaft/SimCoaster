#include "world_person_internal.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_fixed_point.hpp"

namespace simcoaster {
namespace {
std::int32_t vehicle_status(VisitorWorld& c, World& world) {
    auto* payload = c.entities.at(world.saved.current_arrival_vehicle).payload;
    if (!payload)
        throw std::logic_error("arrival vehicle is missing");
    auto* script = world_operations::find_script(c, reinterpret_cast<ParkObjectRuntimeView*>(payload)->script_id);
    return world_operations::script_slot(c, script, 1);
}

void clear_finished_vehicle(VisitorWorld& c, World& world) {
    if (!world.saved.current_arrival_vehicle)
        return;
    auto* payload = c.entities.at(world.saved.current_arrival_vehicle).payload;
    if (!payload)
        throw std::logic_error("arrival vehicle is missing");
    auto* script = world_operations::find_script(c, reinterpret_cast<ParkObjectRuntimeView*>(payload)->script_id);
    world_operations::set_script_slot(c, script, 1, 0);
    world.saved.current_arrival_vehicle = 0;
}

CellId cell(std::int32_t x, std::int32_t y) {
    return static_cast<CellId>((static_cast<std::uint32_t>(y) << 7) + static_cast<std::uint32_t>(x) + 1u);
}
} //namespace

CellId get_bus_stop_cell(const PersonEnvironmentBalance& balance, BusStopSelector selector) {
    const auto& fixed = balance.FixedItemInfo;
    match (selector) {
        case BusStopSelector::b => return cell(fixed.BusStopBPosX, fixed.BusStopBPosY);
        case BusStopSelector::c => return cell(fixed.BusStopCPosX, fixed.BusStopCPosY);
        case _ => return cell(fixed.BusStopAPosX, fixed.BusStopAPosY);
    }
}

CellId select_world_map_cell_id_from_pair(const PersonEnvironmentBalance& balance, bool second) {
    const auto& fixed = balance.FixedItemInfo;
    return second ? cell(fixed.EntranceBPosX, fixed.EntranceBPosY) : cell(fixed.EntranceAPosX, fixed.EntranceAPosY);
}

std::int32_t get_current_arrival_vehicle_status(VisitorWorld& c, World& world) {
    if (world.saved.current_arrival_vehicle) {
        const auto status = vehicle_status(c, world);
        if (status != 6)
            return status;
        clear_finished_vehicle(c, world);
    }
    return -1;
}

bool is_current_arrival_vehicle_not_size1(const World& world) {
    return world.saved.current_arrival_vehicle
        && world.saved.current_arrival_vehicle != world.saved.arrival_vehicle_size_1;
}

bool can_person_proceed_from_arrival_vehicle(VisitorWorld& c, World& world) {
    const auto vehicle = world.saved.current_arrival_vehicle;
    if (!vehicle || vehicle != world.saved.arrival_vehicle_size_1)
        return true;
    auto status = vehicle_status(c, world);
    if (status == 6)
        clear_finished_vehicle(c, world);
    else if (status == 3)
        return false;
    if (std::bit_cast<std::int32_t>(world.arrival.people_on_bus) >= 10)
        return true;
    if (world.saved.current_arrival_vehicle) {
        status = vehicle_status(c, world);
        if (status == 6)
            clear_finished_vehicle(c, world);
        else if (status == 1)
            return false;
    }
    if (!world.saved.current_arrival_vehicle)
        return true;
    status = vehicle_status(c, world);
    if (status == 6) {
        clear_finished_vehicle(c, world);
        return true;
    }
    return status != 2;
}

std::int32_t get_world_park_gates_rsse_slot1(VisitorWorld& c, const World& world) {
    if (!world.saved.park_gates)
        return -1;
    auto* payload = c.entities.at(world.saved.park_gates).payload;
    if (!payload)
        throw std::logic_error("park gates are missing");
    auto* script = world_operations::find_script(c, reinterpret_cast<ParkObjectRuntimeView*>(payload)->script_id);
    return world_operations::script_slot(c, script, 1);
}

bool try_set_mobile_entity_path_to_cell(VisitorWorld& c,
                                        MapObjectBase& base,
                                        MobileEntityState& mobile,
                                        CellId destination) {
    const auto index = static_cast<std::uint32_t>(destination) - 1u;
    const auto x = static_cast<std::uint16_t>(((index & 127u) << 8) | 128u),
               y = static_cast<std::uint16_t>(((index >> 7) << 8) | 128u);
    if (next_world_grid_change_serial(c) < mobile.stranded_time)
        mobile.stranded_time = 0;
    if (mobile.stranded_time && local_world_grid_unchanged_since_serial(c, base, mobile))
        return false;
    mobile.accurate_destination_y = y;
    mobile.stranded_time = 0;
    mobile.accurate_destination_x = x;
    return build_world_navigation_path(c,
                                       mobile.navigation,
                                       { static_cast<std::int32_t>(std::bit_cast<std::int16_t>(x)) * 256,
                                         static_cast<std::int32_t>(std::bit_cast<std::int16_t>(y)) * 256 });
}

bool try_set_mobile_entity_path_to_park_exit(VisitorWorld& c, PersonRuntimeView& person) {
    const auto index = static_cast<std::uint32_t>(get_bus_stop_cell(c.visitors.environment, BusStopSelector::a)) - 1u;
    const auto x = static_cast<std::uint16_t>(((index & 127u) << 8)
                                              | (next_world_prng_value(person_detail::base_world(c)) & 255u));
    const auto y = static_cast<std::uint16_t>((((index >> 7) & 255u) << 8)
                                              | (next_world_prng_value(person_detail::base_world(c)) & 255u));
    return request_mobile_entity_path_to_destination(c, person.base, person.mobile, x, y);
}

void restore_world_person_saved_state(VisitorWorld& c, PersonRuntimeView& person) {
    world_operations::enter_world_person_state(c, &person, person.saved_state);
}

void update_person_walking_to_park_entrance(VisitorWorld& c, PersonRuntimeView& person) {
    PurposeSpeedMode speed;
    if (is_current_arrival_vehicle_not_size1(person_detail::base_world(c))
        || get_current_arrival_vehicle_status(c, person_detail::base_world(c)) != 3)
        speed = (person.base.header.id & 3) ? PurposeSpeedMode::normal : PurposeSpeedMode::boost_25;
    else
        speed = PurposeSpeedMode::boost_50;
    set_mobile_entity_purpose_speed(person.mobile, speed, person.umbrella_state);
    if (update_mobile_entity_navigation(c, person.base, person.mobile) == NavigationResult::arrived)
        world_operations::enter_world_person_state(c,
                                                   &person,
                                                   person_detail::base_world(c).saved.park_closed
                                                       ? PersonState::wait_for_park_open
                                                       : PersonState::admission_response);
}

void update_person_waiting_at_park_entrance(VisitorWorld& c, PersonRuntimeView& person) {
    if (person_detail::base_world(c).saved.park_closed
        || get_world_park_gates_rsse_slot1(c, person_detail::base_world(c)) != 1) {
        if (person.park_opening_waiting_time)
            --person.park_opening_waiting_time;
        else {
            if (!try_set_mobile_entity_path_to_park_exit(c, person)) {
                person.mobile.navigation.navigation_mode = 1;
                (void)try_set_mobile_entity_path_to_park_exit(c, person);
            }
            world_operations::enter_world_person_state(c, &person, PersonState::walk_to_exit);
            person.exit_level = 0;
        }
        return;
    }
    if (person.paid_admission) {
        const auto id = person_detail::cell_id(person.base);
        if (!c.simulation.map_cell_records)
            throw std::logic_error("entrance map cells are missing");
        if (!id || id > map_cell_count)
            throw std::out_of_range("entrance map cell");
        if (c.simulation.map_cell_records[id - 1].who == person.base.header.id) {
            const auto entrance =
                select_world_map_cell_id_from_pair(c.visitors.environment,
                                                   (next_world_prng_value(person_detail::base_world(c)) & 1u) != 0);
            (void)try_set_mobile_entity_path_to_cell(c, person.base, person.mobile, entrance);
            world_operations::enter_world_person_state(c, &person, PersonState::cross_entrance);
        }
        return;
    }
    const auto booths = static_cast<std::uint32_t>(c.visitors.environment.FixedItemInfo.NumTicketBooths);
    if (!booths)
        throw std::domain_error("no ticket booths");
    auto candidate = person.base.header.id % booths;
    const auto count = std::bit_cast<std::int32_t>(booths);
    for (std::int32_t i = 0; i < count; ++i) {
        if (person_detail::cell_id(person.base) == get_ticket_booth_cell(c.visitors.environment, candidate)) {
            (void)try_set_mobile_entity_path_to_cell(c,
                                                     person.base,
                                                     person.mobile,
                                                     get_ticket_booth_cell(c.visitors.environment, candidate));
            break;
        }
        candidate ^= 1u;
    }
    person.park_opening_waiting_time = 0;
    world_operations::enter_world_person_state(c, &person, PersonState::walk_to_entrance);
}

void update_person_first_aid_recovery_timer(VisitorWorld& c, PersonRuntimeView& person) {
    const auto old = person.first_aid_timeout;
    person.first_aid_timeout = fixed::subtract(old, 1);
    if (old < 0)
        world_operations::enter_world_person_state(c, &person, PersonState::roaming);
}
} //namespace simcoaster
