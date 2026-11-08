#include "simcoaster/world_normal_camera_input.hpp"
#include "simcoaster/sam_record.hpp"
#include "simcoaster/world.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster {
namespace {
struct PointerBalance {
    [[=sam::Field { "MouseButtonHeldTime", sam::Kind::integer }]] std::int32_t held_time {};
    [[=sam::Field { "MouseScrollSpeedX", sam::Kind::scalar }]] float speed_x {};
    [[=sam::Field { "MouseScrollSpeedY", sam::Kind::scalar }]] float speed_y {};
};

using EditExtents = StandardCameraEditExtents;

struct Balance {
    [[=sam::Group { "UserInterface" }]] PointerBalance pointer;
    [[=sam::Group { "CoasterEditExtents" }]] EditExtents extents;
};

//Standard.sam's UserInterface group also owns menus, tooltips and heatmap
//colors. The generic known-group reader remains strict within an owned group;
//select this receiver's leaves before applying that reader. Derive selection
//from the same SAM annotations rather than maintaining another key table.
constexpr auto camera_keys = [] consteval {
    constexpr auto& bindings = sam::record_detail::bindings<Balance>;
    std::array<const char*, bindings.size()> result {};
    std::size_t index {};
    for (const auto& binding : bindings) {
        result[index++] = std::define_static_string(std::string { binding.group } + "." + binding.field.name);
    }
    return result;
}();

std::string camera_projection(std::string_view text, bool preserve) {
    std::string selected { text };
    std::array<bool, camera_keys.size()> found {};
    const auto terminated = text.find('\0');
    const auto limit = terminated == text.npos ? text.size() : terminated;
    for (std::size_t line = 0; line < limit;) {
        const auto newline = text.find('\n', line);
        const auto end = newline == text.npos ? limit : std::min(newline, limit);
        auto cursor = line;
        while (cursor < end && sam::detail::space(text[cursor])) {
            ++cursor;
        }
        bool owned {};
        for (std::size_t i = 0; i < camera_keys.size(); ++i) {
            const std::string_view key = camera_keys[i];
            if (text.substr(cursor, end - cursor).starts_with(key) && (cursor + key.size() == end || !sam::detail::alnum(text[cursor + key.size()]))) {
                owned = found[i] = true;
                break;
            }
        }
        if (!owned) {
            for (auto i = line; i < end; ++i) {
                if (selected[i] != '\r') {
                    selected[i] = ' ';
                }
            }
        }
        line = end + (end < limit ? 1 : 0);
    }
    if (!preserve) {
        for (std::size_t i = 0; i < found.size(); ++i) {
            if (!found[i]) {
                throw std::runtime_error("Missing original normal-camera field " + std::string { camera_keys[i] });
            }
        }
    }
    return selected;
}

constexpr float quarter_turn = 1.5707964f;
constexpr float half_quarter_turn = .78539819f;
constexpr float edge = .94999999f;

float axis_speed(double value) {
    if (value < -edge) {
        return static_cast<float>((value + edge) * 10.);
    }
    if (value > edge) {
        return static_cast<float>((value - edge) * 10.);
    }
    return 0;
}

void check_time(std::chrono::milliseconds time) {
    if (time.count() < 0) {
        throw std::invalid_argument("Negative World pointer clock sample");
    }
}
} //namespace

bool apply_world_normal_camera_wheel(WorldRuntime& c, std::int32_t amount) {
    auto& zoom = c.view.mobile.camera_zoom;
    if (!std::isfinite(zoom)) {
        throw std::domain_error("World camera wheel starts at nonfinite zoom");
    }
    zoom += amount >= 0 ? -10.f : 10.f;
    ++c.view.camera.render_revision;
    return true;
}

struct WorldNormalCameraInputRuntime::State {
    Balance balance;
    bool configured {}, right_physical {}, camera_right {}, cancel_pending {};
    bool previous_rotate_left {}, previous_rotate_right {}, heading_snap_pending = true;
    UiPoint press_position;
    std::chrono::milliseconds press_ui {}, release_deadline {};
    std::optional<double> middle_anchor;
    std::uint32_t middle_viewport {};

    void require_balance() const {
        if (!configured) {
            throw std::logic_error("Normal World camera requires original UserInterface balance");
        }
    }

    void promote_cancel(std::chrono::milliseconds now) {
        if (cancel_pending && now - press_ui > std::chrono::milliseconds { 200 }) {
            cancel_pending = false;
            camera_right = true;
        }
    }
};

WorldNormalCameraInputRuntime::WorldNormalCameraInputRuntime() : state_(std::make_unique<State>()) {}

WorldNormalCameraInputRuntime::~WorldNormalCameraInputRuntime() = default;

void WorldNormalCameraInputRuntime::configure_balance(std::string_view text, bool preserve) {
    if (preserve && !state_->configured) {
        throw std::logic_error("Normal World camera overlay has no base balance");
    }
    auto candidate = state_->balance;
    const auto selected = camera_projection(text, preserve);
    const auto parsed = sam::parse_record<Balance, true>(candidate, selected, preserve);
    if (!parsed) {
        throw std::runtime_error("Invalid original normal-camera SAM at byte " + std::to_string(parsed.error().offset));
    }
    if (candidate.pointer.held_time < 0 || !std::isfinite(candidate.pointer.speed_x) || !std::isfinite(candidate.pointer.speed_y) || candidate.extents.min_x > candidate.extents.max_x || candidate.extents.min_z > candidate.extents.max_z) {
        throw std::runtime_error("Invalid original normal-camera balance");
    }
    state_->balance = candidate;
    state_->configured = true;
}

WorldNormalCameraPointerFeedback WorldNormalCameraInputRuntime::reset() {
    auto& s = *state_;
    WorldNormalCameraPointerFeedback result;
    result.release_pointer = s.right_physical || s.middle_anchor.has_value();
    s.right_physical = s.camera_right = s.cancel_pending = false;
    s.previous_rotate_left = s.previous_rotate_right = false;
    s.middle_anchor.reset();
    s.middle_viewport = 0;
    return result;
}

WorldNormalCameraPointerFeedback WorldNormalCameraInputRuntime::pointer(WorldNormalCameraPointerEvent event, const WorldNormalCameraPointerSample& sample, const UiPreferences& options) {
    auto& s = *state_;
    check_time(sample.ui_time);
    check_time(sample.platform_time);
    if (event == WorldNormalCameraPointerEvent::cancel) {
        return reset();
    }
    s.require_balance();
    WorldNormalCameraPointerFeedback result;
    (do {
        match (event) {
            case WorldNormalCameraPointerEvent::middle_down => {
                //The platform event is in the original logical2048-wide UI domain.
                s.middle_anchor = static_cast<double>(sample.logical_position.x) / 2048.;
                s.middle_viewport = 0;
                result.capture_pointer = true;
            }
            case WorldNormalCameraPointerEvent::middle_up => {
                s.middle_anchor.reset();
                s.middle_viewport = 0;
                result.release_pointer = true;
            }
            case WorldNormalCameraPointerEvent::right_down => {
                if (s.right_physical) {
                    do_return;
                }
                if (sample.platform_time.count() > std::numeric_limits<std::int64_t>::max() - s.balance.pointer.held_time) {
                    throw std::overflow_error("World right-button deadline");
                }
                s.right_physical = true;
                s.press_position = sample.logical_position;
                s.press_ui = sample.ui_time;
                s.release_deadline = sample.platform_time + std::chrono::milliseconds { s.balance.pointer.held_time };
                s.cancel_pending = options.rmb_cancel_on && !sample.staff_area_selection;
                s.camera_right = !s.cancel_pending;
                result.capture_pointer = result.handler_right_down = true;
            }
            case WorldNormalCameraPointerEvent::move => {
                if (s.cancel_pending && (std::abs(std::int64_t { sample.logical_position.x } - s.press_position.x) > 8 || std::abs(std::int64_t { sample.logical_position.y } - s.press_position.y) > 8)) {
                    s.cancel_pending = false;
                    s.camera_right = true;
                }
            }
            case WorldNormalCameraPointerEvent::right_up => {
                if (!s.right_physical) {
                    do_return;
                }
                //5070A0 checks the200ms timeout only on periodic event30, not here.
                //Preserve release ordering: reset/cancel the old map handler first,
                //then route this release to the current handler as4D2260 permits.
                result.cancel_interaction = options.rmb_cancel_on && s.cancel_pending;
                result.context_menu_release = true;
                result.handler_right_up = !options.rmb_scroll_on || sample.platform_time <= s.release_deadline;
                result.release_pointer = true;
                s.right_physical = s.camera_right = s.cancel_pending = false;
            }
            case WorldNormalCameraPointerEvent::cancel => {
            }
            case _ => {
            }
        }
    });
    return result;
}

WorldNormalCameraFeedback WorldNormalCameraInputRuntime::advance(WorldRuntime& c, const UiPreferences& options, const WorldNormalCameraInput& input, std::chrono::milliseconds elapsed) {
    auto& s = *state_;
    s.require_balance();
    check_time(input.ui_time);
    if (elapsed.count() < 0 || !input.viewport[0] || !input.viewport[1] || input.viewport[0] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || input.viewport[1] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("Invalid normal World camera frame");
    }
    c.view.camera.viewport = input.viewport;
    WorldNormalCameraFeedback feedback;
    if (!input.active || (c.view.flags & 0x16u)) {
        feedback.release_pointer = reset().release_pointer;
    }
    if (c.view.flags & 0x96u) {
        return feedback; //Special/locked matrix owners retain their own kernel.
    }
    if (!c.construction.active_terrain_model_data || !c.construction.active_terrain_model_data->terrain_grid) {
        throw std::logic_error("Normal World camera has no actual terrain grid");
    }
    const auto& terrain = *c.construction.active_terrain_model_data->terrain_grid;
    if (!terrain.width_cells || !terrain.height_cells || !std::isfinite(terrain.cell_scale_x) || terrain.cell_scale_x <= 0 || !std::isfinite(terrain.cell_scale_y) || terrain.cell_scale_y <= 0) {
        throw std::logic_error("Normal World camera terrain extents are invalid");
    }
    if (options.rmb_cancel_on) {
        s.promote_cancel(input.ui_time);
    }
    auto& view = c.view.camera;
    const auto before_focus = view.focus_position;
    const auto before_heading = view.heading_radians;
    const auto before_smoothed = view.smoothed_heading_radians;
    const auto before_zoom = c.view.mobile.camera_zoom;
    const auto old_x = view.damped_pan_x, old_z = view.damped_pan_z;
    float frame = elapsed.count() >= 200 ? 100.f : static_cast<float>(elapsed.count() + 1);
    const bool isometric = options.isometric_on != 0;
    if (std::exchange(s.heading_snap_pending, false)) {
        //466DE0/46798C: initialization's first update uses1ms and snaps to
        //the closest isometric quarter. Exact ties round toward the zero turn.
        if (isometric) {
            const auto remainder = std::fmod(static_cast<double>(view.heading_radians), static_cast<double>(quarter_turn));
            const auto adjustment = remainder > half_quarter_turn ? quarter_turn - remainder : remainder < -half_quarter_turn ? -(remainder + quarter_turn) : -remainder;
            view.heading_radians = static_cast<float>(view.heading_radians + adjustment);
        }
        frame = 1;
    }
    const bool left = input.active && input.rotate_left;
    const bool right = input.active && input.rotate_right;
    if (input.active && input.middle_button) {
        //Approved replacement for466B40's centre-relative continuous steering:
        //retain its angle scale but apply it only to horizontal drag movement.
        //Neither elapsed time nor render-frame count multiplies a displacement.
        const auto x = static_cast<double>(input.pointer[0]) / input.viewport[0];
        if (s.middle_viewport && s.middle_viewport != input.viewport[0]) {
            s.middle_anchor.reset(); //Resizing cannot become a synthetic drag.
        }
        if (s.middle_anchor) {
            view.heading_radians += static_cast<float>(x - *s.middle_anchor);
        }
        s.middle_anchor = x;
        s.middle_viewport = input.viewport[0];
    } else {
        s.middle_anchor.reset();
        s.middle_viewport = 0;
    }
    if (isometric) {
        if (left && !s.previous_rotate_left) {
            view.heading_radians += quarter_turn;
        }
        if (right && !s.previous_rotate_right) {
            view.heading_radians -= quarter_turn;
        }
    } else {
        view.heading_radians += (static_cast<float>(left) - right) * frame * .0020000001f;
    }
    s.previous_rotate_left = left;
    s.previous_rotate_right = right;
    if (input.active) {
        if (input.zoom_out) {
            c.view.mobile.camera_zoom += frame * .050000001f;
        }
        if (input.zoom_in) {
            c.view.mobile.camera_zoom -= frame * .050000001f;
        }
    }
    if (!std::isfinite(c.view.mobile.camera_zoom) || !std::isfinite(view.heading_radians) || !std::isfinite(view.smoothed_heading_radians) || !std::isfinite(old_x) || !std::isfinite(old_z)) {
        throw std::domain_error("Normal World camera state is nonfinite");
    }
    if (c.view.mobile.camera_zoom < 0) {
        //Several real wheel messages can cross zero before the next displayed
        //frame. Native's late limit check can then feed negative zoom to FSQRT.
        //Keep the original positive overshoot/movement law, but restore an
        //enabled valid range before that otherwise undefined graphics state.
        const auto minimum = view.alternate_zoom_limits ? 40.f : options.minimum_zoom;
        const auto maximum = view.alternate_zoom_limits ? 300.f : options.maximum_zoom;
        if (!input.zoom_limits_enabled || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum < 0 || minimum > maximum) {
            throw std::domain_error("Negative camera zoom without valid enabled limits");
        }
        c.view.mobile.camera_zoom = std::clamp(c.view.mobile.camera_zoom, minimum, maximum);
    }
    //50A210's integer edge-corrected viewport halves are floored for odd
    //resolutions. They are neither the last mouse-down position nor a ray hit.
    const auto half_x = input.viewport[0] / 2u, half_y = input.viewport[1] / 2u;
    if (!half_x || !half_y) {
        throw std::invalid_argument("Normal World camera viewport is too small");
    }
    float pointer_x = static_cast<float>((static_cast<double>(input.pointer[0]) - half_x) / half_x);
    float pointer_z = static_cast<float>(-(static_cast<double>(input.pointer[1]) - half_y) / half_y);
    const bool edge_pan = input.active && input.pointer_inside_world && (pointer_x < -edge || pointer_x > edge || pointer_z < -edge || pointer_z > edge);
    const bool forward = input.active && input.pan_forward;
    const bool backward = input.active && input.pan_backward;
    const bool pan_left = input.active && input.pan_left;
    const bool pan_right = input.active && input.pan_right;
    const bool explicit_pan = forward || backward || pan_left || pan_right;
    const auto blend = std::min(frame / (isometric ? 200.f : 140.f), 1.f);
    view.smoothed_heading_radians = (1.f - blend) * view.smoothed_heading_radians + blend * view.heading_radians;
    if (std::abs(view.smoothed_heading_radians - view.heading_radians) < .0005f) {
        view.smoothed_heading_radians = view.heading_radians;
    }
    const auto decay = frame * .0099999998f + 1.f;
    if (!s.camera_right && !edge_pan && !explicit_pan) {
        view.damped_pan_x = old_x / decay;
        view.damped_pan_z = old_z / decay;
    } else {
        float local_x {}, local_z {};
        if (s.camera_right && options.rmb_scroll_on) {
            //4682F2/468300 both load MouseScrollSpeedX. The parsed Y field is
            //deliberately retained in Balance but the native kernel never reads it.
            local_x = static_cast<float>(s.balance.pointer.speed_x * pointer_x);
            local_z = static_cast<float>(s.balance.pointer.speed_x * pointer_z);
            feedback.close_visitor_locator = true;
        } else {
            if (options.rmb_scroll_on) {
                pointer_x = pointer_z = 0;
            }
            if (forward) {
                pointer_z = 1;
            }
            if (backward) {
                pointer_z = -1;
            }
            if (pan_left) {
                pointer_x = -1;
            }
            if (pan_right) {
                pointer_x = 1;
            }
            local_x = axis_speed(pointer_x);
            local_z = axis_speed(pointer_z);
            feedback.close_visitor_locator = local_x != 0 || local_z != 0;
        }
        if (feedback.close_visitor_locator) {
            c.view.flags &= ~4096u;
        }
        const auto heading = static_cast<double>(view.smoothed_heading_radians);
        const auto sine = std::sin(heading), cosine = std::cos(heading);
        const auto world_x = static_cast<float>(cosine * local_x + sine * local_z);
        const auto world_z = static_cast<float>(cosine * local_z - sine * local_x);
        const auto scale = std::sqrt(c.view.mobile.camera_zoom) * .02f;
        view.damped_pan_x = (scale * world_x + old_x) * .5f;
        view.damped_pan_z = (scale * world_z + old_z) * .5f;
        float min_x {}, min_z {};
        auto max_x = static_cast<float>(terrain.width_cells) * terrain.cell_scale_x;
        auto max_z = static_cast<float>(terrain.height_cells) * terrain.cell_scale_y;
        if (c.mode.game == GameMode::code_3) {
            min_x = static_cast<float>(s.balance.extents.min_x);
            max_x = static_cast<float>(s.balance.extents.max_x);
            min_z = static_cast<float>(s.balance.extents.min_z);
            max_z = static_cast<float>(s.balance.extents.max_z);
        }
        //At an already-exceeded bound, reject only further outward velocity.
        //Native467E20 permits motion back into bounds; it never clamps focus.
        if ((before_focus[0] < min_x && view.damped_pan_x < 0) || (before_focus[0] > max_x && view.damped_pan_x > 0)) {
            view.damped_pan_x = old_x / decay;
        }
        if ((before_focus[2] < min_z && view.damped_pan_z < 0) || (before_focus[2] > max_z && view.damped_pan_z > 0)) {
            view.damped_pan_z = old_z / decay;
        }
    }
    if (std::abs(view.damped_pan_x) < .000099999997f && std::abs(view.damped_pan_z) < .000099999997f) {
        view.damped_pan_x = view.damped_pan_z = 0;
    }
    view.focus_position[0] += view.damped_pan_x * frame;
    view.focus_position[2] += view.damped_pan_z * frame;
    if (input.zoom_limits_enabled) {
        const auto minimum = view.alternate_zoom_limits ? 40.f : options.minimum_zoom;
        const auto maximum = view.alternate_zoom_limits ? 300.f : options.maximum_zoom;
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum) {
            throw std::invalid_argument("Invalid native camera zoom limits");
        }
        c.view.mobile.camera_zoom = std::clamp(c.view.mobile.camera_zoom, minimum, maximum);
    }
    feedback.camera_right_held = s.camera_right;
    feedback.changed = before_focus != view.focus_position || before_heading != view.heading_radians || before_smoothed != view.smoothed_heading_radians || before_zoom != c.view.mobile.camera_zoom || old_x != view.damped_pan_x || old_z != view.damped_pan_z;
    if (feedback.changed) {
        ++view.render_revision;
    }
    return feedback;
}
} //namespace simcoaster
