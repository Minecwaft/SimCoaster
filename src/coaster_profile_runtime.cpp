#include "simcoaster/coaster_profile.hpp"
#include "simcoaster/dynamic_texture_balance.hpp"

namespace simcoaster {
bool rebuild_coaster_ride_track(std::span<CoasterGeometryObjectInput> objects, const DynamicTextureMeshBalance& balance, RideTrainState& trains, float cap, RideTrainLifecycleBackend& backend, CoasterRideTrackRuntime& output) {
    const auto settings = coaster_profile_settings(balance);
    CoasterAttachmentRows rows;
    CoasterRideTrackRuntime rebuilt;
    if (!build_coaster_attachment_rows(objects, settings.track_curviness, rows) || !build_coaster_track_samples(objects, std::move(rows), trains.track_sample_spacing, settings.entrance_interval, rebuilt.track) || !rebuild_coaster_speed_profile(objects, rebuilt.track, trains.configuration, settings, trains.track_sample_spacing, cap, rebuilt.profile)) {
        return false;
    }
    rebuilt.use_sample_bank = settings.force_banking;
    if (trains.runtime_flags & 0x1000u) {
        if (!(trains.runtime_flags & 0x4000u)) {
            std::vector<float> speeds;
            speeds.reserve(rebuilt.track.samples.size());
            for (const auto& sample : rebuilt.track.samples) {
                speeds.push_back(sample.profile_speed);
            }
            if (!reseed_ride_trains_from_profile(trains, speeds, rebuilt.track.last_sample_position, backend, rebuilt.track.reported_sample_count)) {
                return false;
            }
        }
        trains.runtime_flags &= ~0x1000u;
    }
    output = std::move(rebuilt);
    return true;
}
} //namespace simcoaster
