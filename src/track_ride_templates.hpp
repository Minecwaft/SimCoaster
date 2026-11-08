#pragma once
#include "simcoaster/track_ride_restore.hpp"

namespace simcoaster::track_detail {
struct TrackRideTemplate {
    std::int32_t budget, radius, extent;
    std::array<std::int32_t, 4> start, end;
    std::uint32_t limit;
    std::int32_t threshold, first_mesh, mesh_variants;
};

//Complete consumed defaults from Game.exe's 15 rows at 0x9D7300, ordered by
//negative subtype. The remaining native template words are zero.
inline constexpr std::array track_ride_templates { TrackRideTemplate { 4350, 768, 0, { 8, 960, 8, 1000 }, { 12, 1010, 14, 1100 }, 8, 1024, 1, 1 }, TrackRideTemplate { 4350, 768, 0, { 42, 850, 14, 1300 }, { 42, 850, 14, 1300 }, 6, 1024, 0, 1 }, TrackRideTemplate { 4350, 1024, 0, { 10, 940, 6, 1000 }, { 16, 1000, 12, 1100 }, 8, 600, 0, 1 }, TrackRideTemplate { -3, 900, 2048, { 16, 960, 18, 900 }, { 30, 950, 34, 900 }, 8, 2048, 0, 4 },
                                                   TrackRideTemplate { -1, 1024, 1281, { 4, 970, 7, 980 }, { 9, 1000, 14, 980 }, 64, 512, 0, 1 },  TrackRideTemplate { 150, 896, 0, { 10, 940, 6, 1000 }, { 16, 1000, 12, 1100 }, 8, 1024, 0, 1 },  TrackRideTemplate { -3, 900, 2048, { 16, 960, 18, 900 }, { 30, 950, 34, 900 }, 8, 2048, 0, 4 },  TrackRideTemplate { -1, 1024, 1281, { 4, 970, 7, 980 }, { 9, 1000, 14, 980 }, 64, 512, 0, 1 },
                                                   TrackRideTemplate { -3, 900, 2048, { 16, 960, 18, 900 }, { 30, 950, 34, 900 }, 8, 2048, 1, 4 }, TrackRideTemplate { -3, 900, 2048, { 16, 960, 18, 900 }, { 30, 950, 34, 900 }, 8, 2048, 1, 1 },  TrackRideTemplate { 150, 896, 0, { 10, 940, 6, 1000 }, { 16, 1000, 12, 1100 }, 8, 512, 0, 1 },   TrackRideTemplate { -1, 1024, 1281, { 4, 970, 7, 980 }, { 9, 1000, 14, 980 }, 64, 512, 0, 1 },
                                                   TrackRideTemplate { -1, 1024, 1281, { 4, 970, 7, 980 }, { 9, 1000, 14, 980 }, 64, 512, 0, 1 },  TrackRideTemplate { 150, 896, 0, { 10, 940, 6, 1000 }, { 16, 1000, 12, 1100 }, 8, 1024, 0, 1 },  TrackRideTemplate { 150, 1024, 0, { 10, 940, 6, 1000 }, { 16, 1000, 12, 1100 }, 8, 512, 0, 1 } };

inline bool initialize_track_ride_template(TrackRide& ride, std::int32_t tag) {
    if (tag >= 0 || tag < -15) {
        return false;
    }
    const auto& source = track_ride_templates[static_cast<std::size_t>(-tag - 1)];
    ride.template_tag = tag;
    ride.initial_close_budget = source.budget;
    ride.collision_radius = source.radius;
    ride.region_extent = source.extent;
    ride.start_values = source.start;
    ride.end_values = source.end;
    ride.simulation_record_limit = source.limit;
    ride.simulation_target_distance_threshold = source.threshold;
    ride.interpolation_percent = 50;
    ride.first_mesh_template_index = source.first_mesh;
    ride.mesh_template_variant_count = source.mesh_variants;
    return true;
}
} //namespace simcoaster::track_detail
