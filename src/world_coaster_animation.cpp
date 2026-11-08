#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/rsse_actions.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "coaster_editor_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
[[noreturn]] void invalid(std::size_t index, std::string detail) {
    throw MeshError { MeshFailure::configuration, index, "Coaster animation: " + detail, {} };
}

std::int32_t coordinate(float value) {
    if (!std::isfinite(value) || value < std::numeric_limits<std::int32_t>::min() || static_cast<double>(value) > std::numeric_limits<std::int32_t>::max()) {
        invalid(0, "positioned request coordinate");
    }
    return static_cast<std::int32_t>(value);
}

std::pair<float, float> sincos(float angle) {
    return { simulation_sine_at_rounded_index(angle * simulation_sine_samples_per_radian), simulation_sine_at_rounded_index((angle + 1.5707964f) * simulation_sine_samples_per_radian) };
}

struct TrainAnimation final : RideTrainMotionBackend, RideTrainLifecycleBackend {
    AttractionWorld& c;
    WorldMeshRuntime& meshes;
    WorldCoasterBinding& ride;

    TrainAnimation(AttractionWorld& context, WorldMeshRuntime& owner, WorldCoasterBinding& binding) : c(context), meshes(owner), ride(binding) {}

    void register_car_mesh(std::uint32_t slot) override {
        meshes.set_registered(c, slot, true);
    }

    void unregister_car_mesh(std::uint32_t slot) override {
        meshes.set_registered(c, slot, false);
    }

    void reset_non_bend_car_animation(std::uint32_t slot) override {
        const auto reset = meshes.request(c, slot, 0, { { 5, 0 }, 1, 1 });
        if (!reset) {
            throw reset.error();
        }
    }

    void clear_train_rsse(RideTrain& train) override {
        if (!c.construction.action_recording_disabled && c.resources.resource_manager_feature_flag_0400 && c.resources.resource_manager_dispatch_initialized) {
            if (train.positioned_request_a) {
                c.ports.resources.request_target_recycle(c, train.positioned_request_a);
            }
            if (train.positioned_request_b) {
                c.ports.resources.request_target_recycle(c, train.positioned_request_b);
            }
            if (train.passenger_scream_request) {
                c.ports.resources.submit_resource_target_parameter_request(c, train.passenger_scream_request);
            }
        }
        train.rsse_value_slot_5 = train.rsse_value_slot_6 = train.rsse_value_slot_7 = train.rsse_value_slot_8 = 0;
        train.positioned_request_a = train.positioned_request_b = train.passenger_voice_request = train.passenger_scream_request = 0;
        train.flags &= ~std::to_underlying(RideTrainFlag::scream_active);
    }

    void initialize_train_rsse(RideTrain& train) override {
        clear_train_rsse(train);
        if (!c.construction.action_recording_disabled && c.mode.game != GameMode::code_3) {
            train.rsse_value_slot_5 = static_cast<std::uint32_t>(get_rsse_sound_script_slot(c, ride.script_id, 5));
            train.rsse_value_slot_6 = static_cast<std::uint32_t>(get_rsse_sound_script_slot(c, ride.script_id, 6));
            train.rsse_value_slot_7 = static_cast<std::uint32_t>(get_rsse_sound_script_slot(c, ride.script_id, 7));
            train.rsse_value_slot_8 = static_cast<std::uint32_t>(get_rsse_sound_script_slot(c, ride.script_id, 8));
            const auto submit = [&](std::uint32_t slot) {
                return submit_positioned_template_request(c, 0, c.resources.local_rides_catalog_slot, static_cast<std::uint32_t>(get_rsse_sound_script_slot(c, ride.script_id, slot)), coordinate(train.center_position[0]), coordinate(train.center_position[1]), coordinate(train.center_position[2]));
            };
            train.positioned_request_a = submit(0);
            train.positioned_request_b = submit(1);
        }
        set_ride_train_transition(train, RideTrainTransition::winch_slow, 100, c.construction.action_recording_disabled != 0, *this);
    }

    void release_train_passenger_meshes(RideTrain& train, bool second_group) override {
        auto& state = ride.train_state;
        for (std::size_t i = train.lead_car; i < train.lead_car + train.car_count; ++i) {
            auto& car = train.cars.at(i);
            auto& ids = second_group ? car.persons_b : car.persons_a;
            const auto& heads = state.configuration.car_types.at(car.type_index).head_nodes;
            if (ids.size() > heads.size() || std::ranges::count_if(ids, [](auto id) {
                                                 return id != 0;
                                             }) > train.passenger_count) {
                invalid(car.mesh_slot, "passenger ownership");
            }
            for (std::size_t person = 0; person < ids.size(); ++person) {
                if (!ids[person]) {
                    continue; //a previous partial transfer already owns it
                }
                enqueue_completed_ride_exit(state, ids[person]);
                meshes.release_embedded_person(c, car.mesh_slot, heads[person]);
                --train.passenger_count;
                ids[person] = 0;
            }
            ids.clear();
        }
    }

    void populate_passengers(RideTrain& train, std::size_t first, std::size_t count) {
        auto& state = ride.train_state;
        if (first > train.cars.size() || count > train.cars.size() - first) {
            invalid(ride.mesh_slot, "boarding car range");
        }
        while (train.available_passenger_slots && count && !state.queue_a.ids.empty()) {
            const auto person = state.queue_a.pop();
            if (!person) {
                return;
            }
            c.random.mesh_passenger_random_state = 214013u * c.random.mesh_passenger_random_state + 2531011u;
            auto candidate = first + (c.random.mesh_passenger_random_state >> 16) % count;
            std::size_t checked = 0;
            while (train.cars[candidate].persons_b.size() >= train.cars[candidate].passenger_capacity) {
                candidate = first + (candidate - first + 1) % count;
                if (++checked >= count) {
                    break;
                }
            }
            if (checked < count) {
                auto& car = train.cars[candidate];
                const auto& heads = state.configuration.car_types.at(car.type_index).head_nodes;
                if (car.persons_b.size() >= heads.size()) {
                    invalid(car.mesh_slot, "boarding attachment capacity");
                }
                (void)meshes.bind_embedded_person(c, car.mesh_slot, heads[car.persons_b.size()], person);
                car.persons_b.push_back(person);
                ++train.passenger_count;
            }
            --train.available_passenger_slots;
        }
    }

    void update_passengers(RideTrain& train) {
        auto& state = ride.train_state;
        const auto& track = ride.track->track;
        const auto fill = [&](std::size_t first, std::size_t count) {
            train.available_passenger_slots = 0;
            if (first > train.cars.size() || count > train.cars.size() - first) {
                invalid(ride.mesh_slot, "exchange car range");
            }
            for (std::size_t i = first; i < first + count; ++i) {
                const auto& car = train.cars[i];
                if (car.passenger_capacity > car.persons_b.size()) {
                    train.available_passenger_slots += car.passenger_capacity - static_cast<std::uint32_t>(car.persons_b.size());
                }
            }
            populate_passengers(train, first, count);
        };
        //These inequalities test interval overlap, including the car extents.
        //The previous IDB comment incorrectly described complete containment.
        if (train.activation_lower >= track.activation_lower && train.activation_upper <= track.activation_upper) {
            if (!(train.flags & 0x20u)) {
                release_train_passenger_meshes(train, false);
            }
            if (state.detail_state_flags & 4u) {
                std::optional<std::size_t> first, last;
                for (std::size_t i = train.lead_car; i < train.lead_car + train.car_count; ++i) {
                    const auto& car = train.cars.at(i);
                    const auto& type = state.configuration.car_types.at(car.type_index);
                    if (type.front_extent / state.track_sample_spacing + car.track_position >= track.activation_lower && car.track_position - type.rear_extent / state.track_sample_spacing <= track.activation_upper) {
                        if (!first) {
                            first = i;
                        }
                        last = i;
                    }
                }
                //The native no-match branch subtracts a null pointer from the car
                //allocation. Treat that malformed range as empty on the x64 host.
                if (first) {
                    const auto merged_first = std::min(*first, static_cast<std::size_t>(train.first_active_car));
                    const auto merged_last = std::max(*last, static_cast<std::size_t>(train.last_active_car));
                    fill(merged_first, merged_last - merged_first + 1);
                    train.first_active_car = static_cast<std::uint32_t>(*first);
                    train.last_active_car = static_cast<std::uint32_t>(*last);
                }
            }
            train.flags |= 0x20u;
        } else if (train.flags & 0x20u) {
            if (state.detail_state_flags & 4u) {
                if (train.first_active_car > train.car_count) {
                    invalid(ride.mesh_slot, "exchange tail range");
                }
                fill(train.first_active_car, train.car_count - train.first_active_car);
            }
            train.first_active_car = train.last_active_car = 0;
            for (std::size_t i = train.lead_car; i < train.lead_car + train.car_count; ++i) {
                auto& car = train.cars.at(i);
                car.persons_a.swap(car.persons_b);
                car.persons_b.clear();
            }
            train.flags &= ~0x20u;
        }
    }

    bool set_car_pose(RideTrainCar& car, const MeshMatrix& matrix, std::optional<float> divisor) override {
        auto* instance = meshes.instance(car.mesh_slot);
        if (!instance || !instance->playback.model.root) {
            return false;
        }
        instance->playback.model.nodes[*instance->playback.model.root].local_transform = matrix;
        if (divisor) {
            meshes.set_control_rate(car.mesh_slot, 0, *divisor);
        }
        return true;
    }

    void update_bend_car_pose(RideTrainCar& car) override {
        if (!car.mesh_slot) {
            return;
        }
        auto* instance = meshes.instance(car.mesh_slot);
        if (!instance || !ride.track) {
            invalid(car.mesh_slot, "bend mesh owner");
        }
        const auto& type = ride.train_state.configuration.car_types.at(car.type_index);
        const auto& balance = ride.definition->balance.coaster;
        const auto& source = instance->playback.assets->model;
        auto& model = instance->playback.model;
        if (!type.primary_node || *type.primary_node >= model.nodes.size() || *type.primary_node >= source.nodes.size()) {
            invalid(car.mesh_slot, "bend primary node");
        }
        auto& node = model.nodes[*type.primary_node];
        const auto& original = source.nodes[*type.primary_node];
        if (node.flags & 0x200u) {
            return;
        }
        if (!node.drawable || !original.drawable) {
            invalid(car.mesh_slot, "bend drawable");
        }
        const auto pose = build_bend_car_pose(ride.track->track.samples, ride.track->track.reported_sample_count, car, type, ride.train_state.track_sample_spacing, balance.fBendElevationLow, balance.fBendElevationHigh);
        if (!pose) {
            invalid(car.mesh_slot, "bend track samples");
        }
        auto& positions = node.drawable->positions;
        positions = original.drawable->positions;
        for (auto& p : positions) {
            const auto at = (p[2] + static_cast<float>(pose->center) * ride.train_state.track_sample_spacing) / ride.train_state.track_sample_spacing;
            if (!(at >= 0) || at >= static_cast<double>(pose->samples.size() - 1)) {
                invalid(car.mesh_slot, "bend vertex interval");
            }
            const auto i = static_cast<std::size_t>(at);
            const auto f = at - static_cast<float>(i);
            const auto& a = pose->samples[i];
            const auto& b = pose->samples[i + 1];
            p[1] += (1 - f) * a.elevation + f * b.elevation;
            const auto [sp, cp] = sincos((1 - f) * a.pitch + f * b.pitch);
            const auto y = p[1];
            p[1] = y * cp - p[2] * sp;
            p[2] = y * sp + p[2] * cp;
            const auto [sh, ch] = sincos((1 - f) * a.yaw + f * b.yaw);
            const auto x = p[0];
            p[0] = sh * p[2] + ch * x;
            p[2] = ch * p[2] - sh * x;
        }
        node.flags |= 0x200000u | 0x10000u;
        model.flags |= 0x40000u;
    }

    void submit_train_spacing_effect(const RideTrain& train) override {
        if (c.construction.action_recording_disabled) {
            return;
        }
        const auto& track = ride.track->track;
        const auto position = std::fmod(train.activation_lower, track.last_sample_position);
        if (!(position >= 0)) {
            invalid(ride.mesh_slot, "spacing effect coordinate");
        }
        const auto i = static_cast<std::size_t>(position);
        if (i + 1 >= track.samples.size()) {
            invalid(i, "spacing effect sample");
        }
        const auto fraction = position - static_cast<float>(i);
        std::array<std::int32_t, 3> p;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            p[axis] = coordinate(track.samples[i].position[axis] * (1 - fraction) + track.samples[i + 1].position[axis] * fraction);
        }
        (void)submit_positioned_template_request(c, 0, c.resources.global_rides_catalog_slot, 0xCu, p[0], p[1], p[2]);
    }

    std::uint32_t set_rsse_value(std::uint32_t target, std::uint32_t operation, std::int32_t value) override {
        return dispatch_resource_manager_operation(c, target, std::bit_cast<std::int32_t>(operation), value);
    }
};

void animate_textures(WorldCoasterBinding& ride, MeshMilliseconds now) {
    if (!ride.generated_mesh || !ride.definition->balance.mesh_definition) {
        return;
    }
    const auto& definition = *ride.definition->balance.mesh_definition;
    if (now < ride.last_texture_update) {
        invalid(ride.mesh_slot, "texture clock moved backwards");
    }
    auto delta = static_cast<float>(static_cast<double>((now - ride.last_texture_update).count()) * ride.train_state.animation_speed_multiplier * 0.001);
    for (auto& phase : ride.texture_phases) {
        const auto updated = static_cast<double>(phase) + delta;
        phase = static_cast<float>(updated);
        if (updated > 1) {
            const auto cycles = static_cast<float>(std::trunc(updated));
            phase -= cycles;
            delta -= cycles;
        }
    }
    auto& generated = *ride.generated_mesh;
    std::vector<bool> animated(generated.vertices.size());
    for (std::size_t i = 0; i < definition.textures.size(); ++i) {
        if (definition.textures[i].scroll_rate != 0) {
            for (const auto vertex : generated.texture_indices[i]) {
                animated.at(vertex) = true;
            }
        }
    }
    for (std::size_t i = 0; i < animated.size(); ++i) {
        if (animated[i]) {
            generated.vertices[i].uv[1] -= delta;
        }
    }
    ride.last_texture_update = now;
}
} //namespace

void destroy_coaster_train_meshes(AttractionWorld& c, WorldMeshRuntime& meshes, WorldCoasterBinding& ride) {
    TrainAnimation backend { c, meshes, ride };
    for (auto& train : ride.train_state.trains) {
        if ((train.flags & std::to_underlying(RideTrainFlag::resources_active)) && !stop_ride_train_resources(ride.train_state, train, backend)) {
            invalid(ride.mesh_slot, "train destruction layout");
        }
        for (auto& car : train.cars) {
            if (car.mesh_slot) {
                meshes.erase(c, car.mesh_slot);
                car.mesh_slot = 0;
            }
        }
    }
}

std::expected<void, MeshError> regenerate_coaster_record(AttractionWorld& c, WorldMeshRuntime& meshes, std::uint32_t map_id) {
    try {
        auto* world = meshes.coasters();
        auto* ride = world ? world->find_map(map_id) : nullptr;
        if (!ride) {
            invalid(map_id, "record regeneration owner");
        }
        const auto index = static_cast<std::size_t>(ride - world->bindings.data());
        const auto placements = refresh_coaster_placements(c, meshes);
        if (!placements) {
            return placements;
        }
        const auto generated = refresh_coaster_geometry(c, meshes, index);
        if (!generated) {
            return generated;
        }
        auto& state = ride->train_state;
        TrainAnimation backend { c, meshes, *ride };
        if (ride->track && (state.runtime_flags & 0x1000u) && !(state.runtime_flags & 0x4000u)) {
            std::vector<float> speeds;
            speeds.reserve(ride->track->track.samples.size());
            for (const auto& sample : ride->track->track.samples) {
                speeds.push_back(sample.profile_speed);
            }
            if (!reseed_ride_trains_from_profile(state, speeds, ride->track->track.last_sample_position, backend, ride->track->track.reported_sample_count)) {
                invalid(ride->mesh_slot, "record train reseeding");
            }
        }
        //47EEDA clears1000 even when4000 blocks reseeding;481FCB clears4000.
        //67 does not execute475900's latch2000 producer or its train-motion pass.
        state.runtime_flags &= ~(0x1000u | 0x4000u);
        ride->geometry_dirty = false;
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

void reset_all_coaster_train_rsse(AttractionWorld& c, WorldMeshRuntime& meshes) {
    if (auto* world = meshes.coasters()) {
        for (auto& ride : world->bindings) {
            TrainAnimation backend { c, meshes, ride };
            if (ride.train_state.active_train_count > ride.train_state.trains.size()) {
                invalid(ride.mesh_slot, "sound reset active train count");
            }
            for (auto& train : std::span { ride.train_state.trains }.first(ride.train_state.active_train_count)) {
                backend.initialize_train_rsse(train);
            }
        }
    }
}

void transition_coaster_detail_state(AttractionWorld& c, WorldMeshRuntime& meshes, WorldCoasterBinding& ride, RideDetailState request) {
    if (auto* world = meshes.coasters(); world && world->active_edit_transaction) {
        auto& transaction = world->active_edit_transaction->root();
        if (transaction.defer_lifecycle) {
            auto& state = ride.train_state;
            const auto raw = std::to_underlying(request), low = raw & 0xFu, playback = raw & 0x70u;
            transaction.effects.emplace_back(coaster_editor_detail::EditTransaction::LifecycleChange { ride.map_id, request, state.detail_state_flags, state.animation_speed_multiplier, (state.runtime_flags & 0x800u) != 0 });
            //Publish logical state now; native context exit consults it to decide
            //whether admission may reopen. Resource side effects wait for commit.
            if (low) {
                if ((state.detail_state_flags & 0xFu) == 2u && low == 1u) {
                    state.runtime_flags &= ~0x800u;
                } else if ((state.detail_state_flags & 0xFu) == 1u && low == 2u && (state.placement_flags & 4u)) {
                    state.runtime_flags |= 0x800u;
                }
                state.detail_state_flags = (state.detail_state_flags & ~0xFu) | low;
            }
            if (playback) {
                state.detail_state_flags = (state.detail_state_flags & ~0x70u) | playback;
            }
            return;
        }
    }
    auto& state = ride.train_state;
    if (state.active_train_count > state.trains.size()) {
        invalid(ride.mesh_slot, "lifecycle train count");
    }
    TrainAnimation backend { c, meshes, ride };
    const auto raw = std::to_underlying(request), lifecycle = raw & 0xFu;
    if (lifecycle) {
        const auto current = state.detail_state_flags & 0xFu;
        bool drain = false;
        if (current == 1u && lifecycle == 2u) {
            if (!(state.detail_state_flags & 0x40u)) {
                state.animation_speed_multiplier = 1;
            }
            for (std::size_t i = 0; i < state.active_train_count; ++i) {
                if (!initialize_ride_train_resources(state, state.trains[i], backend)) {
                    invalid(ride.mesh_slot, "vehicle activation");
                }
            }
        } else if (current == 2u && lifecycle == 1u) {
            for (std::size_t i = 0; i < state.active_train_count; ++i) {
                if (!stop_ride_train_resources(state, state.trains[i], backend)) {
                    invalid(ride.mesh_slot, "vehicle deactivation");
                }
            }
            state.runtime_flags &= ~0x800u;
        } else if ((current == 4u || current == 8u) && lifecycle == 2u) {
            for (std::size_t i = 0; i < state.active_train_count; ++i) {
                backend.release_train_passenger_meshes(state.trains[i], true);
                backend.release_train_passenger_meshes(state.trains[i], false);
            }
            drain = true;
        }
        if ((current == 1u || current == 2u || current == 4u || current == 8u) && lifecycle == 8u) {
            drain = true;
        }
        if (drain) {
            while (!state.queue_a.ids.empty()) {
                //Secure the new owner before removing A, including allocation failures.
                enqueue_completed_ride_exit(state, state.queue_a.ids.front());
                state.queue_a.pop();
            }
        }
        if ((current == 1u && lifecycle == 2u) || (current == 2u && lifecycle == 1u)) {
            //Both native lifecycle calls pass an allow-registration value whose low
            //bit is zero, so they only unregister the remembered pulsing geometry.
            if (ride.pulsing_geometry_slot) {
                auto* geometry = meshes.instance(*ride.pulsing_geometry_slot);
                if (!geometry) {
                    invalid(*ride.pulsing_geometry_slot, "pulsing geometry owner");
                }
                geometry->playback.runtime_flags &= ~0x4000000u;
                ride.pulsing_geometry_slot.reset();
            }
        }
        state.detail_state_flags = (state.detail_state_flags & ~0xFu) | lifecycle;
    }
    const auto playback = raw & 0x70u, current = state.detail_state_flags & 0x70u;
    if (playback) {
        if ((current == 0x10u || current == 0x20u) && playback == 0x40u) {
            state.animation_speed_multiplier = 0;
        } else if (current == 0x40u && (playback == 0x10u || playback == 0x20u)) {
            resume_ride_train_motion(state);
        }
        state.detail_state_flags = (state.detail_state_flags & ~0x70u) | playback;
    }
}

std::expected<void, MeshError> install_restored_coaster_train_poses(AttractionWorld& c, WorldMeshRuntime& meshes) {
    try {
        auto* world = meshes.coasters();
        if (!world) {
            return {};
        }
        for (auto& ride : world->bindings) {
            if (!ride.track) {
                continue;
            }
            TrainAnimation backend { c, meshes, ride };
            if (!install_ride_train_poses(ride.train_state, ride.track->view(), ride.track->track.last_sample_position, backend)) {
                invalid(ride.mesh_slot, "initial train poses");
            }
            ride.train_state.runtime_flags &= ~(0x1000u | 0x4000u);
            ride.texture_phases.assign(ride.definition->balance.mesh_definition->textures.size(), 0);
            ride.last_texture_update = meshes.clocks.simulation;
            //Seed the native first-update latch without consuming an elapsed frame.
            ride.train_state.last_simulation_time_ms = meshes.clocks.simulation.count();
            ride.train_state.runtime_flags |= 0x10u;
        }
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> advance_coaster_animation(AttractionWorld& c, WorldMeshRuntime& meshes, MeshMilliseconds elapsed) {
    try {
        if (elapsed.count() < 0 || meshes.clocks.simulation.count() > std::numeric_limits<std::int64_t>::max() - elapsed.count() || meshes.clocks.unscaled.count() > std::numeric_limits<std::int64_t>::max() - elapsed.count()) {
            invalid(0, "animation interval");
        }
        meshes.clocks.simulation += elapsed;
        meshes.clocks.unscaled += elapsed;
        meshes.simulation_delta = elapsed;
        return update_coaster_animation(c, meshes);
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> update_coaster_animation(AttractionWorld& c, WorldMeshRuntime& meshes) {
    try {
        auto* world = meshes.coasters();
        if (world) {
            for (std::size_t i = 0; i < world->bindings.size(); ++i) {
                auto& ride = world->bindings[i];
                if (!ride.geometry_dirty) {
                    continue;
                }
                //Native pass one rebuilds every dirty ride before pass two advances any
                //trains. The placement graph and mesh-slot identities remain owned.
                auto& state = ride.train_state;
                //475900 records GENERATETRACK/map-WORD once while latch2000 is clear.
                //Interactive commits clear that latch; loading/raw setters retain it.
                if (!(state.runtime_flags & 0x2000u) && !c.construction.action_recording_disabled && c.construction.action_record_streams_initialized) {
                    if (!c.construction.action_record_stream) {
                        invalid(ride.mesh_slot, "generation action stream");
                    }
                    const std::array event { std::byte { 0x43 } };
                    const auto map = save::detail::encode(static_cast<std::uint16_t>(ride.map_id));
                    (void)c.construction.action_record_stream->write(event);
                    (void)c.construction.action_record_stream->write(map);
                }
                const auto refreshed = refresh_coaster_geometry(c, meshes, i);
                if (!refreshed) {
                    return refreshed;
                }
                ride.geometry_dirty = true;
                TrainAnimation backend { c, meshes, ride };
                if (ride.track) {
                    if ((state.runtime_flags & 0x1000u) && !(state.runtime_flags & 0x4000u)) {
                        std::vector<float> speeds;
                        speeds.reserve(ride.track->track.samples.size());
                        for (const auto& sample : ride.track->track.samples) {
                            speeds.push_back(sample.profile_speed);
                        }
                        if (!reseed_ride_trains_from_profile(state, speeds, ride.track->track.last_sample_position, backend, ride.track->track.reported_sample_count)) {
                            invalid(ride.mesh_slot, "train reseeding");
                        }
                    }
                    if (!install_ride_train_poses(state, ride.track->view(), ride.track->track.last_sample_position, backend)) {
                        invalid(ride.mesh_slot, "rebuilt train poses");
                    }
                    ride.texture_phases.assign(ride.definition->balance.mesh_definition->textures.size(), 0);
                    ride.last_texture_update = meshes.clocks.simulation;
                }
                state.runtime_flags &= ~(0x1000u | 0x4000u);
                state.runtime_flags |= 0x2000u;
                ride.geometry_dirty = false;
            }
        }
        if (world) {
            for (auto& ride : world->bindings) {
                if (ride.track && (ride.train_state.detail_state_flags & 0xEu)) {
                    TrainAnimation backend { c, meshes, ride };
                    if (!update_ride_train_motion(ride.train_state, ride.track->view(), ride.track->motion_settings(ride.speed_cap, c.construction.action_recording_disabled != 0), meshes.clocks.simulation.count(), backend)) {
                        invalid(ride.mesh_slot, "train simulation");
                    }
                    animate_textures(ride, meshes.clocks.simulation);
                    for (std::size_t i = 0; i < ride.train_state.active_train_count; ++i) {
                        if (ride.train_state.trains[i].flags & 2u) {
                            if ((ride.train_state.detail_state_flags & 0xCu) && !(ride.train_state.detail_state_flags & 0x40u)) {
                                backend.update_passengers(ride.train_state.trains[i]);
                            }
                            update_coaster_train_audio(c, meshes, ride, ride.train_state.trains[i]);
                        }
                    }
                }
            }
        }
        const auto updated = meshes.update(c, WorldMeshUpdate::background);
        if (!updated) {
            return updated;
        }
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
