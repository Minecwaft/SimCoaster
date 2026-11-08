#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace simcoaster {
struct WorldRuntime;

//Native camera command6/18,7/19,0/21,1/22 and World mouse bits1/4.
//In POI these are turn/walk inputs, rather than ordinary orbit zoom.
struct WorldCameraInput {
    bool active {}, rotate_left {}, rotate_right {}, forward {}, backward {};
    bool left_button {}, right_button {};
    std::array<std::int32_t, 2> pointer {};
    std::array<std::uint32_t, 2> viewport { 1100, 760 };
    //A normal-view input owner has already advanced heading this frame.
    //Special-view cleanup still runs when returning from mouse look.
    bool normal_heading_external {};
};

struct WorldCameraInputFeedback {
    bool mouse_look_active {}, recenter_pointer {};
    std::optional<std::array<std::int32_t, 2>> restore_pointer;
};

//The live input owner keeps mouse-look lifetime separate from the cached
//render pose. No pointer query advances movement or attachment history.
class WorldCameraInputRuntime {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldCameraInputRuntime();
    ~WorldCameraInputRuntime();
    WorldCameraInputRuntime(const WorldCameraInputRuntime&) = delete;
    WorldCameraInputRuntime& operator=(const WorldCameraInputRuntime&) = delete;
    void configure_balance(std::string_view, bool preserve);
    WorldCameraInputFeedback advance(WorldRuntime&, const WorldCameraInput&, std::chrono::milliseconds);
};
} //namespace simcoaster
