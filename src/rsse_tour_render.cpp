#include "rsse_tour_internal.hpp"

namespace simcoaster {
namespace {
using namespace tour_detail;
constexpr double coordinate_scale = .0033333334f;
constexpr double pi = 3.1415927f;

float interpolate_angle(int previous, int current, float fraction) {
    auto difference = fixed::subtract(current, previous) & 4095;
    if (difference >= 2048)
        difference -= 4095;
    return static_cast<float>(static_cast<double>(difference) * fraction + previous);
}

float smooth_angle(float desired, float previous, int divisor) {
    auto next = fixed::truncate_integer(desired) & 4095;
    const auto old = fixed::truncate_integer(previous) & 4095;
    if (next - old > 2048)
        next -= 4096;
    else if (next - old < -2048)
        next += 4096;
    return static_cast<float>((fixed::add(next, mul(divisor - 1, old)) / divisor) & 4095);
}
} //namespace

void render_rsse_tours(ScriptWorld& c, float fraction) {
    if (!std::isfinite(fraction))
        throw std::invalid_argument("TOUR interpolation is not finite");
    fraction = std::min(fraction, 2.f);
    for (std::size_t handle = 1; handle < c.scripts.tours.slots.size(); ++handle)
        if (auto* r = c.scripts.tours.slots[handle].get())
            for (auto& e : r->entries)
                if (active(e)) {
                    float x, y, z, heading, roll, pitch;
                    if (e.initialize_render_pose) {
                        x = static_cast<float>(static_cast<double>(e.current_x) * coordinate_scale);
                        y = static_cast<float>(static_cast<double>(e.current_y) * coordinate_scale);
                        z = static_cast<float>(static_cast<double>(e.current_z) * coordinate_scale);
                        heading = static_cast<float>(e.heading);
                        roll = static_cast<float>(e.roll);
                        pitch = static_cast<float>(e.pitch);
                        e.initialize_render_pose = 0;
                    } else {
                        const auto position = [&](int previous, int current) {
                            return static_cast<float>(
                                (static_cast<double>(fixed::subtract(current, previous)) * fraction + previous)
                                * coordinate_scale);
                        };
                        x = position(e.previous_x, e.current_x);
                        y = position(e.previous_y, e.current_y);
                        z = position(e.previous_z, e.current_z);
                        x = static_cast<float>((static_cast<double>(e.render_x) * 3. + x) * .25);
                        y = static_cast<float>((static_cast<double>(e.render_y) * 3. + y) * .25);
                        z = static_cast<float>((static_cast<double>(e.render_z) * 3. + z) * .25);
                        heading = smooth_angle(interpolate_angle(e.previous_heading, e.heading, fraction),
                                               e.render_heading,
                                               4);
                        roll = smooth_angle(interpolate_angle(e.previous_roll, e.roll, fraction), e.render_roll, 4);
                        pitch = smooth_angle(interpolate_angle(e.previous_pitch, e.pitch, fraction), e.render_pitch, 8);
                    }
                    e.render_x = x;
                    e.render_y = y;
                    e.render_z = z;
                    e.render_heading = heading;
                    e.render_roll = roll;
                    e.render_pitch = pitch;
                    if (e.mesh_instance_handle) {
                        const std::array<float, 3> angles{ static_cast<float>(pi * roll * -.00048828125),
                                                           static_cast<float>(pi * pitch * -.001953125),
                                                           static_cast<float>(((3072. - heading) * .00048828125 + 1.)
                                                                              * pi) };
                        set_mesh_root_pose_zxy(c, e.mesh_instance_handle, { x, y, z }, angles);
                        if (e.particle_runtime_handle) {
                            const auto node =
                                c.ports.meshes.find_embedded_node_resource_record(c, e.mesh_instance_handle, 256, 1);
                            if (node != -1) {
                                const auto position =
                                    world_operations::get_mesh_clone_attachment_position(c,
                                                                                         e.mesh_instance_handle,
                                                                                         node)
                                        .value_or(std::array<float, 3>{});
                                //FTOL precedes SHL10 in this native path; fractional
                                //coordinates are discarded before scaling, unlike the regular
                                //particle API callers.
                                std::array<int, 3> scaled{};
                                for (std::size_t axis = 0; axis < scaled.size(); ++axis)
                                    scaled[axis] = mul(fixed::truncate_integer(position[axis]), 1024);
                                c.ports.effects.set_particle_effect_position(c, e.particle_runtime_handle, scaled);
                            }
                        }
                    }
                }
}
} //namespace simcoaster
