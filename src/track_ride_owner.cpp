#include "track_ride_internal.hpp"
#include "track_ride_templates.hpp"

namespace simcoaster {
std::uint32_t create_track_ride(WorldRuntime& c,
                                std::int32_t tag,
                                std::int32_t x,
                                std::int32_t y,
                                TrackRideOrientationFlags orientation,
                                std::uint16_t definition_id) {
    using namespace track_detail;
    if (!c.resources.track_rides)
        return 0;
    auto& m = manager(c);
    if (tag >= 0 || tag < -15)
        return 0;
    const auto available = std::ranges::find_if(m.rides, [](const auto& item) {
        return !item;
    });
    if (available == m.rides.end())
        return 0;
    const auto slot = static_cast<std::size_t>(available - m.rides.begin());
    auto ride = std::make_unique<TrackRide>();
    if (!initialize_track_ride_template(*ride, tag))
        return 0;
    ride->handle = (std::bit_cast<std::uint32_t>(tag) << 8) | static_cast<std::uint32_t>(slot);
    ride->object_definition_id = definition_id;
    ride->origin_x = x;
    ride->origin_y = y;
    ride->orientation = std::to_underlying(orientation);
    //Geometry/node pools are shared. Preserve the previous regions before the
    //owner is published so an exhausted pool or allocation exception cannot
    //leave active orphan regions or consume a logical owner identity.
    auto previous_geometry = m.geometry;
    *available = std::move(ride);
    const auto rollback = [&] {
        available->reset();
        m.geometry = std::move(previous_geometry);
    };
    try {
        auto& owner = **available;
        const bool built =
            tag == -11 ? c.resources.object_catalog.shared_ride_description
                             && build_described_track_ride_geometry(m,
                                                                    owner,
                                                                    *c.resources.object_catalog.shared_ride_description)
                       : build_standard_track_ride_geometry(m, owner);
        if (!built) {
            rollback();
            return 0;
        }
        if (tag == -2)
            owner.transition_state = std::to_underlying(TrackRideTransition::active);
        else
            update_track_ride_tuning(owner, owner.interpolation_percent);
        return owner.handle;
    } catch (...) {
        rollback();
        throw;
    }
}

void destroy_track_ride(WorldRuntime& c, std::uint32_t handle) {
    using namespace track_detail;
    if (!c.resources.track_rides)
        return;
    auto& m = manager(c);
    auto* owner = m.find(handle);
    if (!owner)
        return;
    //Original teardown scans the global simulation pool first, then closes the
    //owner, recycles route nodes, auxiliary regions and owner regions in order.
    for (auto [ride, simulation] : simulations(m))
        if (ride == owner)
            release_simulation(c, *owner, *simulation);
    close_track_ride(c, handle);
    for (const auto& auxiliary : owner->auxiliary)
        for (const auto index : auxiliary.geometry)
            m.geometry.at(index).flags &= ~1u;
    for (const auto index : owner->geometry)
        m.geometry.at(index).flags = 0;
    for (const auto index : owner->unlinked_geometry)
        m.geometry.at(index).flags = 0;
    m.rides[handle & 255u].reset();
}

void prune_track_ride_dynamic_path(WorldRuntime& c, std::uint32_t handle) {
    using namespace track_detail;
    if (!c.resources.track_rides)
        return;
    auto& m = manager(c);
    auto* owner = m.find(handle);
    if (!owner)
        return;
    for (auto [ride, simulation] : simulations(m))
        if (ride == owner)
            release_simulation(c, *owner, *simulation);
    close_track_ride(c, handle);
    //Native 6946B0 retains flags 2/4 nodes in their chain order. Vector erasure
    //models unlinking; keeping inactive rows in this chain would let next_node
    //select a freed node. Original global pool identities remain in pool_slot.
    std::erase_if(owner->nodes, [](const auto& node) {
        return !(node.flags & 6u);
    });
    for (const auto& auxiliary : owner->auxiliary)
        for (const auto index : auxiliary.geometry)
            m.geometry.at(index).flags &= ~1u;
    owner->auxiliary.clear();
    owner->transition_node.reset();
    owner->first_simulation.reset();
}

bool append_track_ride_auxiliary(WorldRuntime& c, std::uint32_t handle, TrackRideAuxiliary part) {
    using namespace track_detail;
    if (!c.resources.track_rides)
        return false;
    auto& m = manager(c);
    auto* owner = m.find(handle);
    if (!owner)
        return false;
    if (std::ranges::any_of(owner->auxiliary, [&](const auto& old) {
            return old.x == part.x && old.y == part.y;
        }))
        return false;
    std::size_t count = 0;
    for (const auto& ride : m.rides)
        if (ride)
            count += ride->auxiliary.size();
    if (count >= 1024)
        return false;
    part.geometry.clear();
    part.linked_node_slot.reset();
    owner->auxiliary.reserve(owner->auxiliary.size() + 1);
    auto previous_geometry = m.geometry;
    auto previous_nodes = owner->nodes;
    const auto rollback = [&] {
        m.geometry = std::move(previous_geometry);
        owner->nodes = std::move(previous_nodes);
    };
    try {
        if (!add_track_ride_geometry_piece(m, *owner, part)) {
            rollback();
            return false;
        }
        owner->auxiliary.push_back(std::move(part));
        return true;
    } catch (...) {
        rollback();
        throw;
    }
}
} //namespace simcoaster
