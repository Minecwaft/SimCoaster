#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace simcoaster {
struct WorldRuntime;
struct ParkObjectRuntimeView;
struct Stream;

struct WorldViewInspectionAttachment {
    std::uint32_t mesh_slot{};
    std::size_t resource_index{};
    std::uint64_t binding_revision{};
    std::array<float, 16> transform{};
};

//Native camera globals gathered into one host state. Zoom and flags stay in
//WorldRuntime's existing render bindings because other subsystems consume them.
struct WorldViewCameraState {
    std::array<std::uint32_t, 2> viewport{ 1100, 760 }; //process-only render dimensions
    float heading_radians{}, smoothed_heading_radians{};
    //465730 initializes LOOK pitch from the zero-valued fallback heading.
    //Orbit pitch is derived separately from zoom in4691B7; it must not tilt
    //the animated attachment used by Intro and ride inspection.
    //Native4A0713 starts with near10. Special-mode entry4672AB uses .1;
    //exit4672F7 restores10. Starting ordinary views at .1 loses depth precision.
    float pitch_radians{}, near_clip_distance{ 10.f }, far_clip_distance{ 1000.f };
    float heading_input{}, pitch_input{};
    float damped_pan_x{}, damped_pan_z{};
    std::array<float, 3> focus_position{}, saved_focus_position{}; //x, height, y
    std::array<float, 3> interaction_snapshot_position{};
    bool interaction_snapshot_valid{};
    float saved_heading_radians{}, saved_ride_x_rotation{}, saved_ride_y_rotation{};
    float fallback_heading_radians{}, fallback_zoom = 110.0f;
    bool isometric_mode{};
    //Native A14654 input bit512; separate from A1465C view-mode flags.
    bool alternate_zoom_limits{};
    //Native A144D8 begins as identity in466DE0 and is replaced by the RAW
    //attachment after each inspection frame. Selection/exit do not reset it.
    std::array<float, 16> previous_inspection_attachment{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    //Process-only presentation cache. All consumers use the same resolved
    //camera-to-world pose; the once-per-frame resolver owns history updates.
    std::optional<std::array<float, 16>> render_pose;
    std::optional<std::array<float, 16>> render_base_pose;
    std::array<float, 3> render_focus_snapshot{};
    float render_heading_snapshot{}, render_pitch_snapshot{};
    std::uint32_t render_flags_snapshot{};
    //Native A146B0 is process history, updated only by normal camera frames.
    //Entering/leaving special views and reading a camera section retain it.
    float normal_terrain_height{}, normal_render_pitch{};
    float render_zoom_snapshot{}, render_smoothed_heading_snapshot{};
    std::array<float, 2> render_pan_snapshot{};
    std::uint64_t render_revision{};
    std::uint64_t render_pose_revision{};
    std::uint32_t render_pose_mode{};
};

void set_world_view_camera_default_scene_pose(WorldRuntime&);
//Supplied focus pair in503530; preserve zoom/heading and clear pan inertia.
void set_world_view_camera_focus(WorldRuntime&, std::array<float, 2>);
//Native46A550/46A590 and4660F0/466250; focus helpers preserve camera height.
void capture_world_view_camera_interaction_snapshot(WorldRuntime&);
void clear_world_view_camera_interaction_snapshot(WorldRuntime&);
void focus_world_view_camera_on_park_object_entry(WorldRuntime&, const ParkObjectRuntimeView*);
void focus_world_view_camera_on_park_object_exit(WorldRuntime&, const ParkObjectRuntimeView*, bool alternate_heading);
void begin_park_object_connection_camera(WorldRuntime&, const ParkObjectRuntimeView*);
//4890B0: fit the pre-update attachment, at most twenty native adjustments.
std::uint32_t
fit_world_view_camera_to_coaster_attachment(WorldRuntime&, const std::array<float, 3>&, bool skip = false);
bool write_camera_save_section(const WorldRuntime&, Stream&);
bool restore_camera_save_section(WorldRuntime&, Stream&);
} //namespace simcoaster
