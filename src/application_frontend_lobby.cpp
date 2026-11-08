#include "simcoaster/standard_balance.hpp"
#include "application_frontend_internal.hpp"
#include "mesh_traversal.hpp"
#include "simcoaster/mesh_transform.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/sam_record.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char overview_template[] {
#embed "ui_templates/application_overview.bin"
};
static_assert(sizeof(overview_template) == 550);
constexpr std::array<float, 3> angles { 0, 2.093f, 4.186f }; //Original93DC4C.
constexpr float full_turn = 6.283f;
constexpr std::array<std::uint32_t, 3> theme_requests { 308, 302, 305 };
constexpr std::array<std::uint32_t, 15> area_requests { 274, 267, 275, 276, 283, 280, 278, 279, 269, 281, 271, 273, 272, 270, 268 };
//Approved repair: keep locked areas at a stable level within the native64..192
//range, rather than sampling a different brightness from the startup clock.
constexpr std::uint32_t unavailable_level = 96;
constexpr std::uint32_t unavailable_color = unavailable_level | (unavailable_level << 8) | (unavailable_level << 16);
using LobbyBalance = StandardLobbyBalance;

MeshMatrix multiply(const MeshMatrix& a, const MeshMatrix& b) {
    MeshMatrix result {};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            double value {};
            for (std::size_t k = 0; k < 4; ++k) {
                value += double(a[row * 4 + k]) * b[k * 4 + col];
            }
            result[row * 4 + col] = static_cast<float>(value);
        }
    }
    return result;
}

int category_for_mode(std::uint16_t mode) {
    constexpr std::array<int, 3> categories { 1, 2, 0 };
    return mode < 3 ? categories[mode] : 4;
}
} //namespace

void ApplicationFrontendController::State::bind_lobby(bool intro_mode) {
    if (lobby_bound) {
        return;
    }
    if (!c.simulation.current || !c.resources.meshes || !c.simulation.current->saved.lobby) {
        throw std::runtime_error("Original ATM frontend requires saved World lobby ownership");
    }
    auto* header = c.entities.at(c.simulation.current->saved.lobby).payload;
    if (!header || header->kind != ThingKind::park_object) {
        throw std::runtime_error("Original ATM lobby is not a live park object");
    }
    const auto& owner = *reinterpret_cast<ParkObjectRuntimeView*>(header);
    auto* mesh = c.resources.meshes->instance(owner.mesh_instance_id);
    auto* script = find_rsse_script_state_by_id(c, owner.script_id);
    if (!mesh || !script) {
        throw std::runtime_error("Original ATM lobby mesh/program is absent");
    }
    lobby_mesh_slot = owner.mesh_instance_id;
    lobby_mesh_generation = mesh->generation;
    lobby_script_id = owner.script_id;
    auto index = std::optional<std::size_t> { 0 };
    for (std::size_t i = 0; i < 15; ++i) {
        if (!index || *index >= mesh->playback.model.nodes.size()) {
            throw std::runtime_error("Original ATM must retain all15 linked area nodes");
        }
        const auto& node = mesh->playback.model.nodes[*index];
        if (!node.drawable) {
            throw std::runtime_error("Original ATM area has no drawable");
        }
        node_backups.push_back({ *index, node.flags, node.drawable->packed_vertex_colors });
        index = node.next_sibling;
    }
    LobbyBalance balance;
    (void)load_standard_balance_overlays(
        game_root,
        c.source.selected_resource_source_name,
        [&](const auto& file) {
            return c.ports.assets.read_balance_asset(c, file.native_encoded_string());
        },
        [&](std::string_view text, bool preserve, const auto& file) {
            const auto parsed = sam::parse_record<LobbyBalance, true>(balance, text, preserve);
            if (!parsed) {
                throw std::runtime_error("Original ATM AllThemes balance in " + file.native_encoded_string() + " at byte " + std::to_string(parsed.error().offset));
            }
        });
    if (balance.extent != 15) {
        throw std::runtime_error("Original ATM requires all15 AllThemes camera entries");
    }
    for (std::size_t i = 0; i < area_cameras.size(); ++i) {
        const auto& camera = balance.cameras[i];
        area_cameras[i] = { camera.x, camera.y, camera.z, camera.zoom, camera.rotation };
    }
    saved_speed = c.timing.speed_percent;
    saved_scale = c.timing.scale;
    c.timing.speed_percent = 100;
    c.timing.scale = .7;
    c.mode.scheduler = 20;
    angle_tick = fade_tick = 0;
    selected_area = previous_area = -1;
    destination.reset();
    dolly = 0;
    height_offset = -7;
    metric_complete = fade_ready = false;
    directional = intro_mode ? 255 : 136;
    ambient = intro_mode ? 85 : 136;
    spin_origin = angle - .005f;
    if (intro_mode && !suppress_buttons && !restoring) {
        const auto random = static_cast<std::uint32_t>(next_crt_random(c));
        spin_target = angles[random % 3];
    }
    lobby_bound = true;
    //521C90/5BEEC0 publish the selector state read by71BDC0/718190.
    //A separate render-camera override did not bind the Advisor's context.
    c.advisor.presentation.map_source_view = true;
    lobby_intro = intro_mode;
    if (!intro_mode || !intro_sound_active) {
        host.lobby_sound(true, intro_mode);
    }
    intro_sound_active = false;
    if (!intro_mode && mode < 3) {
        map_sound = submit_positioned_template_request(c, 0, c.resources.global_rides_catalog_slot, theme_requests[mode], 0, 500, 0);
    }
    publish_lobby_view();
}

void ApplicationFrontendController::State::restore_nodes() {
    if (c.resources.meshes) {
        auto* mesh = c.resources.meshes->instance(lobby_mesh_slot);
        if (mesh && mesh->generation == lobby_mesh_generation) {
            for (auto& backup : node_backups) {
                if (!backup.modified) {
                    continue;
                }
                if (backup.index >= mesh->playback.model.nodes.size()) {
                    continue;
                }
                auto& node = mesh->playback.model.nodes[backup.index];
                node.flags = (node.flags & ~0x1000000u) | (backup.flags & 0x1000000u);
                if (node.drawable) {
                    node.drawable->packed_vertex_colors = backup.colors;
                }
                backup.modified = false;
            }
        }
    }
}

void ApplicationFrontendController::State::leave_lobby() {
    if (!lobby_bound) {
        if (std::exchange(intro_sound_active, false)) {
            host.lobby_sound(false, true);
        }
        return;
    }
    restore_nodes();
    node_backups.clear();
    if (area_sound) {
        c.ports.resources.request_target_recycle(c, area_sound);
    }
    if (map_sound) {
        c.ports.resources.request_target_recycle(c, map_sound);
    }
    area_sound = map_sound = area_sound_deadline = 0;
    host.lobby_sound(false, lobby_intro);
    if (saved_speed) {
        c.timing.speed_percent = std::clamp<std::int16_t>(saved_speed, 10, 400);
        c.timing.scale = double(c.timing.speed_percent) * .0125;
    }
    saved_speed = 0;
    lobby_bound = false;
    c.advisor.presentation.map_source_view = false;
    c.mode.scheduler = 10;
    host.lobby_view(std::nullopt);
}

void ApplicationFrontendController::State::select_mode(std::uint16_t selected, bool sound) {
    if (selection_locked) {
        return;
    }
    mode = selected;
    if (sound && !suppress_template && mode < 3) {
        if (map_sound) {
            c.ports.resources.submit_resource_target_parameter_request(c, map_sound);
        }
        map_sound = submit_positioned_template_request(c, 0, c.resources.global_rides_catalog_slot, theme_requests[mode], 0, 500, 0);
    }
    if (overview) {
        const auto category = category_for_mode(mode);
        if (category < 3) {
            ui.set_text(required(overview, 90001), theme_name(category));
        }
        constexpr std::array<std::uint32_t, 3> controls { 90009, 90007, 90008 };
        for (std::size_t i = 0; i < controls.size(); ++i) {
            required(overview, controls[i]).color = static_cast<int>(i) == category ? std::array<std::uint8_t, 4> { 255, 0, 0, 255 } : std::array<std::uint8_t, 4> { 255, 255, 255, 255 };
        }
    }
}

void ApplicationFrontendController::State::open_chooser(int source, bool hide_close) {
    if (overview) {
        return;
    }
    //Native521C90 clears existing control feedback before dismissing the UI.
    if (!restoring) {
        c.ports.ui.clear_control_particle_group(c);
    }
    remove(intro);
    remove(profile);
    commands(true);
    suppress_template = false;
    map_view_active = true;
    selection_locked = false;
    constexpr std::array<std::uint16_t, 3> modes { 2, 0, 1 };
    const auto current = host.selected_resource_source();
    if (!restoring && current >= 0 && current < 3) {
        mode = modes[static_cast<std::size_t>(current)];
        //5220C7/5BED50 initialize the angle from the loaded source BEFORE an
        //explicit source argument changes the destination. Navigation then eases.
        angle = angles[mode];
    }
    if (source != 4) {
        if (source < 0 || source > 2) {
            throw std::out_of_range("Original overview resource source");
        }
        mode = modes[static_cast<std::size_t>(source)];
    }
    initial_mode = mode;
    hide_chooser_close = hide_close;
    bind_lobby(false);
    overview = parse(overview_template, 90000);
    phase = ApplicationFrontendPhase::chooser;
    left_repeat = right_repeat = 0;
    for (const auto id : { 90001u, 90007u, 90008u, 90009u, 90010u }) {
        auto& control = required(overview, id);
        caption(control, {}, 6, 1, 1);
        control.color = { 255, 255, 255, 255 };
        control.text_constraints = { true, true };
    }
    for (const auto id : { 90004u, 90005u }) {
        auto& button = required(overview, id);
        button.repeat_interval = std::chrono::milliseconds(200);
        button.repeat_delay = std::chrono::milliseconds(1300);
    }
    ui.set_visible(required(overview, 90002), !hide_close);
    host.persistent_visible(false);
    ui.set_input_root(overview);
    ui.set_focus(&required(overview, 90001));
    refresh_overview();
    select_mode(mode, false);
    if (!restoring) {
        stop_active_advisor_response(c, AdvisorStopTargetAction::parameter_request);
        if (auto* record = advisor()) {
            record->from_ui_panel = 1;
            record->delayed_message = 441;
            record->delay_ticks = c.ports.clock.read_advisor_ui_delay_clock(c);
        }
    }
}

void ApplicationFrontendController::State::refresh_overview() {
    if (!overview) {
        return;
    }
    auto& manager = get_or_create_theme_global_info_manager(c);
    constexpr std::array<std::uint32_t, 3> controls { 90009, 90007, 90008 };
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const auto* info = get_or_create_theme_global_info_for_ordinal(c, manager, static_cast<std::uint32_t>(i));
        const auto name = theme_name(static_cast<int>(i));
        const std::array<BfstWideStringArgument, 1> strings { { { 5, name } } };
        const std::array<BfstSignedIntegerArgument, 2> numbers { { { 22, info->challenges_done }, { 23, 10 } } };
        ui.set_text(required(overview, controls[i]), text(491, strings, numbers));
    }
    auto mission = text(493);
    if (manager.mission_number != -1) {
        const auto ending = manager.mission_research.ending_theme;
        const auto operation = format(table("OPERATION.str"), static_cast<std::uint32_t>(manager.mission_number));
        if (ending >= 0 && ending < 3) {
            const auto theme = theme_name(ending);
            const std::array<BfstWideStringArgument, 2> strings { { { 24, operation }, { 5, theme } } };
            mission = text(492, strings);
        } else {
            //Native5216D0 appends the shared theme-label buffer, not its separately
            //formatted operation, in this invalid-category branch. The last shared
            //theme label from the preceding progress loop is Arabian.
            mission = text(29) + theme_name(0);
        }
    }
    ui.set_text(required(overview, 90010), mission);
}

void ApplicationFrontendController::State::navigate(int delta) {
    if (selection_locked || mode > 2) {
        return;
    }
    const auto next = (static_cast<int>(mode) + delta + 3) % 3;
    select_mode(static_cast<std::uint16_t>(next));
}

void ApplicationFrontendController::State::shade_nodes() {
    restore_nodes();
    if (!c.resources.meshes) {
        return;
    }
    auto* mesh = c.resources.meshes->instance(lobby_mesh_slot);
    if (!mesh || mesh->generation != lobby_mesh_generation) {
        throw std::runtime_error("ATM mesh owner changed during frontend presentation");
    }
    const auto& availability = get_or_create_theme_global_info_manager(c).lobby_area_available_flags;
    const std::size_t group = mode == 0 ? 10 : mode == 1 ? 5 : 0;
    for (std::size_t i = 0; i < node_backups.size(); ++i) {
        auto& node = mesh->playback.model.nodes[node_backups[i].index];
        const bool selected = selected_area >= 0 && i == static_cast<std::size_t>(selected_area);
        const bool closed = mode < 3 && i >= group && i < group + 5 && !availability[i];
        if ((selected || (closed && !(node.flags & 0x1000000u))) && !(node.flags & 0x4000u) && node.drawable) {
            auto& backup = node_backups[i];
            backup.flags = node.flags;
            backup.colors = node.drawable->packed_vertex_colors;
            backup.modified = true;
            node.flags |= 0x1000000u;
            node.drawable->packed_vertex_colors.assign(node.drawable->position_indices.size(), selected ? 0xFFFFFFFFu : unavailable_color);
        }
    }
}

std::int32_t ApplicationFrontendController::State::pick_area(const SceneCamera& camera) {
    const auto pointer = ui.pointer_position();
    if (mode >= 3 || pointer.x < 0 || pointer.y < 0 || pointer.x >= 2048 || pointer.y >= 1536) {
        return -1;
    }
    auto* mesh = c.resources.meshes ? c.resources.meshes->instance(lobby_mesh_slot) : nullptr;
    if (!mesh || mesh->generation != lobby_mesh_generation) {
        throw std::runtime_error("ATM mesh owner changed during frontend selection");
    }
    const auto& model = mesh->playback.model;
    const auto poses = evaluate_mesh_pose(model);
    if (!poses) {
        throw std::runtime_error(describe_mesh_error(poses.error()));
    }
    using Point = std::array<double, 3>;
    const auto cross = [](const Point& a, const Point& b) -> Point {
        return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    };
    const auto dot = [](const Point& a, const Point& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto tangent = std::tan(double(camera.vertical_fov) * .5);
    const Point ray { (double(pointer.x) / 1024. - 1.) * tangent * pixel_width / pixel_height, (1. - double(pointer.y) / 768.) * tangent, 1. };
    const auto view = camera.view();
    auto closest = double(camera.far_plane);
    std::int32_t selected = -1;
    std::vector<std::int32_t> areas(model.nodes.size(), -1);
    for (std::size_t i = 0; i < node_backups.size(); ++i) {
        areas.at(node_backups[i].index) = static_cast<std::int32_t>(i);
    }
    const auto require = [](bool valid) {
        if (!valid) {
            throw std::runtime_error("ATM selection mesh hierarchy is invalid");
        }
    };
    std::vector<std::size_t> pending;
    for (auto root = model.root; root; root = model.nodes[*root].next_sibling) {
        require(*root < model.nodes.size() && pending.size() < model.nodes.size());
        pending.push_back(*root);
    }
    std::reverse(pending.begin(), pending.end());
    std::vector<bool> visited(model.nodes.size());
    std::vector<Point> positions;
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        require(index < model.nodes.size() && !visited[index]);
        visited[index] = true;
        const auto& node = model.nodes[index];
        if (node.drawable && !(node.flags & 0x8210u)) {
            const auto& drawable = *node.drawable;
            const auto transform = multiply((*poses)[index], view);
            positions.clear();
            positions.reserve(drawable.positions.size());
            for (const auto& source : drawable.positions) {
                const auto p = mesh_transform::point(transform, source);
                positions.push_back({ p[0], p[1], p[2] });
            }
            for (const auto& group : drawable.groups) {
                for (std::size_t i = group.first_triangle; i < std::size_t(group.first_triangle) + group.triangle_count; ++i) {
                    const auto& face = drawable.triangles.at(i);
                    std::array<Point, 3> p;
                    for (std::size_t corner = 0; corner < p.size(); ++corner) {
                        p[corner] = positions.at(drawable.position_indices.at(face.corners[corner]));
                    }
                    Point a, b, origin;
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        a[axis] = p[1][axis] - p[0][axis];
                        b[axis] = p[2][axis] - p[0][axis];
                        origin[axis] = -p[0][axis];
                    }
                    const auto normal = cross(ray, b);
                    const auto determinant = dot(a, normal);
                    if (determinant == 0 || !std::isfinite(determinant)) {
                        continue;
                    }
                    const auto u = dot(origin, normal) / determinant;
                    const auto perpendicular = cross(origin, a);
                    const auto v = dot(ray, perpendicular) / determinant;
                    const auto distance = dot(b, perpendicular) / determinant;
                    if (u >= 0 && v >= 0 && u + v <= 1 && distance >= camera.near_plane && distance < closest) {
                        closest = distance;
                        selected = areas[index];
                    }
                }
            }
        }
        if (!(node.flags & 0x20u)) {
            const auto first = pending.size();
            append_mesh_children(model, node, pending, require);
            for (auto i = first; i < pending.size(); ++i) {
                if (areas[pending[i]] < 0) {
                    areas[pending[i]] = areas[index];
                }
            }
        }
    }
    const std::int32_t group = mode == 0 ? 10 : mode == 1 ? 5 : 0;
    return selected >= group && selected < group + 5 ? selected : -1;
}

void ApplicationFrontendController::State::update_area() {
    auto* script = find_rsse_script_state_by_id(c, lobby_script_id);
    if (!script) {
        throw std::runtime_error("ATM program retired during chooser presentation");
    }
    //Native5BD4A0 uses a fixed320x240 mask. Pick the displayed region with
    //the actual camera instead, including ride/building children and occlusion.
    //Publish this frame's rotation before picking, rather than using its old pose.
    selected_area = pick_area(publish_lobby_view());
    const auto now = clock();
    if (selected_area < 0) {
        host.cursor(0);
        if (overview) {
            ui.set_text(required(overview, 90001), theme_name(category_for_mode(mode)));
        }
        if (area_sound_deadline && area_sound_deadline < now) {
            c.ports.resources.request_target_recycle(c, area_sound);
            area_sound_deadline = 0;
        }
        set_rsse_script_state_slot(c, script, 0, -1);
    } else {
        const auto area = static_cast<std::size_t>(selected_area);
        const bool available = get_or_create_theme_global_info_manager(c).lobby_area_available_flags[area] == 1;
        host.cursor(available ? 2 : 8);
        if (overview) {
            ui.set_text(required(overview, 90001), text(area < 5 ? 500 : area < 10 ? 505 : 510) + u"\n" + text(available ? 494 : 495));
        }
        destination = area_cameras[area];
        set_rsse_script_state_slot(c, script, 0, selected_area);
        if (area_sound_deadline < now && selected_area != previous_area) {
            previous_area = selected_area;
            if (area_sound_deadline) {
                c.ports.resources.request_target_recycle(c, area_sound);
            }
            area_sound = submit_positioned_template_request(c, 0, c.resources.global_rides_catalog_slot, area_requests[area], 0, 500, 0);
            area_sound_deadline = now + 1500;
        }
    }
    shade_nodes();
}

void ApplicationFrontendController::State::begin_area() {
    if (selected_area < 0 || !destination) {
        return;
    }
    const auto area = static_cast<std::size_t>(selected_area);
    if (get_or_create_theme_global_info_manager(c).lobby_area_available_flags[area] != 1) {
        return;
    }
    auto* script = find_rsse_script_state_by_id(c, lobby_script_id);
    if (!script) {
        throw std::runtime_error("ATM selection lost its original program");
    }
    (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 31, 0, 500, 0);
    set_rsse_script_state_slot(c, script, 2, 0);
    set_rsse_script_state_slot(c, script, 1, 1);
    set_rsse_script_state_slot(c, script, 0, -1);
    selection_locked = true;
    phase = ApplicationFrontendPhase::area_transition;
}

SceneCamera ApplicationFrontendController::State::publish_lobby_view() {
    auto tilt = mesh_identity_matrix;
    const auto cosine = static_cast<float>(std::cos(-.8)), sine = static_cast<float>(std::sin(-.8));
    tilt[5] = tilt[10] = cosine;
    tilt[6] = -sine;
    tilt[9] = sine;
    auto yaw = mesh_identity_matrix;
    yaw[0] = yaw[10] = static_cast<float>(std::cos(double(angle)));
    yaw[8] = -static_cast<float>(std::sin(double(angle)));
    yaw[2] = -yaw[8];
    auto offset = mesh_identity_matrix;
    offset[14] = -35;
    auto origin = mesh_identity_matrix;
    origin[12] = std::bit_cast<float>(1174849031u);
    origin[13] = std::bit_cast<float>(1133477888u);
    origin[14] = std::bit_cast<float>(1174443878u);
    auto pose = multiply(multiply(multiply(offset, tilt), yaw), origin);
    //Native6E0490 is a plain64-byte copy, not a matrix inverse. These globals
    //contain the camera-to-world basis used by the modern shared camera path.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        pose[12 + axis] += height_offset * pose[4 + axis];
        if (metric_complete) {
            pose[12 + axis] += dolly * pose[8 + axis];
        }
    }
    const auto gray = [](int value) {
        const auto color = static_cast<std::uint32_t>(value);
        return color | (color << 8) | (color << 16);
    };
    return host.lobby_view(ApplicationLobbyView { pose, gray(directional), gray(ambient), mode, selected_area });
}

void ApplicationFrontendController::State::update_lobby() {
    if (!map_view_active) {
        leave_lobby();
        return;
    }
    const auto now = clock();
    const auto delta = angle_tick ? now - angle_tick : 0;
    auto advance_angle = [&](float increment, bool cap) {
        angle_tick = now;
        const auto elapsed = cap ? std::min(delta, 300u) : delta;
        angle = static_cast<float>(double(angle) + double(elapsed) * increment / 25.0);
    };
    if (mode < 3) {
        const auto target = angles[mode];
        if (angle != target) {
            auto difference = double(target) - angle;
            if (difference > 3.141) {
                difference -= 6.283;
            } else if (difference < -3.141) {
                difference += 6.283;
            }
            if (std::abs(difference) < .01 && (suppress_template || std::abs(difference) < .001)) {
                angle = target;
                angle_tick = 0;
            } else {
                advance_angle(static_cast<float>(difference / 8), true);
            }
        }
    } else if (mode == 5) {
        advance_angle(.025f, false);
    } else if (mode == 4) {
        const auto increment = (angle - spin_origin) / 16;
        if (increment < .025f) {
            advance_angle(increment, true);
        } else {
            mode = 5;
            advance_angle(.025f, false);
        }
    } else if (angle != spin_target) {
        const auto remaining = spin_target <= angle ? 6.283 - (double(angle) - spin_target) : double(spin_target) - angle;
        if (mode == 6) {
            if (remaining > 1.5700001) {
                mode = 7;
            }
            advance_angle(.025f, false);
        } else if (std::abs(remaining) < .001) {
            angle = spin_target;
            angle_tick = 0;
        } else {
            advance_angle(static_cast<float>(std::min(remaining / 8, .025)), true);
        }
    }
    if (angle >= full_turn) {
        angle -= full_turn;
    } else if (angle < 0) {
        angle += full_turn;
    }
    if (!selection_locked && !suppress_template) {
        update_area();
        if (std::exchange(area_click, false)) {
            begin_area();
        }
    }
    if (selection_locked) {
        auto* script = find_rsse_script_state_by_id(c, lobby_script_id);
        if (!script) {
            throw std::runtime_error("ATM transition lost its original program");
        }
        if (get_rsse_script_state_slot(c, script, 2)) {
            set_rsse_script_state_slot(c, script, 2, 0);
            if (!destination) {
                throw std::logic_error("ATM transition has no selected native camera");
            }
            auto& camera = c.view.camera;
            camera.focus_position = camera.saved_focus_position = { float(destination->x), float(destination->y), float(destination->z) };
            c.view.mobile.camera_zoom = camera.fallback_zoom = float(destination->zoom);
            camera.heading_radians = camera.fallback_heading_radians = float(destination->rotation);
            camera.render_pose.reset();
            camera.render_base_pose.reset();
            ++camera.render_revision;
            constexpr std::array<int, 3> sources { 1, 2, 0 };
            const auto source = mode < 3 ? sources[mode] : 4;
            cancel_advisor(144);
            cancel_advisor(441);
            remove(overview);
            map_view_active = false;
            phase = ApplicationFrontendPhase::none;
            leave_lobby();
            restore_park_ui();
            //Native5BDB90 compares the source manager that owns this World.
            //Profile activation may already select the destination in the session
            //while the actual lobby World still owns Intro resources.
            if (c.ports.assets.selected_resource_source(c) != source) {
                host.request_source(source, false);
            }
            return;
        }
    }
    if (phase == ApplicationFrontendPhase::profile_transition && !metric_complete && mode < 3 && angle == angles[mode]) {
        cancel_advisor(619);
        metric_complete = true;
        return;
    }
    if (selection_locked || suppress_template) {
        publish_lobby_view();
    }
    if (metric_complete && !fade_ready) {
        const auto elapsed = fade_tick ? now - fade_tick : 0;
        fade_tick = now;
        if (directional || ambient) {
            directional = std::max(0, static_cast<int>(double(directional) - .31 * elapsed));
            ambient = std::max(0, static_cast<int>(double(ambient) - .1 * elapsed));
        }
        if (height_offset > -11) {
            height_offset -= .14f;
        }
        if (dolly < 18) {
            dolly = static_cast<float>(double(dolly) + double(elapsed) * .02);
        } else {
            directional = ambient = 0;
            fade_ready = true;
        }
        if (fade_ready && phase == ApplicationFrontendPhase::profile_transition && !transition_queued) {
            transition_queued = true;
            map_view_active = false;
            suppress_template = false;
            remove(profile);
            commands(true);
            phase = ApplicationFrontendPhase::none;
            leave_lobby();
            host.request_source(profile_source, true);
        }
    }
}

bool ApplicationFrontendController::State::key(std::uint32_t key, bool down, std::uint32_t modifiers) {
    const auto native = key > 255 ? key >> 8 : key;
    if (modifiers) {
        return false;
    }
    if (phase == ApplicationFrontendPhase::intro && native == 27 && down) {
        skip_intro();
        return true;
    }
    if (!overview) {
        return false;
    }
    if (native == 37 || native == 39) {
        auto& held = native == 37 ? left_down : right_down;
        held = down;
        auto& deadline = native == 37 ? left_repeat : right_repeat;
        auto& opposite = native == 37 ? right_repeat : left_repeat;
        auto& button = required(overview, native == 37 ? 90004 : 90005);
        if (button.visual_flags != 2) {
            ui.set_value(button, down ? 1 : 0);
        }
        if (!down) {
            deadline = 0;
            return true;
        }
        const auto now = clock();
        if (deadline < now) {
            deadline = now + 1000;
            opposite = 0;
            navigate(native == 37 ? -1 : 1);
        }
        return true;
    }
    return false;
}
} //namespace simcoaster
