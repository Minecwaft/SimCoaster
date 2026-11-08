#pragma once
#include "simcoaster/numeric_word.hpp"
#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_domains_fwd.hpp"
#include "simcoaster/rsse_actions.hpp"
#include "simcoaster/simulation_sine.hpp"
#include "simcoaster/track_ride_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_research.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcoaster::track_detail {
using Flag = TrackSimulationFlag;

constexpr std::uint32_t bit(Flag flag) {
    return std::to_underlying(flag);
}

inline bool has(const TrackRideSimulation& s, Flag flag) {
    return (s.flags & bit(flag)) != 0;
}

inline TrackRideTransition state(const TrackRide& r) {
    return static_cast<TrackRideTransition>(r.transition_state);
}

inline void state(TrackRide& r, TrackRideTransition s) {
    r.transition_state = std::to_underlying(s);
}

inline TrackRideManager& manager(WorldRuntime& c) {
    return require_bound(c.resources.track_rides, "track ride manager is not bound");
}

inline World& world(WorldRuntime& c) {
    return require_bound(c.simulation.current, "track ride World is not bound");
}

inline std::int32_t mul(std::int32_t a, std::int32_t b) {
    return fixed::narrow(std::int64_t{ a } * b);
}

inline std::int32_t div(std::int32_t a, std::int32_t b) {
    return numeric::quotient(a, b, "track ride division");
}

inline std::int32_t scale(std::int32_t a, std::int32_t b, std::int32_t divisor = 1024) {
    return div(mul(a, b), divisor);
}

inline std::int32_t heading(std::int32_t x, std::int32_t y) {
    return fixed::truncate_integer(
        256.0 - std::atan2(static_cast<double>(x), static_cast<double>(fixed::negate(y))) * 0.3183101550488766 * 256.0);
}

inline std::int32_t random(WorldRuntime& c) {
    return next_world_prng_value(world(c));
}

inline std::uint32_t random_bits(WorldRuntime& c) {
    return static_cast<std::uint32_t>(random(c));
}

inline std::uint32_t private_random(WorldRuntime& c) {
    auto& r = manager(c).random_state;
    r = r * 214013u + 2531011u;
    return (r >> 16) & 32767u;
}
enum class Particle : std::uint32_t {
    sparks = 1,
    smoke = 2,
    exhaust = 0xA,
    beam_car = 0xF,
    beam_person = 0x13,
    small_splash = 0x60
};

inline auto simulations(TrackRideManager& m) {
    std::vector<std::pair<TrackRide*, TrackRideSimulation*>> result;
    for (auto& r : m.rides)
        if (r)
            for (auto& s : r->simulations)
                if (has(s, Flag::active))
                    result.emplace_back(r.get(), &s);
    std::ranges::sort(result, {}, [](const auto& p) {
        return p.second->pool_slot;
    });
    return result;
}

inline std::size_t active_count(const TrackRide& r) {
    return static_cast<std::size_t>(std::ranges::count_if(r.simulations, [](const auto& s) {
        return has(s, Flag::active);
    }));
}

inline TrackRideSimulation* simulation(TrackRideManager& m, std::int32_t slot) {
    for (auto& r : m.rides)
        if (r)
            for (auto& s : r->simulations)
                if (has(s, Flag::active) && s.pool_slot == slot)
                    return &s;
    return nullptr;
}

inline TrackRideLinkNode* node(TrackRideManager& m, std::int32_t slot) {
    for (auto& r : m.rides)
        if (r)
            for (auto& n : r->nodes)
                if ((n.flags & 1u) && n.pool_slot == slot)
                    return &n;
    return nullptr;
}

inline TrackRide* node_owner(TrackRideManager& m, const TrackRideLinkNode* n) {
    for (auto& r : m.rides)
        if (r)
            for (const auto& v : r->nodes)
                if (&v == n)
                    return r.get();
    return nullptr;
}

inline std::size_t node_count(const TrackRideManager& m) {
    std::size_t count = 0;
    for (const auto& r : m.rides)
        if (r)
            for (const auto& n : r->nodes)
                count += (n.flags & 1u) != 0;
    return count;
}

inline TrackRideLinkNode& next_node(TrackRideManager& m, TrackRideLinkNode& n, bool skip_normal = false) {
    auto* r = node_owner(m, &n);
    if (!r || r->nodes.empty())
        throw std::logic_error("track ride route owner");
    auto index = static_cast<std::size_t>(&n - r->nodes.data());
    for (std::size_t i = 0; i < r->nodes.size(); ++i) {
        index = (index + 1) % r->nodes.size();
        if (!skip_normal || !(r->nodes[index].flags & 2u))
            return r->nodes[index];
    }
    throw std::logic_error("track ride route has no normal node");
}

inline WorldMeshInstance* mesh(WorldRuntime& c, std::uint32_t slot) {
    return c.resources.meshes ? c.resources.meshes->instance(slot) : nullptr;
}

inline auto sound_position(const TrackRideSimulation& s) {
    return std::array<std::int32_t, 3>{ fixed::truncate_integer(static_cast<double>(s.position_x) * 0.0033036007),
                                        0,
                                        fixed::truncate_integer(static_cast<double>(s.position_y) * 0.0033036007) };
}

inline ParkObjectRuntimeView* park_object(WorldRuntime& c, const TrackRide& r) {
    for (auto* row = c.entities.active_head; row; row = row->active_next)
        if (row->payload && row->payload->kind == ThingKind::park_object) {
            auto* o = reinterpret_cast<ParkObjectRuntimeView*>(row->payload);
            if (o->track_ride_handle == r.handle)
                return o;
        }
    return nullptr;
}

inline std::uint32_t
sound(MissionWorld& c, const TrackRide& r, const TrackRideSimulation& s, int slot, std::uint32_t current = 0) {
    const auto* o = park_object(c, r);
    const auto key = get_rsse_sound_script_slot(c, o ? o->script_id : 0, slot);
    if (!key)
        return 0;
    const auto p = sound_position(s);
    return submit_positioned_template_request(c,
                                              current,
                                              c.resources.local_rides_catalog_slot,
                                              static_cast<std::uint32_t>(key),
                                              p[0],
                                              p[1],
                                              p[2]);
}

inline std::array<float, 3> root_position(WorldRuntime& c, const TrackRideSimulation& s) {
    const auto* m = mesh(c, s.mesh_instance_id);
    if (!m || !m->playback.model.root)
        throw std::logic_error("track ride vehicle has no mesh root");
    const auto& t = m->playback.model.nodes.at(*m->playback.model.root).local_transform;
    return { t[12], t[13], t[14] };
}

inline std::array<std::int32_t, 3> particle_position(const std::array<float, 3>& p) {
    return { fixed::truncate_integer(static_cast<double>(p[0]) * 1024.0),
             fixed::truncate_integer(static_cast<double>(p[1]) * 1024.0),
             fixed::truncate_integer(static_cast<double>(p[2]) * 1024.0) };
}

inline std::uint32_t particle(WorldRuntime& c, Particle type, const std::array<float, 3>& p) {
    return c.ports.effects.create_particle_effect(c, std::to_underlying(type), particle_position(p), 0);
}

void animate(WorldRuntime&, TrackRideSimulation&, std::uint32_t animation, bool loop = false, bool force = true);
void release_simulation(WorldRuntime&, TrackRide&, TrackRideSimulation&);
std::uint32_t release_riders(WorldRuntime&, TrackRide&, TrackRideSimulation&, bool effects);
void bind_riders(WorldRuntime&, TrackRide&, TrackRideSimulation&, bool effects);
void setup_type(WorldRuntime&, TrackRide&, TrackRideSimulation&);
void step_target(WorldRuntime&, TrackRide&, TrackRideSimulation&);
void step_motion(WorldRuntime&, TrackRide&, TrackRideSimulation&);
void process_effects(WorldRuntime&, TrackRide&, TrackRideSimulation&);
bool contains(const TrackRideGeometryRegion&, std::int32_t, std::int32_t);
bool overlaps(const TrackRideGeometryRegion&, const TrackRideGeometryRegion&);
std::optional<std::size_t> find_region(TrackRideManager&, std::int32_t, std::int32_t);
std::optional<std::size_t> nearest_region(TrackRideManager&, std::size_t, std::int32_t, std::int32_t, bool);
std::int32_t magnitude(std::int32_t, std::int32_t);
std::int32_t sine(std::int32_t);
std::int32_t cosine(std::int32_t);
std::int32_t steer(WorldRuntime&, TrackRide&, TrackRideSimulation&, std::int32_t, std::int32_t);
void resolve_region(WorldRuntime&, TrackRide&, TrackRideSimulation&);
void collide_solids(WorldRuntime&, TrackRideSimulation&);
void hidden(WorldRuntime&, TrackRideSimulation&, bool);
} //namespace simcoaster::track_detail
