#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/rsse_actions.hpp"
#include <cmath>
#include <limits>

namespace simcoaster {
namespace {
std::int32_t integer(double value, std::size_t slot) {
    if (!std::isfinite(value) || value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
        throw MeshError { MeshFailure::configuration, slot, "Coaster audio: invalid coordinate/control", {} };
    }
    return static_cast<std::int32_t>(value);
}

std::int32_t nearest(float value, std::size_t slot) {
    //479FF3 and47A256/268/27A are direct FISTP, unlike speed's __ftol.
    const auto lower = std::floor(static_cast<double>(value)), fraction = static_cast<double>(value) - lower;
    return integer(lower + ((fraction > .5 || (fraction == .5 && std::fmod(lower, 2.) != 0)) ? 1. : 0.), slot);
}

void passengers(AttractionWorld& c, WorldCoasterBinding& ride, RideTrain& train, const std::array<std::int32_t, 3>& position) {
    const auto index = nearest(train.distance, ride.mesh_slot);
    if (!ride.track || index < 0 || static_cast<std::size_t>(index) >= ride.track->track.samples.size()) {
        throw MeshError { MeshFailure::configuration, ride.mesh_slot, "Coaster audio: sample outside track", {} };
    }
    const auto flags = ride.track->track.samples[index].sample_flags;
    const bool permitted = !(train.flags & std::to_underlying(RideTrainFlag::speed_fade)) && !(ride.train_state.detail_state_flags & 0x40u);
    constexpr auto latch = std::to_underlying(RideTrainFlag::scream_active);
    const auto move = [&](std::uint32_t request) {
        return request ? c.ports.resources.update_positioned_request_position(c, request, position) : 0;
    };
    //479FE0 has deliberately different recording gates in its two branches.
    if (train.flags & latch) {
        if (flags & 2u) {
            if (train.passenger_voice_request) {
                if (!c.construction.action_recording_disabled) {
                    train.passenger_voice_request = move(train.passenger_voice_request);
                }
            } else if (permitted) {
                train.passenger_voice_request = submit_rsse_passenger_voice(c, 1, 100, 100, position);
            }
        } else if (train.passenger_voice_request) {
            train.passenger_voice_request = move(train.passenger_voice_request);
        }
        if (!(flags & 4u) || !permitted) {
            if (!c.construction.action_recording_disabled && train.passenger_scream_request) {
                c.ports.resources.submit_resource_target_parameter_request(c, train.passenger_scream_request);
            }
            train.passenger_scream_request = 0;
            train.flags &= ~latch;
        } else if (!c.construction.action_recording_disabled) {
            if (train.passenger_scream_request) {
                //Native ignores the move/level return values in this active branch.
                (void)move(train.passenger_scream_request);
                (void)set_rsse_scream_level(c, train.passenger_scream_request, 75, 75);
            } else {
                train.flags &= ~latch; //approved stale-handle repair; retry below
            }
        }
        if (train.flags & latch) {
            return;
        }
        if (!(flags & 4u) || !permitted) {
            return;
        }
    } else if ((flags & 2u) && permitted && !c.construction.action_recording_disabled) {
        train.passenger_voice_request = submit_rsse_passenger_voice(c, 1, 100, 100, position);
    }
    if ((flags & 4u) && permitted) {
        if (!train.passenger_scream_request && !c.construction.action_recording_disabled) {
            train.passenger_scream_request = start_rsse_scream(c, 0, static_cast<std::int32_t>((train.passenger_count >> 1) + 1), 75, 75, position);
        }
        //A failed/suppressed creation cannot leave the native stale latch behind.
        if (train.passenger_scream_request) {
            train.flags |= latch;
        }
    }
}
} //namespace

void update_coaster_train_audio(AttractionWorld& c, WorldMeshRuntime& meshes, WorldCoasterBinding& ride, RideTrain& train) {
    if (train.selected_car >= train.cars.size()) {
        throw MeshError { MeshFailure::configuration, ride.mesh_slot, "Coaster audio: selected car", {} };
    }
    const auto inspected = (c.view.flags & 4u) && meshes.instance(meshes.selected_slot) ? meshes.selected_slot : 0;
    const auto& source = inspected == train.cars[train.selected_car].mesh_slot ? train.lead_position : train.center_position;
    //Approved47A1E0 repair: initialize XYZ even when recording suppresses the
    //engine branch, since the downstream passenger branches have separate gates.
    const std::array position { nearest(source[0], ride.mesh_slot), nearest(source[1], ride.mesh_slot), nearest(source[2], ride.mesh_slot) };
    if (!c.construction.action_recording_disabled) {
        const auto maximum = ride.train_state.configuration.maximum_speed_at_maximum_setting;
        if (!(maximum > 0) || !std::isfinite(maximum)) {
            throw MeshError { MeshFailure::configuration, ride.mesh_slot, "Coaster audio: maximum speed", {} };
        }
        const auto percent = integer(static_cast<double>(train.speed) / maximum * 100., ride.mesh_slot);
        train.positioned_request_a = dispatch_resource_manager_operation(c, train.positioned_request_a, std::bit_cast<std::int32_t>(train.rsse_value_slot_6), percent);
        train.positioned_request_b = dispatch_resource_manager_operation(c, train.positioned_request_b, std::bit_cast<std::int32_t>(train.rsse_value_slot_6), 100 - percent);
        train.positioned_request_a = c.ports.resources.update_positioned_request_position(c, train.positioned_request_a, position);
        train.positioned_request_b = c.ports.resources.update_positioned_request_position(c, train.positioned_request_b, position);
    }
    if (train.passenger_count) {
        passengers(c, ride, train, position);
    }
}
} //namespace simcoaster
