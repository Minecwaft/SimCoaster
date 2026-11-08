#include "simcoaster/rsse_vm.hpp"
#include "simcoaster/world_grid.hpp"
#include "track_ride_internal.hpp"

namespace simcoaster {
namespace {
bool station_track(const World& w, const TrackCell& cell) {
    const auto type = cell.base.common.type;
    if (type == 13 || type == 18) {
        return true;
    }
    if (!world_cell_is_track_child(&cell.base)) {
        return false;
    }
    const auto parent = cell.base.common.parent_id;
    if (!parent || parent > map_cell_count) {
        return false;
    }
    const auto parent_type = w.track_cells[parent - 1].base.common.type;
    return parent_type == 13 || parent_type == 18;
}

void endpoint(ScriptWorld& c, ParkObjectRuntimeView& object, bool valid, bool other) {
    auto& w = track_detail::world(c);
    auto x = static_cast<int>(object.base.x >> 8), y = static_cast<int>(object.base.y >> 8);
    std::uint8_t direction = static_cast<std::uint8_t>(valid);
    match (object.angle) {
        case 0 => {
            x += other ? -2 : 4;
            ++y;
            direction = 4;
        }
        case 90 => {
            ++x;
            y += other ? 1 : -5;
            direction = 1;
        }
        case 180 => {
            x += other ? 1 : -5;
            y -= 2;
            direction = 64;
        }
        case 270 => {
            x -= 2;
            y += other ? -2 : 4;
            direction = 16;
        }
        case _ => {
        }
    } if (other) {
        direction = opposite_world_cell_direction(direction);
    }
    auto remaining = direction;
    for (std::size_t visited = 0; visited < map_cell_count; ++visited) {
        const auto id = static_cast<CellId>(static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(y) * 128u + 1u);
        if (!id || id > map_cell_count) {
            throw std::out_of_range("track ride endpoint cell");
        }
        auto& cell = w.track_cells[id - 1];
        bool connected = true;
        if (world_cell_is_track_root(&cell.base)) {
            if (get_world_cell_corner_neighbor_mask(c, &cell.base)) {
                auto back = opposite_world_cell_direction(direction);
                if (!(get_world_cell_neighbor_mask(c, &cell.base) & back)) {
                    back = remaining;
                }
                remaining = static_cast<std::uint8_t>(back ^ get_world_cell_neighbor_mask(c, &cell.base));
                direction = remaining;
            }
            if (cell.base.common.mesh_instance) {
                (void)world_operations::request_mesh_animation_control_transition(c, cell.base.common.mesh_instance, static_cast<MeshAnimationRequest>(valid ? 5 : 12), 0, static_cast<MeshAnimationTransitionFlags>(7), 1.f, 0);
            }
        } else {
            remaining = direction = 0;
        }
        if (!get_world_cell_corner_neighbor_mask(c, &cell.base) && !(get_world_cell_neighbor_mask(c, &cell.base) & remaining)) {
            connected = false;
        }
        if (auto* next = resolve_track_cell_in_direction(c, &cell.base, direction, 2)) {
            if (station_track(w, *next) && next->base.common.parent_id == cell.base.common.parent_id && (get_world_cell_neighbor_mask(c, &next->base) & opposite_world_cell_direction(direction))) {
                connected = false;
            }
        }
        match (direction) {
            case 1  => y -= 2;
            case 4  => x += 2;
            case 16 => y += 2;
            case 64 => x -= 2;
            case _  => {
            }
        } if (!connected) {
            return;
        }
    }
    throw std::logic_error("cyclic track ride endpoint traversal");
}
} //namespace

void step_rsse_bump_instruction(ScriptWorld& c, RsseScriptState& script) {
    auto* header = c.entities.at(script.owner_used_thing_id).payload;
    if (!header || header->kind != ThingKind::park_object) {
        throw std::invalid_argument("RSSE BUMP owner is not a park object");
    }
    auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(header);
    const auto handle = object.track_ride_handle;
    const auto read = [&] {
        return read_rsse_instruction_word(script);
    };
    const auto resolve = [&] {
        return resolve_rsse_operand(script, read());
    };
    const auto tagged = [](std::uint32_t word) {
        return (word & rsse_tag_mask) == std::to_underlying(RsseWordTag::value_slot);
    };
    const auto output = [&](std::uint32_t destination, std::int32_t value) {
        script.current_value = value;
        if (tagged(destination)) {
            script.value_slots.at(destination ^ std::to_underlying(RsseWordTag::value_slot)) = value;
        }
    };
    // clang-format off
  match(static_cast<RsseBumpCommand>(read())) {
    case RsseBumpCommand::board_person => {const auto source=read();if(tagged(source))script.current_value=enqueue_track_ride_rider(c,handle,static_cast<std::uint32_t>(script.value_slots.at(source^std::to_underlying(RsseWordTag::value_slot))));}
    case RsseBumpCommand::pop_person => {const auto destination=read();if(tagged(destination))output(destination,std::bit_cast<std::int32_t>(dequeue_track_ride_rider(c,handle)));}
    case RsseBumpCommand::start => {(void)read();start_track_ride_simulations(c,handle);}
    // The native return is a pointer. All ten installed BUMP-4 sites either
    // branch on zero (four sites), or overwrite LV with GETTIME/COPY (six).
    // Keep an opaque positive pool identity instead of truncating an x64 pointer.
    case RsseBumpCommand::create_vehicle => {(void)read();const auto* vehicle=create_track_ride_simulation(c,handle);script.current_value=vehicle?vehicle->pool_slot+1:0;}
    case RsseBumpCommand::get_valid => {(void)read();script.current_value=object.is_track_ride_valid;}
    case RsseBumpCommand::close => {(void)read();close_track_ride(c,handle);}
    case RsseBumpCommand::begin_open => {(void)read();begin_track_ride_open(c,handle);}
    case RsseBumpCommand::set_broken => {const auto enabled=resolve();set_track_ride_fault(c,handle,enabled?TrackRideFault::broken:TrackRideFault::none);}
    case RsseBumpCommand::set_worn => {const auto enabled=resolve();set_track_ride_fault(c,handle,enabled?TrackRideFault::worn:TrackRideFault::none);}
    case RsseBumpCommand::close_release => {(void)read();close_track_ride(c,handle,true);}
    case RsseBumpCommand::count_active => {const auto destination=read();const auto* ride=track_detail::manager(c).find(handle);output(destination,ride?static_cast<std::int32_t>(track_detail::active_count(*ride)):0);}
    case RsseBumpCommand::attach_queued => {(void)read();script.current_value=attach_queued_track_ride_riders(c,handle);}
    case RsseBumpCommand::set_step_budget => {const auto budget=resolve();set_track_ride_close_budget(c,handle,track_detail::mul(30,budget));script.current_value=budget;}
    case RsseBumpCommand::set_boundary_budget => {const auto budget=resolve();set_track_ride_close_budget(c,handle,fixed::negate(budget));script.current_value=budget;}
    case RsseBumpCommand::remove_vehicle => {(void)read();script.current_value=remove_track_ride_simulation(c,handle,true);}
    case RsseBumpCommand::set_endpoint_closed => {const auto closed=resolve();if(script.endpoint_animation_closed!=closed){script.endpoint_animation_closed=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(closed));endpoint(c,object,!closed,false);endpoint(c,object,!closed,true);}}
    case _ => ;
  }
    // clang-format on
}
} //namespace simcoaster
