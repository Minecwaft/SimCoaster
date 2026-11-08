#include "simcoaster/world_camera_render.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "simcoaster/world.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
void require_finite(const MeshMatrix& matrix) {
    if (!std::ranges::all_of(matrix, [](float value) {
            return std::isfinite(value);
        }))
        throw std::domain_error("World camera attachment contains non-finite coordinates");
}

//Native4DB380 uses exact trigonometry for POI. Native4695AE instead starts
//with identity and calls4DAB70 XY, whose sine lookup spills/scales floats
//before nearest-even indexing. Both use4DA8F0's row-basis Y rotation.
MeshMatrix xy_basis(float pitch, float heading, bool lookup) {
    if (!std::isfinite(pitch) || !std::isfinite(heading))
        throw std::domain_error("World camera look angles are non-finite");
    const auto sincos = [lookup](float angle) {
        if (lookup)
            return std::pair{ simulation_sine_at_rounded_index(angle * simulation_sine_samples_per_radian),
                              simulation_sine_at_rounded_index(static_cast<float>(
                                  (static_cast<double>(angle) + 1.5707964f) * simulation_sine_samples_per_radian)) };
        return std::pair{ static_cast<float>(std::sin(static_cast<double>(angle))),
                          static_cast<float>(std::cos(static_cast<double>(angle))) };
    };
    const auto [sx, cx] = sincos(pitch);
    const auto [sy, cy] = sincos(heading);
    MeshMatrix matrix{ 1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1 };
    for (std::size_t row = 0; row < 3; ++row) {
        const auto x = matrix[row * 4], z = matrix[row * 4 + 2];
        matrix[row * 4] = static_cast<float>(static_cast<double>(sy) * z + static_cast<double>(cy) * x);
        matrix[row * 4 + 2] = static_cast<float>(static_cast<double>(cy) * z - static_cast<double>(sy) * x);
    }
    return matrix;
}

MeshMatrix premultiply_basis(const MeshMatrix& left, const MeshMatrix& right) {
    auto result = right;
    //4DBFC0 preserves the destination's projective column. Its left look
    //transform has zero translation, so the current attachment origin stays.
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column) {
            double value{};
            for (std::size_t k = 0; k < 3; ++k)
                value += static_cast<double>(left[row * 4 + k]) * right[k * 4 + column];
            result[row * 4 + column] = static_cast<float>(value);
        }
    return result;
}

void publish_pose(WorldRuntime& c, MeshMatrix pose) {
    auto& state = c.view.camera;
    require_finite(pose);
    state.render_pose = std::move(pose);
    state.render_pose_mode = c.view.flags & 0x16u;
    state.render_pose_revision = state.render_revision;
    state.render_focus_snapshot = state.focus_position;
    state.render_heading_snapshot = state.heading_radians;
    state.render_pitch_snapshot = state.pitch_radians;
    state.render_flags_snapshot = c.view.flags;
    state.render_zoom_snapshot = c.view.mobile.camera_zoom;
    state.render_smoothed_heading_snapshot = state.smoothed_heading_radians;
    state.render_pan_snapshot = { state.damped_pan_x, state.damped_pan_z };
}

bool cache_current(const WorldRuntime& c) {
    const auto& s = c.view.camera;
    return s.render_pose
        && s.render_pose_mode == (c.view.flags & 0x16u)
        && s.render_pose_revision == s.render_revision
        && s.render_focus_snapshot == s.focus_position
        && s.render_heading_snapshot == s.heading_radians
        && s.render_smoothed_heading_snapshot == s.smoothed_heading_radians
        && s.render_pitch_snapshot == s.pitch_radians
        && s.render_flags_snapshot == c.view.flags
        && s.render_zoom_snapshot == c.view.mobile.camera_zoom
        && s.render_pan_snapshot == std::array{ s.damped_pan_x, s.damped_pan_z };
}

MeshMatrix normal_world_pose(WorldRuntime& c, bool advance_history) {
    auto& state = c.view.camera;
    const auto zoom = c.view.mobile.camera_zoom;
    if (!std::isfinite(zoom) || zoom < 0 || !std::isfinite(state.damped_pan_x) || !std::isfinite(state.damped_pan_z))
        throw std::domain_error("World camera orbit inputs are not finite");
    //4691B7..469247: native render pitch is a clamped zoom-derived value,
    //independent of the look pitch retained for POI/ride inspection.
    const auto proportion = (static_cast<double>(zoom) - 70.) * .016666668f;
    const auto pitch =
        std::clamp(static_cast<float>((1. - proportion) * .78539819f + proportion * 1.134464f), 0.f, 1.4707963f);
    state.normal_render_pitch = pitch;
    auto pose = xy_basis(pitch, state.smoothed_heading_radians, false);
    //469269..469381: exact ordered ZX basis rotation. Preserve the native
    //second-axis sign/clamp even though it yields zero for finite velocities.
    const auto bank_z_magnitude =
        std::sqrt(std::abs(static_cast<double>(state.damped_pan_x)) * 100.) * .0049999999f - .001f;
    const auto bank_z = static_cast<float>(std::copysign(std::max(bank_z_magnitude, 0.), state.damped_pan_x));
    const auto bank_x_raw = std::sqrt(std::abs(static_cast<double>(state.damped_pan_z)) * 100.) * -.0049999999f - .001f;
    const auto bank_x =
        static_cast<float>(state.damped_pan_z < 0 ? std::min(-bank_x_raw, 0.) : std::max(bank_x_raw, 0.));
    const auto sz = static_cast<float>(std::sin(static_cast<double>(bank_z)));
    const auto cz = static_cast<float>(std::cos(static_cast<double>(bank_z)));
    const auto sx = static_cast<float>(std::sin(static_cast<double>(bank_x)));
    const auto cx = static_cast<float>(std::cos(static_cast<double>(bank_x)));
    for (std::size_t row = 0; row < 3; ++row) {
        const auto x = pose[row * 4], y = pose[row * 4 + 1];
        pose[row * 4] = static_cast<float>(static_cast<double>(x) * cz - static_cast<double>(y) * sz);
        pose[row * 4 + 1] = static_cast<float>(static_cast<double>(y) * cz + static_cast<double>(x) * sz);
        const auto rotated_y = pose[row * 4 + 1], z = pose[row * 4 + 2];
        pose[row * 4 + 1] = static_cast<float>(static_cast<double>(rotated_y) * cx - static_cast<double>(z) * sx);
        pose[row * 4 + 2] = static_cast<float>(static_cast<double>(z) * cx + static_cast<double>(rotated_y) * sx);
    }
    //flt_A146B0 is a persistent zero-initialized terrain filter. It advances
    //only on normal displayed frames, never from render/pick/audio queries.
    if (advance_history)
        state.normal_terrain_height = static_cast<float>(
            (world_operations::sample_terrain_surface_height(c, state.focus_position[0], state.focus_position[2])
             + static_cast<double>(state.normal_terrain_height))
            * .5);
    const auto offset_y = static_cast<float>(std::sin(static_cast<double>(pitch)) * zoom);
    const auto offset_z = static_cast<float>(std::cos(static_cast<double>(pitch))) * -zoom;
    const auto heading = static_cast<double>(state.smoothed_heading_radians);
    const auto offset_x = static_cast<float>(std::sin(heading) * offset_z);
    const auto rotated_z = static_cast<float>(std::cos(heading) * offset_z);
    pose[12] = state.focus_position[0] + offset_x;
    pose[13] =
        static_cast<float>(static_cast<double>(state.focus_position[1]) + state.normal_terrain_height + offset_y);
    pose[14] = state.focus_position[2] + rotated_z;
    return pose;
}

MeshMatrix apply_attachment_look(const WorldRuntime& c, MeshMatrix pose) {
    if (!(c.view.flags & 0x800u)) {
        const auto& state = c.view.camera;
        pose = premultiply_basis(xy_basis(state.pitch_radians, state.smoothed_heading_radians, true), pose);
    }
    return pose;
}

void resolve(WorldRuntime& c, std::chrono::milliseconds elapsed, bool advance_history) {
    auto& state = c.view.camera;
    auto mode = c.view.flags & 0x16u;
    if (elapsed.count() < 0)
        throw std::invalid_argument("World camera frame interval is negative");
    if (!mode) {
        state.render_base_pose.reset();
        publish_pose(c, normal_world_pose(c, advance_history));
        return;
    }
    MeshMatrix pose;
    if (c.view.flags & 0x3Cu) {
        const auto attachment = c.ports.camera.world_view_inspection_attachment(c);
        //Resolving a retired selected attachment can perform its native mode
        //exit. Re-read flags before requiring any attachment or publishing art.
        mode = c.view.flags & 0x16u;
        if (!mode) {
            state.render_base_pose.reset();
            publish_pose(c, normal_world_pose(c, advance_history));
            return;
        }
        if (!(c.view.flags & 0x3Cu)) {
            resolve(c, elapsed, advance_history);
            return;
        }
        if (!attachment)
            throw std::logic_error("World inspection camera has no live selected attachment");
        pose = attachment->transform;
        require_finite(pose);
        require_finite(state.previous_inspection_attachment);
        //467930 receives (unscaled_now+1)-last_unscaled_now. A stall over
        //200ms is represented by100ms before469475's later1..1000 clamp.
        //Entry presentation uses1ms without consuming a displayed frame.
        const auto delta = !advance_history       ? 1.
                         : elapsed.count() >= 200 ? 100.
                                                  : static_cast<double>(elapsed.count()) + 1.;
        const auto previous_weight = (1000. - delta) * static_cast<double>(.00050000002f);
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column) {
                const auto i = row * 4 + column;
                pose[i] = static_cast<float>((1. - previous_weight) * pose[i]
                                             + previous_weight * state.previous_inspection_attachment[i]);
            }
        //Keep this displayed frame's filtered basis separate from user look.
        //A UI callback may change heading/pitch before the next frame; its
        //presentation must not consume or replace this RAW-history blend.
        state.render_base_pose = pose;
        pose = apply_attachment_look(c, std::move(pose));
        //Native469563 stores CURRENT RAW, not this blended/look-rotated pose.
        //It was initialized once by466DE0, never reset on selection or exit.
        if (advance_history)
            state.previous_inspection_attachment = attachment->transform;
        //469620 explicitly restores the raw attachment translation after look.
        std::copy_n(attachment->transform.begin() + 12, 3, pose.begin() + 12);
    } else {
        state.render_base_pose.reset();
        pose = xy_basis(state.pitch_radians, state.smoothed_heading_radians, false);
        pose[12] = state.focus_position[0];
        pose[13] = static_cast<float>(
            static_cast<double>(state.focus_position[1])
            + world_operations::sample_terrain_surface_height(c, state.focus_position[0], state.focus_position[2])
            + 5.);
        pose[14] = state.focus_position[2];
    }
    publish_pose(c, std::move(pose));
}
} //namespace

void initialize_world_camera_render(WorldRuntime& c) {
    auto& state = c.view.camera;
    const auto mode = c.view.flags & 0x16u;
    if (cache_current(c))
        return;
    //469080 commits direct heading in special modes before constructing the
    //frame. Mode-entry presentation can precede the first input update.
    if (mode)
        state.smoothed_heading_radians = state.heading_radians;
    if (mode && (c.view.flags & 0x3Cu) && state.render_pose_mode == mode && state.render_base_pose) {
        publish_pose(c, apply_attachment_look(c, *state.render_base_pose));
        return;
    }
    resolve(c, std::chrono::milliseconds{ 1 }, false);
}

void advance_world_camera_render(WorldRuntime& c, std::chrono::milliseconds elapsed) {
    resolve(c, elapsed, true);
}

void apply_world_camera_projection(const WorldRuntime& c, SceneCamera& camera) {
    const auto& state = c.view.camera;
    const auto profile_distance = c.view.first_person_view_distance;
    //6CE110/6CE440 initialize the terrain context's frustum flag to1; the
    //modern GPU backend uses that6CF8D0 path. The other native CPU-grid path
    //derives its range from corner rays and is deliberately not duplicated.
    //The native predicate is flags&6, distinct from the0x16 fog/pose gate.
    const auto far_distance =
        profile_distance == 0.f ? state.far_clip_distance : ((c.view.flags & 6u) ? profile_distance : 600.f);
    const auto near_distance = state.near_clip_distance;
    if (!std::isfinite(profile_distance)
        || profile_distance < 0.f
        || !std::isfinite(near_distance)
        || !std::isfinite(far_distance)
        || near_distance <= 0.f
        || far_distance <= near_distance)
        throw std::domain_error("World camera projection distances are invalid");
    camera.near_plane = near_distance;
    camera.far_plane = far_distance;
}

void apply_world_camera_render(const WorldRuntime& c, SceneCamera& camera) {
    const auto& state = c.view.camera;
    const auto mode = c.view.flags & 0x16u;
    if (!cache_current(c))
        throw std::logic_error("World camera pose has not been resolved");
    apply_world_camera_projection(c, camera);
    camera.explicit_pose = state.render_pose;
    if (!mode)
        camera.pitch = state.normal_render_pitch;
}
} //namespace simcoaster
