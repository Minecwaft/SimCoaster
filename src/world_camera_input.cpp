#include "simcoaster/world_camera_input.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/sam_record.hpp"
#include "simcoaster/ui_control.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_navigation.hpp"
#include "simcoaster/world_objects.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
using CameraInputBalance = StandardCameraInputBalance;
//Original data constants9AE014/18/20. Keeping the recovered binary32
//payloads avoids replacing the deliberate inset/tie-breaking quantities.
constexpr auto inside_edge = std::bit_cast<float>(std::uint32_t { 0x411FFBE7 });
constexpr auto diagonal_tie = std::bit_cast<float>(std::uint32_t { 0x3F8147AE });
constexpr auto crossing_epsilon = std::bit_cast<float>(std::uint32_t { 0x3A83126F });

int map_coordinate(float value) {
    const auto scaled = static_cast<double>(value) * .1f;
    if (!std::isfinite(scaled) || scaled < std::numeric_limits<int>::min() || scaled > std::numeric_limits<int>::max()) {
        throw std::domain_error("World camera position is not a finite map coordinate");
    }
    return static_cast<int>(scaled); //Native469F30 truncates toward zero.
}

float edge_inside(int cell, float direction) {
    return static_cast<float>(cell) * 10.f + (direction < 0 ? 0.f : inside_edge);
}

float boundary_fraction(float position, float residual) {
    if (residual == 0) {
        return 2;
    }
    double whole {};
    const auto fraction = std::modf(static_cast<double>(position) * .1f, &whole);
    return static_cast<float>(std::abs((residual < 0 ? fraction : 1. - fraction) / (static_cast<double>(residual) * .1f)));
}

ParkObjectRuntimeView* ride_entry(WorldRuntime& c, float x, float z) {
    //Native469EE0 clamps ONLY the auto-entry lookup. Collision retains the
    //actual source coordinate and separately rejects outward World edges.
    if (!c.weather.map_heightfield_width || !c.weather.map_heightfield_height || c.weather.map_heightfield_width > 128 || c.weather.map_heightfield_height > 128) {
        throw std::logic_error("POI ride entry requires a live terrain grid");
    }
    const auto cx = std::clamp(map_coordinate(x), 0, static_cast<int>(c.weather.map_heightfield_width) - 1);
    const auto cz = std::clamp(map_coordinate(z), 0, static_cast<int>(c.weather.map_heightfield_height) - 1);
    const auto& cell = c.simulation.current->map_cells.at(static_cast<std::size_t>(cz) * 128u + static_cast<std::size_t>(cx));
    if (cell.base.common.type != 9) {
        return nullptr;
    }
    const auto parent = cell.base.common.parent_id;
    if (!parent || parent > map_cell_count) {
        throw std::logic_error("POI ride entrance has no live parent cell");
    }
    auto id = c.simulation.current->map_cells.at(parent - 1u).who;
    std::size_t traversed {};
    while (id) {
        if (++traversed > c.entities.size()) {
            throw std::length_error("Cyclic POI ride entrance object list");
        }
        const auto* record = c.entities.at(id).payload;
        if (!record) {
            throw std::logic_error("POI ride entrance object list has an empty identity");
        }
        auto& base = *reinterpret_cast<const MapObjectBase*>(record);
        if (record->kind == ThingKind::park_object && !record->retirement_pending) {
            auto* object = reinterpret_cast<ParkObjectRuntimeView*>(const_cast<ThingHeader*>(record));
            if (!get_park_object_balance(c, *object).UsageInfo.CannotRide) {
                return object;
            }
        }
        id = base.map_child;
    }
    return nullptr;
}

bool walk_poi(WorldRuntime& c, float dx, float dz) {
    if (std::abs(dx) <= .000099999997f) {
        dx = 0;
    }
    if (std::abs(dz) <= .000099999997f) {
        dz = 0;
    }
    if (!dx && !dz) {
        return false;
    }
    auto& view = c.view.camera;
    //Native468823..46904F is an edge-by-edge residual walk. Each crossed
    //axis must be blocked in BOTH navigation modes0 and1 to stop that axis.
    //A blocked diagonal component can slide along the other component.
    for (std::size_t count = 0; count <= map_cell_count * 2u; ++count) {
        const auto source = view.focus_position;
        const auto cx = map_coordinate(source[0]), cz = map_coordinate(source[2]);
        if (cx < 0 || cz < 0 || cx > 127 || cz > 127) {
            throw std::out_of_range("POI camera walk starts outside the World grid");
        }
        auto tx = boundary_fraction(source[0], dx);
        const auto tz = boundary_fraction(source[2], dz);
        if (tx == tz && dx && dz) {
            dx /= diagonal_tie;
            tx *= diagonal_tie;
        }
        std::array<float, 3> next { source[0], 0, source[2] };
        bool again {};
        const auto blocked = [&](GridPathDirection direction) {
            const GridPathCell position { static_cast<std::uint8_t>(cx), static_cast<std::uint8_t>(cz) };
            return is_world_cell_step_blocked(c, position, direction, 0) && is_world_cell_step_blocked(c, position, direction, 1);
        };
        if (tx < tz && tx < 1) {
            if (blocked(dx < 0 ? GridPathDirection::west : GridPathDirection::east)) {
                next[0] = edge_inside(cx, dx);
                dx = 0;
            } else {
                const auto amount = tx * dx;
                next[0] += amount;
                dx -= amount;
                if (map_coordinate(next[0]) == cx) {
                    next[0] += dx < 0 ? -crossing_epsilon : crossing_epsilon;
                }
            }
            const auto amount = tx * dz;
            next[2] += amount;
            dz -= amount;
            if (map_coordinate(next[2]) != cz) {
                next[2] = edge_inside(cz, dz);
            }
            again = dx != 0 || dz != 0;
        } else if (tz < 1) {
            if (blocked(dz < 0 ? GridPathDirection::north : GridPathDirection::south)) {
                next[2] = edge_inside(cz, dz);
                dz = 0;
            } else {
                const auto amount = tz * dz;
                next[2] += amount;
                dz -= amount;
                if (map_coordinate(next[2]) == cz) {
                    next[2] += dz < 0 ? -crossing_epsilon : crossing_epsilon;
                }
            }
            const auto amount = tz * dx;
            next[0] += amount;
            dx -= amount;
            if (map_coordinate(next[0]) != cx) {
                next[0] = edge_inside(cx, dx);
            }
            again = dx != 0 || dz != 0;
        } else {
            next[0] += dx;
            next[2] += dz;
            if (map_coordinate(next[0]) != cx) {
                next[0] = edge_inside(cx, dx);
            }
            if (map_coordinate(next[2]) != cz) {
                next[2] = edge_inside(cz, dz);
            }
        }
        bool entered {};
        if (auto* object = ride_entry(c, next[0], next[2])) {
            //Native auto-entry deliberately differs from Camera65's explicit
            //ride selection: pass1,0, then452 even when CannotRide was the only
            //admissibility gate. Do not add open/category/upgrade restrictions.
            c.ports.meshes.prepare_park_object_animation(c, object, 1, 0);
            world_operations::queue_scored_advisor_action(c, 452);
            again = false;
            entered = true;
        }
        view.focus_position = next;
        if (!again) {
            return entered;
        }
    }
    throw std::length_error("POI camera residual walk did not converge");
}

bool pointer_can_look(WorldRuntime& c, const WorldCameraInput& input) {
    if (!input.active || c.mode.scheduler == 17 || (c.view.flags & 2048u)) {
        return false;
    }
    const auto* primary = c.ui.persistent_root;
    if (!primary || !primary->runtime) {
        return true;
    }
    const auto& ui = *primary->runtime;
    const UiControl* panel {};
    //50A210 creates primary and secondary as sibling roots in this same
    //UiRuntime. WorldRuntime owns the primary binding; find the genuine39700
    //secondary camera panel through that owner's actual tree, without adding
    //a second borrowed WorldRuntime pointer or guessing a replacement rectangle.
    for (auto* root : ui.roots()) {
        if ((panel = ui.find_recursive(root, 39700))) {
            break;
        }
    }
    if (!panel) {
        return true;
    }
    const UiPoint point { static_cast<std::int32_t>(static_cast<std::int64_t>(input.pointer[0]) * 2048 / input.viewport[0]), static_cast<std::int32_t>(static_cast<std::int64_t>(input.pointer[1]) * 1536 / input.viewport[1]) };
    return !panel->attachment_rect.contains(point);
}
} //namespace

struct WorldCameraInputRuntime::State {
    CameraInputBalance balance;
    bool configured {}, mouse_look {};
    std::array<std::int32_t, 2> anchor {};
};

WorldCameraInputRuntime::WorldCameraInputRuntime() : state_(std::make_unique<State>()) {}

WorldCameraInputRuntime::~WorldCameraInputRuntime() = default;

void WorldCameraInputRuntime::configure_balance(std::string_view text, bool preserve) {
    if (!sam::parse_record<CameraInputBalance, true>(state_->balance, text, preserve)) {
        throw std::runtime_error("Invalid original CoasterEditExtents camera balance");
    }
    state_->configured = true;
}

WorldCameraInputFeedback WorldCameraInputRuntime::advance(WorldRuntime& c, const WorldCameraInput& input, std::chrono::milliseconds elapsed) {
    auto& s = *state_;
    WorldCameraInputFeedback feedback;
    if (input.viewport[0] && input.viewport[1]) {
        c.view.camera.viewport = input.viewport;
    }
    if (elapsed.count() < 0) {
        throw std::invalid_argument("Negative World camera input interval");
    }
    const auto frame = elapsed.count() >= 200 ? 100.f : static_cast<float>(elapsed.count() + 1);
    if (!(c.view.flags & 0x16u) || (c.view.flags & 128u)) {
        if (s.mouse_look) {
            feedback.restore_pointer = s.anchor;
        }
        s.mouse_look = false;
        if (!(c.view.flags & 128u) && !input.normal_heading_external) {
            //Normal keyboard/pan ownership stays with its existing host. Heading
            //presentation is shared here so headless/runtime frames interpolate
            //once, rather than each renderer/pick advancing the native accumulator.
            auto& view = c.view.camera;
            const auto previous = view.smoothed_heading_radians;
            const auto blend = std::min(frame / (view.isometric_mode ? 200.f : 140.f), 1.f);
            view.smoothed_heading_radians = (1.f - blend) * previous + blend * view.heading_radians;
            if (std::abs(view.smoothed_heading_radians - view.heading_radians) < .0005f) {
                view.smoothed_heading_radians = view.heading_radians;
            }
            if (view.smoothed_heading_radians != previous) {
                ++view.render_revision;
            }
        }
        return feedback;
    }
    if (!c.simulation.current || !input.viewport[0] || !input.viewport[1]) {
        throw std::invalid_argument("Special camera requires a World and viewport");
    }
    auto& view = c.view.camera;
    const auto before_position = view.focus_position;
    const auto before_heading = view.heading_radians;
    const auto before_pitch = view.pitch_radians;
    const auto before_smoothed = view.smoothed_heading_radians;
    const bool attached = (c.view.flags & 0x3Cu) != 0;
    float local_forward {};
    if (!attached && input.active) {
        view.heading_radians += (static_cast<float>(input.rotate_right) - input.rotate_left) * frame * .0012000001f;
        local_forward = (static_cast<float>(input.forward) - input.backward) * .1f;
    }
    const bool can_look = pointer_can_look(c, input);
    const bool mouse_look = can_look && input.right_button;
    const bool had_mouse_look = s.mouse_look;
    s.mouse_look = mouse_look;
    feedback.mouse_look_active = mouse_look;
    if (!mouse_look && had_mouse_look) {
        feedback.restore_pointer = s.anchor;
    }
    if (mouse_look) {
        const std::array center { static_cast<std::int32_t>(input.viewport[0] / 2u), static_cast<std::int32_t>(input.viewport[1] / 2u) };
        //Native first-frame reset puts the effective cursor at the center
        //before reading its delta. The saved native anchor is also the center.
        const auto pointer = had_mouse_look ? input.pointer : center;
        view.heading_input = std::min(static_cast<float>(pointer[0] - center[0]) * .1f, 10.f);
        view.pitch_input = -std::min(static_cast<float>(pointer[1] - center[1]) * .125f, 10.f);
        s.anchor = center;
        feedback.recenter_pointer = true;
    } else if (can_look) {
        const auto half_x = static_cast<float>(input.viewport[0]) * .5f;
        const auto half_y = static_cast<float>(input.viewport[1]) * .5f;
        const auto x = (static_cast<float>(input.pointer[0]) - half_x) / half_x;
        const auto y = -(static_cast<float>(input.pointer[1]) - half_y) / half_y;
        //Native4681FD/46824D compares against the recovered -.84/.84
        //constants. Use explicit numeric comparisons, without x87 C0 debt.
        view.heading_input = x < -.84f ? (x - -.40000001f) * 2.f : x > .83999997f ? (x - .40000001f) * 2.f : 0.f;
        view.pitch_input = y < -.84 ? (y - .40000001f) * 1.5f : y > .83999997f ? (y - -.40000001f) * 1.5f : 0.f;
    }
    const auto old_x = view.damped_pan_x, old_z = view.damped_pan_z;
    if (can_look || (input.active && input.right_button)) {
        if (!attached && input.active && input.left_button) {
            local_forward += .1f;
        }
        float world_x {}, world_z = local_forward;
        if (can_look || mouse_look) {
            view.heading_radians += view.heading_input * frame * .0012000001f;
            view.pitch_radians = std::clamp(view.pitch_radians - view.pitch_input * frame * .00019999999f, -1.6f, 1.6f);
            world_x = std::sin(view.heading_radians) * local_forward;
            world_z = std::cos(view.heading_radians) * local_forward;
        }
        view.smoothed_heading_radians = view.heading_radians;
        if (!(c.view.mobile.camera_zoom >= 0) || !std::isfinite(c.view.mobile.camera_zoom)) {
            throw std::domain_error("Special camera zoom is not finite/nonnegative");
        }
        const auto scale = std::sqrt(c.view.mobile.camera_zoom) * .02f;
        view.damped_pan_x = (scale * world_x + old_x) * .5f;
        view.damped_pan_z = (scale * world_z + old_z) * .5f;
        float min_x {}, min_z {};
        auto max_x = static_cast<float>(c.weather.map_heightfield_width) * 10.f;
        auto max_z = static_cast<float>(c.weather.map_heightfield_height) * 10.f;
        if (c.mode.game == GameMode::code_3) {
            if (!s.configured) {
                throw std::logic_error("Coaster camera edit extents were not configured");
            }
            const auto& bounds = s.balance.extents;
            min_x = static_cast<float>(bounds.min_x);
            min_z = static_cast<float>(bounds.min_z);
            max_x = static_cast<float>(bounds.max_x);
            max_z = static_cast<float>(bounds.max_z);
        }
        if ((before_position[0] < min_x && view.damped_pan_x < 0) || (before_position[0] > max_x && view.damped_pan_x > 0)) {
            view.damped_pan_x = old_x / (frame * .01f + 1.f);
        }
        if ((before_position[2] < min_z && view.damped_pan_z < 0) || (before_position[2] > max_z && view.damped_pan_z > 0)) {
            view.damped_pan_z = old_z / (frame * .01f + 1.f);
        }
    } else {
        view.damped_pan_x = old_x / (frame * .01f + 1.f);
        view.damped_pan_z = old_z / (frame * .01f + 1.f);
        const auto blend = std::min(frame / (view.isometric_mode ? 200.f : 140.f), 1.f);
        view.smoothed_heading_radians = (1.f - blend) * view.smoothed_heading_radians + blend * view.heading_radians;
        if (std::abs(view.smoothed_heading_radians - view.heading_radians) < .0005f) {
            view.smoothed_heading_radians = view.heading_radians;
        }
    }
    if (std::abs(view.damped_pan_x) < .0001f && std::abs(view.damped_pan_z) < .0001f) {
        view.damped_pan_x = view.damped_pan_z = 0;
    }
    if (attached) {
        view.focus_position[0] += view.damped_pan_x * frame;
        view.focus_position[2] += view.damped_pan_z * frame;
    } else {
        (void)walk_poi(c, view.damped_pan_x * frame, view.damped_pan_z * frame);
    }
    if (view.focus_position != before_position || view.heading_radians != before_heading || view.pitch_radians != before_pitch || view.smoothed_heading_radians != before_smoothed) {
        ++view.render_revision;
    }
    return feedback;
}
} //namespace simcoaster
