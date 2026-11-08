#include "simcoaster/world_surface.hpp"
#include "track_ride_internal.hpp"

namespace simcoaster {
namespace {
using namespace track_detail;

std::uint32_t surface_mesh(WorldRuntime& c, float world_x, float world_z) {
    if (!c.construction.active_terrain_model_data || !c.construction.active_terrain_model_data->terrain_grid)
        throw std::logic_error("track ride render terrain is not bound");
    const auto& terrain = *c.construction.active_terrain_model_data->terrain_grid;
    if (!terrain.width_cells || !terrain.height_cells)
        throw std::logic_error("empty track ride terrain");
    const auto x = std::min(static_cast<std::uint32_t>(fixed::truncate_integer(static_cast<double>(world_x) * .1)),
                            terrain.width_cells - 1),
               y = std::min(static_cast<std::uint32_t>(fixed::truncate_integer(static_cast<double>(world_z) * .1)),
                            terrain.height_cells - 1);
    if (x >= 128 || y >= 128)
        throw std::out_of_range("track ride terrain cell");
    return world_mesh_slot_at_map_cell(c, static_cast<int>(x), static_cast<int>(y));
}

void
pose(WorldRuntime& c, std::uint32_t slot, const std::array<float, 3>& position, float roll, float pitch, float yaw) {
    set_mesh_root_pose_zxy(c, slot, position, { roll, pitch, yaw });
}

struct RenderSample {
    TrackRide* owner;
    TrackRideSimulation* simulation;
    float x, z;
    std::int32_t angle;
    std::array<std::array<float, 2>, 4> corners;
    std::array<float, 4> heights;
    std::array<std::uint32_t, 4> meshes;
};
} //namespace

void interpolate_track_ride_render_poses(WorldRuntime& c, float fraction) {
    if (!std::isfinite(fraction)) {
        throw std::invalid_argument("track ride interpolation fraction");
    }
    fraction = std::clamp(fraction, -1.f, 1.f);
    std::vector<RenderSample> samples;
    for (auto [owner, s] : track_detail::simulations(track_detail::manager(c))) {
        if (!s->mesh_instance_id)
            continue; //Custom-flag rows may intentionally omit a model.
        const auto x = static_cast<float>(
                       (static_cast<double>(s->position_x) + static_cast<double>(s->velocity_x) * fraction)
                       * .0032552083),
                   z = static_cast<float>(
                       (static_cast<double>(s->position_y) + static_cast<double>(s->velocity_y) * fraction)
                       * .0032552083);
        const auto angle =
            fixed::add(s->heading, fixed::truncate_integer(static_cast<double>(s->angular_velocity) * fraction)) & 511;
        const auto sx = static_cast<float>(
                       4 * sine(angle) * static_cast<double>(owner->collision_radius) / 1024 * .0032552083),
                   cz = static_cast<float>(
                       4 * cosine(angle) * static_cast<double>(owner->collision_radius) / 1024 * .0032552083);
        RenderSample sample{ owner,
                             s,
                             x,
                             z,
                             angle,
                             { { { x + sx, z + cz }, { x - sx, z - cz }, { x - cz, z + sx }, { x + cz, z - sx } } },
                             { s->render_height, s->render_height, s->render_height, s->render_height },
                             {} };
        for (std::size_t i = 0; i < 4; ++i) {
            sample.meshes[i] = surface_mesh(c, sample.corners[i][0], sample.corners[i][1]);
        }
        samples.push_back(sample);
    }
    //Native batches all corners by first encountered mesh, so animated surface
    //preparation is shared by every vehicle querying that same surface.
    std::vector<std::uint32_t> prepared;
    for (auto& sample : samples)
        for (const auto slot : sample.meshes)
            if (slot && std::ranges::find(prepared, slot) == prepared.end()) {
                prepared.push_back(slot);
                c.ports.meshes.prepare_mesh_surface_query_geometry(c, slot);
                for (auto& other : samples)
                    for (std::size_t i = 0; i < 4; ++i)
                        if (other.meshes[i] == slot) {
                            const auto height = c.ports.meshes.query_prepared_mesh_surface_height(c,
                                                                                                  other.corners[i][0],
                                                                                                  other.corners[i][1]);
                            other.heights[i] = height == -100.f ? other.simulation->render_height : height;
                        }
            }
    for (auto& sample : samples) {
        auto& s = *sample.simulation;
        auto& heights = sample.heights;
        const auto wave = [&](int frequency) {
            const auto phase = mul(frequency, s.simulation_tick_count);
            const auto now = sine(phase);
            return static_cast<double>(now)
                 + (now - sine(fixed::subtract(phase, frequency))) * static_cast<double>(fraction);
        };
        if (has(s, Flag::four_point_bob)) {
            constexpr std::array frequencies{ 6, 4, 3, 5 };
            for (std::size_t i = 0; i < 4; ++i)
                heights[i] = static_cast<float>(heights[i] + wave(frequencies[i]) * .00125);
        }
        const auto yaw = static_cast<float>(((sample.angle - 256) & 511) * .001953125 * 6.2831855);
        const auto diameter = 2 * static_cast<double>(sample.owner->collision_radius) * .0032552083;
        const auto front = static_cast<float>(std::atan2(diameter, static_cast<double>(heights[1]) - heights[0])),
                   side = static_cast<float>(std::atan2(diameter, static_cast<double>(heights[3]) - heights[2]));
        const auto mean = (heights[0] + heights[1] + heights[2] + heights[3]) * .25f;
        auto roll = (s.flags & 0x800u) ? 1.5707964f : side;
        auto height = (s.flags & 0x800u) ? (heights[0] + heights[1]) * .5f : mean;
        s.render_height = height;
        if (s.flags & 0x400u)
            height = static_cast<float>(wave(6) * .0016666667 + height + 1.6);
        if (s.flags & 0x1000u)
            roll = static_cast<float>(
                roll
                + (static_cast<double>(s.angular_velocity) + static_cast<double>(s.angular_velocity_change) * fraction)
                      * .07692308
                      * 1.5707964);
        pose(c, s.mesh_instance_id, { sample.x, height, sample.z }, roll - 1.5707964f, front - 1.5707964f, yaw);
        if (has(s, Flag::spawn_effect_pending)) {
            s.flags &= ~bit(Flag::spawn_effect_pending);
            (void)particle(c, Particle::beam_car, { sample.x, height, sample.z });
        }
        if (has(s, Flag::secondary_surface))
            pose(c,
                 s.secondary_mesh_instance_id,
                 { sample.x, mean, sample.z },
                 side - 1.5707964f,
                 front - 1.5707964f,
                 yaw);
        if (has(s, Flag::secondary_trailing)) {
            const auto x = static_cast<float>(
                           sample.x - static_cast<double>(mul(4 * s.speed_magnitude, sine(sample.angle))) / 49143.f),
                       z = static_cast<float>(
                           sample.z - static_cast<double>(mul(4 * s.speed_magnitude, cosine(sample.angle))) / 49143.f);
            pose(c, s.secondary_mesh_instance_id, { x, height + .69999999f, z }, 0, 0, yaw);
        }
    }
}
} //namespace simcoaster
