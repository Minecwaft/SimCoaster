#pragma once
#include "simcoaster/world_coaster_runtime.hpp"
#include <algorithm>

namespace simcoaster::coaster_station_detail {
inline float loft_parameter(const CoasterPylonRuntime& pylon, float attachment_y, float root_y) {
    //Both creation476BE0 and restoration4782E0 call476E10. That helper
    //stores the root-relative height in its float RenderVec3, then472840
    //normalizes it. Keep one modern calculation for both reconstruction paths.
    const float relative_height = attachment_y - root_y;
    return static_cast<float>(std::clamp((static_cast<double>(relative_height) - pylon.height_minimum)
                                             / (static_cast<double>(pylon.height_maximum) - pylon.height_minimum),
                                         0.0,
                                         1.0));
}
} //namespace simcoaster::coaster_station_detail
