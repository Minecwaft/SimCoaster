#pragma once
#include "coaster_geometry.hpp"

namespace simcoaster {
struct DynamicTextureMeshBalance;

struct CoasterRatingSettings {
    float initial_sickness{}, initial_excitement{ 1 };
    float g_force_sickness{ 0.5f }, upside_down_sickness{ 3 }, speed_excitement{ 0.1f }, g_force_excitement{ 1 },
        bank_excitement{ 5 };
    float x_force_discomfort{ 1 }, inverted_discomfort{ 2 };
    float normal_start_delta{ 0.5f }, terror_start_delta{ 1 }, normal_sustain{ 10 }, terror_sustain{ 20 };
};

struct CoasterProfileSettings {
    bool slope_friction{}, force_banking{};
    std::array<float, 3> gravity{ 0, -0.3f, 0 };
    float force_multiplier{ 0.2f };
    float track_curviness{ 1.25f }, entrance_interval{};
    CoasterRatingSettings rating;
};

CoasterProfileSettings coaster_profile_settings(const DynamicTextureMeshBalance&);

struct CoasterProfileResult {
    float travel_seconds{}, end_fade_start{}, maximum_y{}, maximum_speed{}, track_length{}, sickness{}, excitement{};
    std::uint32_t vertical_special_count{};
};

//The modern profile chooses the intended numeric comparisons at the original
//x87 sites whose status bits are undefined. That behavior is deliberate and
//documented in the IDB; the remaining formulas follow the executable.
bool rebuild_coaster_speed_profile(std::span<CoasterGeometryObjectInput>,
                                   CoasterTrackSamples&,
                                   const RideTrainConfiguration&,
                                   const CoasterProfileSettings&,
                                   float spacing,
                                   float speed_cap,
                                   CoasterProfileResult&);

struct CoasterRideTrackRuntime {
    CoasterTrackSamples track;
    CoasterProfileResult profile;
    bool use_sample_bank{};

    RideTrackGeometry view() const {
        return track.view(0, use_sample_bank);
    }

    RideMotionSettings motion_settings(float speed_cap, bool recording_disabled = false) const {
        return { track.last_sample_position,
                 profile.end_fade_start,
                 track.activation_upper,
                 speed_cap,
                 recording_disabled };
    }
};

//Complete transformed-attachment -> component/profile -> train reseeding pass.
//Mesh asset loading and transforming the input attachment nodes remain outside.
bool rebuild_coaster_ride_track(std::span<CoasterGeometryObjectInput>,
                                const DynamicTextureMeshBalance&,
                                RideTrainState&,
                                float speed_cap,
                                RideTrainLifecycleBackend&,
                                CoasterRideTrackRuntime&);
} //namespace simcoaster
