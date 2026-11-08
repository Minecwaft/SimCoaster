#include "simcoaster/schema_projection.hpp"
#include "simcoaster/save_stream.hpp"
#include "simcoaster/track_ride_restore.hpp"
#include "track_ride_templates.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <span>

namespace simcoaster {
namespace {
struct SavedSimulation {
    [[=save::Field { "flags", 0 }]] std::uint32_t flags {};
    [[=save::Field { "mesh_template_index", 1 }]] std::uint32_t mesh_template_index {};
    [[=save::Field { "mesh_instance_id", 2 }]] std::uint32_t mesh_instance_id {};
    [[=save::Field { "secondary_mesh_instance_id", 3 }]] std::uint32_t secondary_mesh_instance_id {};
    [[=save::Field { "mesh_animation_sequence_id", 4 }]] std::uint32_t mesh_animation_sequence_id {};
    [[=save::Field { "pending_effect_code", 5 }]] std::uint32_t pending_effect_code {};
    [[=save::Field { "queued_effect_code", 6 }]] std::uint32_t queued_effect_code {};
    [[=save::Field { "unknown_1c", 7 }]] std::uint32_t unknown_1c {};
    [[=save::Field { "positioned_sound_request", 8 }]] std::uint32_t positioned_sound_request {};
    [[=save::Field { "particle_model_node_index", 9 }]] std::int32_t particle_model_node_index {};
    [[=save::Field { "model_node_index", 10 }]] std::int32_t model_node_index {};
    [[=save::Field { "particle_runtime_handle", 11 }]] std::uint32_t particle_runtime_handle {};
    [[=save::Field { "saved_rider_pointer", 12 }]] std::uint32_t saved_rider_pointer {};
    [[=save::Field { "position_x", 13 }]] std::int32_t position_x {};
    [[=save::Field { "position_y", 14 }]] std::int32_t position_y {};
    [[=save::Field { "velocity_x", 15 }]] std::int32_t velocity_x {};
    [[=save::Field { "velocity_y", 16 }]] std::int32_t velocity_y {};
    [[=save::Field { "velocity_x_secondary", 17 }]] std::int32_t velocity_x_secondary {};
    [[=save::Field { "velocity_y_secondary", 18 }]] std::int32_t velocity_y_secondary {};
    [[=save::Field { "speed_magnitude", 19 }]] std::int32_t speed_magnitude {};
    [[=save::Field { "steering_heading", 20 }]] std::int32_t steering_heading {};
    [[=save::Field { "heading", 21 }]] std::int32_t heading {};
    [[=save::Field { "desired_heading", 22 }]] std::int32_t desired_heading {};
    [[=save::Field { "angular_velocity", 23 }]] std::int32_t angular_velocity {};
    [[=save::Field { "angular_velocity_change", 24 }]] std::int32_t angular_velocity_change {};
    [[=save::Field { "collision_radius", 25 }]] std::int32_t collision_radius {};
    [[=save::Field { "thrust_variation", 26 }]] std::int32_t thrust_variation {};
    [[=save::Field { "target_x", 27 }]] std::int32_t target_x {};
    [[=save::Field { "target_y", 28 }]] std::int32_t target_y {};
    [[=save::Field { "target_offset_x", 29 }]] std::int32_t target_offset_x {};
    [[=save::Field { "target_offset_y", 30 }]] std::int32_t target_offset_y {};
    [[=save::Field { "selected_track_node_slot", 31 }]] std::int32_t selected_track_node_slot {};
    [[=save::Field { "selected_peer_simulation_pool_index", 32 }]] std::int32_t selected_peer_simulation_pool_index {};
    [[=save::Field { "unknown_motion_0e", 33 }]] std::int32_t unknown_motion_0e {};
    [[=save::Field { "close_budget_remaining", 34 }]] std::int32_t close_budget_remaining {};
    [[=save::Field { "retarget_budget_remaining", 35 }]] std::int32_t retarget_budget_remaining {};
    [[=save::Field { "simulation_tick_count", 36 }]] std::int32_t simulation_tick_count {};
    [[=save::Field { "saved_overlap_pointer", 37 }]] std::uint32_t saved_overlap_pointer {};
    [[=save::Field { "saved_geometry_pointer", 38 }]] std::uint32_t saved_geometry_pointer {};
    [[=save::Field { "saved_owner_pointer", 39 }]] std::uint32_t saved_owner_pointer {};
    [[=save::Field { "render_height", 40 }]] float render_height {};
    [[=save::Field { "boundary_correction_count", 41 }]] std::int32_t boundary_correction_count {};
    [[=save::Field { "boundary_window_steps", 42 }]] std::uint32_t boundary_window_steps {};
};

static_assert(save::record_width<SavedSimulation> == 172);

using schema::copy_matching;

static_assert(schema::matching_fields<TrackRideSimulation, SavedSimulation>.size() == 39);
static_assert(schema::matching_fields<SavedSimulation, TrackRideSimulation>.size() == 39);

class Reader {
    std::span<const std::byte> bytes_;

public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    template <class T>
    bool read(T& value) {
        constexpr auto n = save::value_width<T>;
        if (bytes_.size() < n) {
            return false;
        }
        save::detail::decode(std::span<const std::byte, n> { bytes_.data(), n }, value);
        bytes_ = bytes_.subspan(n);
        return true;
    }
};

bool contains(const TrackRideGeometryRegion& region, std::int32_t x, std::int32_t y) {
    const auto dx = static_cast<std::int64_t>(region.origin_x) - x, dy = static_cast<std::int64_t>(region.origin_y) - y;
    if (!dx && !dy) {
        return true;
    }
    const auto directions = static_cast<std::uint32_t>((dx >= 0 ? 2 : 4) | (dy >= 0 ? 16 : 8));
    if ((region.flags & directions) != directions) {
        return false;
    }
    auto ax = std::abs(dx), ay = std::abs(dy);
    if (region.flags & 32u) {
        return ax <= region.extent_x && ay <= region.extent_y;
    }
    unsigned shift = 0;
    while (ax > 32768 || ay > 32767) {
        ++shift;
        ax >>= 1;
        ay >>= 1;
    }
    const auto distance = static_cast<std::int64_t>(std::sqrt(static_cast<double>(ax * ax + ay * ay))) << shift;
    return distance <= region.extent_x;
}

std::optional<std::size_t> find_region(const TrackRideManager& manager, std::int32_t x, std::int32_t y) {
    for (std::size_t i = 0; i < manager.geometry.size(); ++i) {
        const auto& r = manager.geometry[i];
        if ((r.flags & 1u) && !(r.flags & 64u) && contains(r, x, y)) {
            return i;
        }
    }
    return std::nullopt;
}

bool needs_transition_node(std::int32_t tag) {
    return tag == -13 || tag == -12 || tag == -10 || tag == -9 || tag == -8 || tag == -7 || tag == -5 || tag == -4;
}

bool finish_ride(TrackRide& ride) {
    if (!needs_transition_node(ride.template_tag)) {
        return true;
    }
    auto it = std::find_if(ride.nodes.begin(), ride.nodes.end(), [](const auto& node) {
        return (node.flags & 4u) != 0;
    });
    if (it == ride.nodes.end() || ride.nodes.empty()) {
        return false;
    }
    auto index = static_cast<std::size_t>(it - ride.nodes.begin());
    for (std::size_t i = 0; i < ride.nodes.size(); ++i) {
        index = (index + 1) % ride.nodes.size();
        if (!(ride.nodes[index].flags & 2u)) {
            ride.transition_node = index;
            return true;
        }
    }
    return false;
}
} //namespace

TrackRide* TrackRideManager::find(std::uint32_t handle) {
    const auto slot = handle & 255u;
    if (slot >= rides.size()) {
        return nullptr;
    }
    auto* ride = rides[slot].get();
    return ride && ride->handle == handle ? ride : nullptr;
}

std::optional<std::int32_t> first_free_track_ride_simulation_slot(const TrackRideManager& manager) {
    std::array<bool, 256> occupied {};
    for (const auto& ride : manager.rides) {
        if (ride) {
            for (const auto& sim : ride->simulations) {
                if (sim.flags & 1u) {
                    if (sim.pool_slot < 0 || static_cast<std::size_t>(sim.pool_slot) >= occupied.size() || occupied[static_cast<std::size_t>(sim.pool_slot)]) {
                        return std::nullopt;
                    }
                    occupied[static_cast<std::size_t>(sim.pool_slot)] = true;
                }
            }
        }
    }
    const auto available = std::find(occupied.begin(), occupied.end(), false);
    if (available == occupied.end()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(available - occupied.begin());
}

bool restore_track_ride_save_section(WorldRuntime& c, Stream& stream, TrackRideManager& manager) {
    std::array<std::uint32_t, 3> root {};
    if (!save::read(stream, root) || root[0] != 1 || root[1] != 12 || root[2] < 12 || root[2] > 10000000) {
        return false;
    }
    std::vector<std::byte> data(root[2] - 12);
    if (!data.empty() && stream.read(data) != static_cast<std::int32_t>(data.size())) {
        return false;
    }
    std::size_t offset = 0;
    while (offset < data.size()) {
        if (data.size() - offset < 12) {
            return false;
        }
        std::array<std::uint32_t, 3> header {};
        save::detail::decode(std::span<const std::byte, 12> { data.data() + offset, 12 }, header);
        if (header[1] < 12 || header[2] < header[1] || header[2] > data.size() - offset) {
            return false;
        }
        Reader reader { std::span<const std::byte> { data.data() + offset + 12, header[1] - 12 } };
        offset += header[2];
        if (header[0] == 2) {
            continue;
        }
        std::uint32_t handle {};
        if (!reader.read(handle)) {
            return false;
        }
        if (header[0] == 3) {
            const auto slot = handle & 255u;
            const auto tag = std::bit_cast<std::int32_t>(handle) >> 8;
            if (slot >= manager.rides.size() || tag >= 0 || tag < -15 || manager.rides[slot]) {
                return false;
            }
            auto ride = std::make_unique<TrackRide>();
            ride->handle = handle;
            ride->template_tag = tag;
            if (!track_detail::initialize_track_ride_template(*ride, tag)) {
                return false;
            }
            if (!reader.read(ride->origin_x) || !reader.read(ride->origin_y) || !reader.read(ride->orientation) || !reader.read(ride->object_definition_id) || !reader.read(ride->interpolation_percent) || !reader.read(ride->first_mesh_template_index) || !reader.read(ride->mesh_template_variant_count) || !reader.read(ride->initial_close_budget) || !reader.read(ride->transition_state)) {
                return false;
            }
            ride->interpolation_percent = std::clamp(ride->interpolation_percent, 0, 100);
            for (std::size_t i = 0; i < 4; ++i) {
                ride->current_values[i] = ride->start_values[i] + ride->interpolation_percent * (ride->end_values[i] - ride->start_values[i]) / 100;
            }
            manager.rides[slot] = std::move(ride);
            if (tag == -11) {
                if (!c.resources.object_catalog.shared_ride_description || !build_described_track_ride_geometry(manager, *manager.rides[slot], *c.resources.object_catalog.shared_ride_description)) {
                    return false;
                }
            } else if (!build_standard_track_ride_geometry(manager, *manager.rides[slot])) {
                return false;
            }
            continue;
        }
        auto* ride = manager.find(handle);
        if (!ride) {
            return false;
        }
        (do {
            match (header[0]) {
                case 4 => {
                    TrackRideAuxiliary part {};
                    if (!reader.read(part.flags) || !reader.read(part.x) || !reader.read(part.y)) {
                        return false;
                    }
                    const auto duplicate = std::any_of(ride->auxiliary.begin(), ride->auxiliary.end(), [&](const auto& entry) {
                        return entry.x == part.x && entry.y == part.y;
                    });
                    if (duplicate) {
                        do_return;
                    }
                    if (!add_track_ride_geometry_piece(manager, *ride, part)) {
                        return false;
                    }
                    ride->auxiliary.push_back(part);
                }
                case 5 => {
                    SavedSimulation saved {};
                    std::int32_t fallback_x {}, fallback_y {}, node_x {}, node_y {};
                    std::uint32_t node_owner {};
                    if (!reader.read(saved) || !reader.read(fallback_x) || !reader.read(fallback_y) || !reader.read(node_owner) || !reader.read(node_x) || !reader.read(node_y)) {
                        return false;
                    }
                    TrackRideSimulation sim {};
                    const auto pool_slot = first_free_track_ride_simulation_slot(manager);
                    if (!pool_slot) {
                        return false;
                    }
                    sim.pool_slot = *pool_slot;
                    copy_matching(sim, saved);
                    if (std::isnan(sim.render_height)) {
                        sim.render_height = 0;
                    }
                    sim.active_geometry = find_region(manager, sim.position_x, sim.position_y);
                    if (!sim.active_geometry) {
                        sim.active_geometry = find_region(manager, fallback_x, fallback_y);
                    }
                    const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, static_cast<std::uint16_t>(ride->object_definition_id));
                    if (!definition || sim.mesh_template_index >= definition->additional_mesh_template_ids.size()) {
                        return false;
                    }
                    sim.mesh_instance_id = world_operations::mark_or_find_matching_mesh_instance_for_refresh(c, { sim.mesh_instance_id, std::bit_cast<std::int32_t>(definition->additional_mesh_template_ids[sim.mesh_template_index]), 257u, {}, 0, nullptr, nullptr, nullptr, false });
                    if (sim.flags & 0x2002000u) {
                        sim.secondary_mesh_instance_id = world_operations::mark_or_find_matching_mesh_instance_for_refresh(c, { sim.secondary_mesh_instance_id, std::bit_cast<std::int32_t>(definition->additional_mesh_template_ids[0]), 257u, {}, 0, nullptr, nullptr, nullptr, false });
                    }
                    if (!node_owner) {
                        sim.selected_track_node_slot = -1;
                    } else {
                        const auto owner_slot = node_owner & 255u;
                        if (owner_slot >= manager.rides.size()) {
                            return false;
                        }
                        const auto* owner = manager.rides[owner_slot].get();
                        if (!owner) {
                            sim.selected_track_node_slot = 0;
                        } else {
                            const auto it = std::find_if(owner->nodes.begin(), owner->nodes.end(), [&](const auto& n) {
                                return n.x == node_x && n.y == node_y;
                            });
                            sim.selected_track_node_slot = it == owner->nodes.end() ? 0 : it->pool_slot;
                        }
                    }
                    if (sim.flags & 4u) {
                        sim.retarget_budget_remaining = 0;
                    }
                    ride->simulations.push_back(std::move(sim));
                    if (saved.flags & 0x400000u) {
                        ride->first_simulation = ride->simulations.size() - 1;
                    }
                }
                case 6 => {
                    if (!finish_ride(*ride)) {
                        return false;
                    }
                }
                case 7 or 8 => {
                    std::uint32_t person {};
                    if (!reader.read(person)) {
                        return false;
                    }
                    if (header[0] == 7) {
                        ride->completed_riders.insert(ride->completed_riders.begin(), TrackRideRider { person });
                    } else if (ride->transition_state != 0) {
                        ride->queued_riders.insert(ride->queued_riders.begin(), TrackRideRider { person });
                    }
                }
                case 9 => {
                    std::uint32_t person {};
                    std::int32_t seat {};
                    if (!reader.read(person) || !reader.read(seat) || ride->simulations.empty()) {
                        return false;
                    }
                    auto& sim = ride->simulations.back();
                    const auto model = c.ports.meshes.find_embedded_node_resource_record(c, sim.mesh_instance_id, 128, seat);
                    sim.riders.insert(sim.riders.begin(), TrackRideRider { person, model, seat });
                    ++ride->attached_rider_count;
                }
                case _ => return false;
            }
        });
    }
    return true;
}

namespace {
class ChunkWriter {
public:
    std::vector<std::byte> bytes;

    template <class T>
    void append(const T& value) {
        const auto encoded = save::detail::encode(value);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }

    template <class F>
    void chunk(std::uint32_t kind, F&& write_body) {
        const auto start = bytes.size();
        append(std::array<std::uint32_t, 3> {});
        write_body();
        const auto size = static_cast<std::uint32_t>(bytes.size() - start);
        const auto header = save::detail::encode(std::array<std::uint32_t, 3> { kind, size, size });
        std::copy(header.begin(), header.end(), bytes.begin() + static_cast<std::ptrdiff_t>(start));
    }
};
} //namespace

bool write_track_ride_save_section(Stream& stream, const TrackRideManager& manager) {
    ChunkWriter writer;
    writer.append(std::array<std::uint32_t, 3> {});
    writer.chunk(2, [&] {
        constexpr char stamp[] = "18:34:51Dec 14 2000";
        const auto bytes = std::as_bytes(std::span { stamp });
        writer.bytes.insert(writer.bytes.end(), bytes.begin(), bytes.end());
    });
    std::array<bool, 256> occupied {};
    for (std::size_t slot = 0; slot < manager.rides.size(); ++slot) {
        if (const auto& pointer = manager.rides[slot]) {
            const auto& ride = *pointer;
            if (ride.template_tag >= 0 || ride.template_tag < -15 || ride.handle != ((std::bit_cast<std::uint32_t>(ride.template_tag) << 8) | static_cast<std::uint32_t>(slot))) {
                return false;
            }
            writer.chunk(3, [&] {
                writer.append(ride.handle);
                writer.append(ride.origin_x);
                writer.append(ride.origin_y);
                writer.append(ride.orientation);
                writer.append(ride.object_definition_id);
                writer.append(ride.interpolation_percent);
                writer.append(ride.first_mesh_template_index);
                writer.append(ride.mesh_template_variant_count);
                writer.append(ride.initial_close_budget);
                writer.append(ride.transition_state);
            });
            for (const auto& part : ride.auxiliary) {
                writer.chunk(4, [&] {
                    writer.append(ride.handle);
                    writer.append(part.flags);
                    writer.append(part.x);
                    writer.append(part.y);
                });
            }
            std::vector<const TrackRideSimulation*> simulations;
            for (const auto& sim : ride.simulations) {
                if (sim.flags & 1u) {
                    if (sim.pool_slot < 0 || static_cast<std::size_t>(sim.pool_slot) >= occupied.size() || occupied[static_cast<std::size_t>(sim.pool_slot)]) {
                        return false;
                    }
                    occupied[static_cast<std::size_t>(sim.pool_slot)] = true;
                    simulations.push_back(&sim);
                }
            }
            std::ranges::sort(simulations, {}, [](const auto* sim) {
                return sim->pool_slot;
            });
            for (const auto* sim : simulations) {
                if (!sim->active_geometry || *sim->active_geometry >= manager.geometry.size()) {
                    return false;
                }
                const auto& geometry = manager.geometry[*sim->active_geometry];
                const TrackRideLinkNode* selected = nullptr;
                const TrackRide* owner = nullptr;
                if (sim->selected_track_node_slot != -1) {
                    for (const auto& candidate : manager.rides) {
                        if (candidate) {
                            for (const auto& n : candidate->nodes) {
                                if ((n.flags & 1u) && n.pool_slot == sim->selected_track_node_slot) {
                                    if (selected) {
                                        return false;
                                    }
                                    selected = &n;
                                    owner = candidate.get();
                                }
                            }
                        }
                    }
                    if (!selected) {
                        return false;
                    }
                }
                SavedSimulation saved {};
                copy_matching(saved, *sim);
                writer.chunk(5, [&] {
                    writer.append(ride.handle);
                    writer.append(saved);
                    writer.append(geometry.origin_x);
                    writer.append(geometry.origin_y);
                    writer.append(owner ? owner->handle : 0u);
                    writer.append(selected ? selected->x : 0);
                    writer.append(selected ? selected->y : 0);
                });
                for (const auto& rider : sim->riders) {
                    writer.chunk(9, [&] {
                        writer.append(ride.handle);
                        writer.append(rider.person_id);
                        writer.append(rider.seat_slot);
                    });
                }
            }
            for (const auto& rider : ride.completed_riders) {
                writer.chunk(7, [&] {
                    writer.append(ride.handle);
                    writer.append(rider.person_id);
                });
            }
            for (const auto& rider : ride.queued_riders) {
                writer.chunk(8, [&] {
                    writer.append(ride.handle);
                    writer.append(rider.person_id);
                });
            }
            writer.chunk(6, [&] {
                writer.append(ride.handle);
            });
        }
    }
    if (writer.bytes.size() > 10000000u) {
        return false;
    }
    const auto header = save::detail::encode(std::array<std::uint32_t, 3> { 1, 12, static_cast<std::uint32_t>(writer.bytes.size()) });
    std::copy(header.begin(), header.end(), writer.bytes.begin());
    return stream.write(writer.bytes) == static_cast<std::int32_t>(writer.bytes.size());
}
} //namespace simcoaster
