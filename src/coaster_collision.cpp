#include "simcoaster/coaster_collision.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace simcoaster {
namespace {
constexpr auto excluded = std::to_underlying(CoasterPeerFlag::excluded);
constexpr auto dirty = std::to_underlying(CoasterPeerFlag::dirty);
constexpr auto overflow = std::to_underlying(CoasterPeerFlag::overflow);

void require(bool value, std::size_t index = 0) {
    if (!value) {
        throw MeshError { MeshFailure::configuration, index, "Coaster collision graph", {} };
    }
}

void check(const CoasterCollisionState& state, std::span<const CoasterGeometryObjectInput> geometry) {
    require(state.nodes.size() == geometry.size());
    std::size_t references = 0;
    for (std::size_t i = 0; i < state.nodes.size(); ++i) {
        const auto& node = state.nodes[i];
        require(node.peers.size() <= 8, i);
        references += node.peers.size();
        for (const auto p : node.peers) {
            require(p < state.nodes.size(), p);
        }
    }
    require(references == state.reference_count);
}

void initialize(WorldCoasterBinding& binding) {
    if (binding.collision.initialized) {
        check(binding.collision, binding.geometry);
        return;
    }
    binding.collision = {};
    binding.collision.nodes.resize(binding.geometry.size());
    binding.collision.initialized = true;
}

bool overlaps(const MeshBounds& a, const MeshBounds& b) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!(a.minimum[axis] < b.maximum[axis] && b.minimum[axis] < a.maximum[axis])) {
            return false;
        }
    }
    return true;
}

float padding(const WorldCoasterBinding& binding, const WorldCoasterRuntime& world, std::size_t i) {
    for (const auto index : { i, i + 1 }) {
        const auto& owner = world.placements.at(binding.geometry_state.at(index).placement);
        if (owner.definition) {
            return owner.definition->pylons.at(owner.pylon).collision_radius;
        }
    }
    return 0;
}

void append(CoasterCollisionState& graph, std::span<CoasterGeometryObjectInput> geometry, std::size_t a, std::size_t b) {
    auto& node = graph.nodes[a];
    if (node.peers.size() < 8) {
        node.peers.push_back(b);
        ++graph.reference_count;
    } else {
        geometry[a].geometry_flags |= overflow;
    }
}

void pair(WorldCoasterBinding& binding, const WorldCoasterRuntime& world, const CoasterTrackSamples& track, std::size_t a, std::size_t b) {
    auto& graph = binding.collision;
    const auto& states = binding.geometry_state;
    const auto owner_a = states[a].placement, owner_b = states[b].placement;
    const auto next_a = states[a + 1].placement, next_b = states[b + 1].placement;
    if (next_a == owner_b || owner_a == next_b || ((world.placements.at(next_a).flags & 1u) && binding.geometry[b].geometry_index == 0) || (binding.geometry[a].geometry_index == 0 && (world.placements.at(next_b).flags & 1u)) || (binding.geometry[b].geometry_flags & excluded) || !overlaps(graph.nodes[a].bounds, graph.nodes[b].bounds)) {
        return;
    }
    MeshBounds overlap;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        overlap.minimum[axis] = std::max(graph.nodes[a].bounds.minimum[axis], graph.nodes[b].bounds.minimum[axis]);
        overlap.maximum[axis] = std::min(graph.nodes[a].bounds.maximum[axis], graph.nodes[b].bounds.maximum[axis]);
    }
    const auto ca = collect_coaster_collision_candidates(track.samples, track.geometry_component_indices[a], track.geometry_component_indices[a + 1], padding(binding, world, a), overlap);
    const auto cb = collect_coaster_collision_candidates(track.samples, track.geometry_component_indices[b], track.geometry_component_indices[b + 1], padding(binding, world, b), overlap);
    const auto& profile = binding.definition->balance.mesh_definition->collision_profile;
    for (const auto& first : ca) {
        for (const auto& second : cb) {
            if (overlaps(first.bounds, second.bounds) && coaster_collision_segments_intersect(profile, track.samples[first.segment], track.samples[first.segment + 1], profile, track.samples[second.segment], track.samples[second.segment + 1])) {
                append(graph, binding.geometry, a, b);
                append(graph, binding.geometry, b, a);
                return;
            }
        }
    }
}
} //namespace

void remove_coaster_geometry_peer_links(CoasterCollisionState& graph, std::span<CoasterGeometryObjectInput> geometry, std::size_t removed) {
    require(graph.nodes.size() == geometry.size() && removed < graph.nodes.size(), removed);
    const auto erase_inbound = [&](std::size_t index) {
        auto& node = graph.nodes[index];
        const auto count = std::erase(node.peers, removed);
        graph.reference_count -= count;
        if (count && (geometry[index].geometry_flags & overflow)) {
            geometry[index].geometry_flags |= dirty;
        }
    };
    //Full lists may have failed to record a backlink. Only the overflow path
    //scans all lists; replacing this with symmetric adjacency loses native
    //order.
    if (geometry[removed].geometry_flags & overflow) {
        for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
            erase_inbound(i);
        }
        geometry[removed].geometry_flags &= ~overflow;
    } else {
        const auto peers = graph.nodes[removed].peers;
        for (const auto p : peers) {
            erase_inbound(p);
        }
    }
    graph.reference_count -= graph.nodes[removed].peers.size();
    graph.nodes[removed].peers.clear();
}

void mark_coaster_geometry_peers_dirty(WorldCoasterBinding& binding) {
    initialize(binding);
    for (std::size_t i = 0; i < binding.geometry.size(); ++i) {
        remove_coaster_geometry_peer_links(binding.collision, binding.geometry, i);
        binding.geometry[i].geometry_flags |= dirty;
    }
}

void clear_coaster_geometry_peers(WorldCoasterBinding& binding) {
    initialize(binding);
    for (std::size_t i = 0; i < binding.geometry.size(); ++i) {
        remove_coaster_geometry_peer_links(binding.collision, binding.geometry, i);
        binding.geometry[i].geometry_flags &= ~dirty;
    }
    binding.collision.needs_full_rebuild = false;
    check(binding.collision, binding.geometry);
}

void rebuild_coaster_geometry_peers(WorldCoasterBinding& binding, const WorldCoasterRuntime& world, const CoasterTrackSamples& track) {
    initialize(binding);
    if (binding.collision.needs_full_rebuild) {
        mark_coaster_geometry_peers_dirty(binding);
        binding.collision.needs_full_rebuild = false;
    }
    const auto size = binding.geometry.size();
    if (size < 2) {
        return;
    }
    require(binding.definition && binding.definition->balance.mesh_definition);
    require(binding.geometry_state.size() == size && track.geometry_component_indices.size() == size);
    for (std::size_t i = 0; i + 1 < size; ++i) {
        const auto first = track.geometry_component_indices[i], last = track.geometry_component_indices[i + 1];
        require(first <= last && last < track.samples.size(), i);
        auto& bounds = binding.collision.nodes[i].bounds;
        bounds.minimum = bounds.maximum = track.samples[first].position;
        for (std::size_t j = first + 1; j <= last; ++j) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto p = track.samples[j].position[axis];
                if (p < bounds.minimum[axis]) {
                    bounds.minimum[axis] = p;
                } else if (p > bounds.maximum[axis]) {
                    bounds.maximum[axis] = p;
                }
            }
        }
        const auto radius = padding(binding, world, i);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            bounds.minimum[axis] -= radius;
            bounds.maximum[axis] += radius;
        }
    }
    for (std::size_t i = 0; i + 1 < size; ++i) {
        if (binding.geometry[i].geometry_flags & dirty) {
            remove_coaster_geometry_peer_links(binding.collision, binding.geometry, i);
            if (binding.geometry[i].geometry_flags & excluded) {
                continue;
            }
            binding.geometry[i].geometry_flags &= ~(dirty | overflow);
            for (std::size_t j = i + 2; j + 1 < size; ++j) {
                pair(binding, world, track, i, j);
            }
            if (i >= 2) {
                for (auto j = i - 2;; --j) {
                    pair(binding, world, track, i, j);
                    if (j == 0) {
                        break;
                    }
                }
            }
        }
    }
    check(binding.collision, binding.geometry);
}

void insert_coaster_geometry_peer_node(WorldCoasterBinding& binding, std::size_t index) {
    const auto size = binding.geometry.size();
    require(index < size, index);
    if (!binding.collision.initialized) {
        initialize(binding);
        for (auto& g : binding.geometry) {
            g.geometry_flags |= dirty;
        }
        return;
    }
    auto& graph = binding.collision;
    require(graph.nodes.size() + 1 == size, index);
    for (auto& node : graph.nodes) {
        for (auto& p : node.peers) {
            if (p >= index) {
                ++p;
            }
        }
    }
    graph.nodes.insert(graph.nodes.begin() + index, CoasterCollisionNode {});
    binding.geometry[index].geometry_flags |= dirty;
    //Inserting changes the predecessor's segment. Its existing links are removed
    //during the ordered rebuild, where overflow also dirties dependent nodes.
    if (index) {
        binding.geometry[index - 1].geometry_flags |= dirty;
    }
}

void erase_coaster_geometry_peer_node(WorldCoasterBinding& binding, std::size_t index) {
    initialize(binding);
    require(index < binding.geometry.size(), index);
    auto& graph = binding.collision;
    remove_coaster_geometry_peer_links(graph, binding.geometry, index);
    if (index) {
        remove_coaster_geometry_peer_links(graph, binding.geometry, index - 1);
        binding.geometry[index - 1].geometry_flags |= dirty;
    }
    graph.nodes.erase(graph.nodes.begin() + index);
    for (auto& node : graph.nodes) {
        for (auto& p : node.peers) {
            require(p != index, p);
            if (p > index) {
                --p;
            }
        }
    }
}

void seed_coaster_geometry_peer_snapshot(WorldCoasterBinding& binding) {
    binding.collision = {};
    initialize(binding);
    auto& graph = binding.collision;
    const auto& saved = binding.saved_geometry;
    const auto seed = [&](std::size_t node, std::span<const std::uint16_t> peers) {
        require(node < graph.nodes.size() && peers.size() <= 8, node);
        for (const auto p : peers) {
            require(p < graph.nodes.size(), p);
            graph.nodes[node].peers.push_back(p);
            ++graph.reference_count;
        }
    };
    if (!saved.root_peers.empty()) {
        seed(2, saved.root_peers);
    }
    for (std::size_t i = 0; i < binding.geometry_state.size(); ++i) {
        if (const auto ordinal = binding.geometry_state[i].saved_ordinal) {
            require(*ordinal < saved.geometry.size(), i);
            seed(i, saved.geometry[*ordinal].peers);
        }
    }
    //0x477870 writes the all-node counter but only ordinal2 and explicit-node
    //lists. 0x4782E0 cannot recover the omitted implicit0/1 pointers. Preserve
    //coherent snapshots exactly; recover lossy ones from geometry, without
    //guessing reciprocals or pretending their old cap/history order survived.
    graph.needs_full_rebuild = graph.reference_count != saved.header.geometry_peer_reference_count;
    check(graph, binding.geometry);
}

void capture_coaster_geometry_peer_snapshot(WorldCoasterBinding& binding) {
    initialize(binding);
    capture_coaster_geometry_peer_snapshot(binding, binding.saved_geometry);
}

void capture_coaster_geometry_peer_snapshot(const WorldCoasterBinding& binding, CoasterRecordSave& saved) {
    //Before geometry restoration, the wire snapshot is still authoritative.
    if (!binding.collision.initialized) {
        return;
    }
    require(!binding.collision.needs_full_rebuild);
    check(binding.collision, binding.geometry);
    require(binding.collision.reference_count <= std::numeric_limits<std::uint32_t>::max());
    saved.header.geometry_peer_reference_count = static_cast<std::uint32_t>(binding.collision.reference_count);
    saved.root_peers.clear();
    for (auto& row : saved.geometry) {
        row.peers.clear();
        row.header.peer_count = 0;
    }
    for (std::size_t i = 0; i < binding.geometry.size(); ++i) {
        std::vector<std::uint16_t>* target = nullptr;
        if (i == 2) {
            target = &saved.root_peers;
        } else if (const auto ordinal = binding.geometry_state.at(i).saved_ordinal) {
            require(*ordinal < saved.geometry.size(), i);
            target = &saved.geometry[*ordinal].peers;
        } else {
            continue; //Native wire layout omits implicit lists, not their count.
        }
        for (const auto p : binding.collision.nodes[i].peers) {
            require(p <= std::numeric_limits<std::uint16_t>::max(), p);
            target->push_back(static_cast<std::uint16_t>(p));
        }
        if (i != 2) {
            saved.geometry[*binding.geometry_state[i].saved_ordinal].header.peer_count = static_cast<std::uint8_t>(target->size());
        }
    }
}
} //namespace simcoaster
