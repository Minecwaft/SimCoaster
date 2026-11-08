#include "simcoaster/world_camera.hpp"
#include "simcoaster/world_save.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_camera_render.hpp"
#include <bit>
#include <cmath>

namespace simcoaster {
std::uint32_t
fit_world_view_camera_to_coaster_attachment(WorldRuntime& c, const std::array<float, 3>& point, bool skip) {
    if (skip)
        return 0;
    auto& view = c.view.camera;
    const auto width = view.viewport[0], height = view.viewport[1];
    const auto viewport_width = static_cast<float>(width), viewport_height = static_cast<float>(height);
    if (!width || !height)
        throw std::invalid_argument("Coaster camera has no viewport");
    for (auto v : point)
        if (!std::isfinite(v))
            throw std::invalid_argument("Coaster camera attachment is nonfinite");
    std::uint32_t changed = 0;
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        initialize_world_camera_render(c);
        SceneCamera camera;
        apply_world_camera_render(c, camera);
        const auto matrix = camera.view_projection(viewport_width / viewport_height);
        const auto clip = [&](std::size_t axis) {
            return point[0] * matrix[axis]
                 + point[1] * matrix[axis + 4]
                 + point[2] * matrix[axis + 8]
                 + matrix[axis + 12];
        };
        const auto w = clip(3), x = (clip(0) / w + 1.f) * .5f * viewport_width,
                   y = (1.f - clip(1) / w) * .5f * viewport_height;
        const auto pan = [&](double heading, float sign = 1.f) {
            view.focus_position[0] += sign * static_cast<float>(std::sin(heading));
            view.focus_position[2] += sign * static_cast<float>(std::cos(heading));
        };
        if (y < 0) {
            if (c.view.mobile.camera_zoom < 130.f)
                c.view.mobile.camera_zoom += 1.f;
            else
                pan(view.heading_radians);
        }
        if (y > viewport_height)
            pan(view.heading_radians, -1.f);
        if (x > viewport_width)
            pan(view.heading_radians + 3.1415927 * .5);
        if (x < 0)
            pan(view.heading_radians - 3.1415927 * .5);
        if (y >= 0 && y <= viewport_height && x >= 0 && x <= viewport_width)
            break;
        ++changed;
        initialize_world_camera_render(c);
    }
    return changed;
}

namespace {
struct SavedCamera {
    [[=save::Field{ "gf_CameraZoom", 0 }]] float zoom{};
    [[=save::Field{ "gf_YRotation", 1 }]] float heading{};
    [[=save::Field{ "gui_CameraFlags", 2 }]] std::uint32_t flags{};
    [[=save::Field{ "gs_RequiredPOIPosition", 3 }]] std::array<float, 3> focus{};
    [[=save::Field{ "gs_SavedPOIPosition", 4 }]] std::array<float, 3> saved_focus{};
    [[=save::Field{ "gf_SavedYRotation", 5 }]] float saved_heading{};
    [[=save::Field{ "gf_SavedRideXRotation", 6 }]] float saved_ride_x{};
    [[=save::Field{ "gf_SavedRideYRotation", 7 }]] float saved_ride_y{};
};

static_assert(save::record_width<SavedCamera> == 48);
} //namespace

void set_world_view_camera_default_scene_pose(WorldRuntime& c) {
    c.view.camera.heading_radians = c.view.camera.fallback_heading_radians;
    c.view.mobile.camera_zoom = c.view.camera.fallback_zoom;
    c.view.camera.focus_position = { 480.0f, 20.0f, 143.0f };
}

void set_world_view_camera_focus(WorldRuntime& c, std::array<float, 2> focus) {
    auto& camera = c.view.camera;
    camera.damped_pan_x = camera.damped_pan_z = 0;
    camera.focus_position[0] = focus[0];
    camera.focus_position[2] = focus[1];
}

void capture_world_view_camera_interaction_snapshot(WorldRuntime& c) {
    c.view.camera.interaction_snapshot_position = c.view.camera.focus_position;
    c.view.camera.interaction_snapshot_valid = true;
}

void clear_world_view_camera_interaction_snapshot(WorldRuntime& c) {
    c.view.camera.interaction_snapshot_valid = false;
}

namespace {
void focus_object_endpoint(WorldRuntime& c, CellId cell, bool alternate_heading) {
    if (!c.simulation.current || !cell || cell > map_cell_count)
        throw std::out_of_range("Camera endpoint has no live World cell");
    auto& view = c.view.camera;
    const auto index = static_cast<std::uint32_t>(cell) - 1u;
    view.focus_position[0] = static_cast<float>((index & 127u) * 10u + 5u);
    view.focus_position[2] = static_cast<float>((index >> 7) * 10u + 5u);
    match (get_world_cell_direction(&c.simulation.current->map_cells.at(index).base) & 0x55u) {
        case 1 => view.heading_radians = 0;
        case 4 => view.heading_radians = alternate_heading ? 4.712389f : 1.5707964f;
        case 16 => view.heading_radians = 3.1415927f;
        case 64 => view.heading_radians = alternate_heading ? 1.5707964f : 4.712389f;
        case _ => {
            //Native unsupported masks leave the heading unchanged.
        }
    }
}
} //namespace

void focus_world_view_camera_on_park_object_entry(WorldRuntime& c, const ParkObjectRuntimeView* object) {
    if (object)
        focus_object_endpoint(c, object->entry_cell, true);
}

void focus_world_view_camera_on_park_object_exit(WorldRuntime& c,
                                                 const ParkObjectRuntimeView* object,
                                                 bool alternate_heading) {
    if (object)
        focus_object_endpoint(c, object->exit_cell, alternate_heading);
}

void begin_park_object_connection_camera(WorldRuntime& c, const ParkObjectRuntimeView* object) {
    if (static_cast<double>(c.balance.standard_supplement.auto_rotate.zoom_level) <= c.view.mobile.camera_zoom)
        clear_world_view_camera_interaction_snapshot(c);
    else {
        capture_world_view_camera_interaction_snapshot(c);
        focus_world_view_camera_on_park_object_entry(c, object);
    }
}

bool write_camera_save_section(const WorldRuntime& c, Stream& stream) {
    const auto& camera = c.view.camera;
    const SavedCamera saved{ c.view.mobile.camera_zoom,    camera.heading_radians,      c.view.flags,
                             camera.focus_position,        camera.saved_focus_position, camera.saved_heading_radians,
                             camera.saved_ride_x_rotation, camera.saved_ride_y_rotation };
    const auto bytes = save::detail::encode(saved);
    return stream.write(bytes) == static_cast<std::int32_t>(bytes.size());
}

bool restore_camera_save_section(WorldRuntime& c, Stream& stream) {
    std::array<std::byte, save::record_width<SavedCamera>> bytes{};
    if (stream.read(bytes) != static_cast<std::int32_t>(bytes.size()))
        return false;
    SavedCamera saved{};
    save::detail::decode(std::span<const std::byte, save::record_width<SavedCamera>>{ bytes }, saved);
    auto& camera = c.view.camera;
    c.view.mobile.camera_zoom = saved.zoom;
    camera.heading_radians = saved.heading;
    camera.focus_position = saved.focus;
    camera.saved_focus_position = saved.saved_focus;
    camera.saved_heading_radians = saved.saved_heading;
    camera.saved_ride_x_rotation = saved.saved_ride_x;
    camera.saved_ride_y_rotation = saved.saved_ride_y;
    c.view.flags = 0;
    camera.smoothed_heading_radians = camera.heading_radians;
    camera.render_pose.reset();
    camera.render_base_pose.reset();
    ++camera.render_revision;
    if (camera.isometric_mode) {
        constexpr double half_pi = static_cast<double>(1.5707964f), threshold = static_cast<double>(0.7853982f);
        const auto heading = static_cast<double>(camera.heading_radians), remainder = std::fmod(heading, half_pi);
        if (remainder > threshold)
            camera.heading_radians = static_cast<float>(half_pi - remainder + heading);
        else if (remainder < -threshold)
            camera.heading_radians = static_cast<float>(heading - (remainder + half_pi));
        else
            camera.heading_radians = static_cast<float>(heading - remainder);
    }
    const auto width = std::bit_cast<std::int32_t>(c.weather.map_heightfield_width * 10u);
    const auto height = std::bit_cast<std::int32_t>(c.weather.map_heightfield_height * 10u);
    const auto x = camera.focus_position[0], y = camera.focus_position[2];
    //x87's unordered comparison also takes the fallback branch for a NaN POI.
    if (c.ports.assets.selected_resource_source(c) != 3
        && (std::isnan(x)
            || std::isnan(y)
            || x > static_cast<double>(width)
            || y > static_cast<double>(height)
            || x < 0
            || y < 0))
        set_world_view_camera_default_scene_pose(c);
    return true;
}
} //namespace simcoaster
