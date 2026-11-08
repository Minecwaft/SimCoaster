#include "native_coaster_editor_internal.hpp"
#include <set>

namespace simcoaster {
using namespace native_coaster_detail;

void NativeCoasterEditor::State::delete_range(AttractionWorld& c, ParkConstructionRuntime& editor, std::uint16_t first, std::uint16_t second, bool recording) {
    auto& initial = ride(c);
    require(first < initial.geometry.size() && second < initial.geometry.size(), "delete endpoints are outside geometry");
    //487270 first starts a range, then applies its second endpoint. Clicking a
    //fixed station owner twice cancels rather than deleting station controls.
    if (first == second && fixed_owner(world(c), initial, first)) {
        interaction &= ~0x80u;
        range_anchor.reset();
        return;
    }
    cleanup(c, editor);
    auto low = static_cast<std::size_t>(first), high = static_cast<std::size_t>(second);
    bool remove_tail = low == 0 || high == 0;
    if (!low) {
        low = initial.geometry.size() - 1;
    } else if (!high) {
        high = initial.geometry.size() - 1;
    }
    if (low > high) {
        std::swap(low, high);
    }
    if (low + 1 == high && (initial.geometry[low].geometry_flags & 0xC0u) == 0xC0u && (initial.geometry[high].geometry_flags & 0x40u)) {
        if (low) {
            --low;
        }
        if (high + 1 < initial.geometry.size()) {
            ++high;
        }
    }
    if (low == high) {
        require(low && high + 1 < initial.geometry.size(), "single-node deletion has no boundary controls");
        --low;
        ++high;
    }
    auto boundary = high;
    if (low + 1 != high) {
        if ((initial.geometry[low + 1].geometry_flags & 0xC0u) == 0x40u) {
            require(low > 0, "partial loop has no lower boundary");
            --low;
        }
        if (initial.geometry[high - 1].geometry_flags & 0x80u) {
            require(high + 1 < initial.geometry.size(), "partial loop has no upper boundary");
            ++boundary;
        }
    }
    if (boundary + 1 == initial.geometry.size()) {
        remove_tail = true;
    }
    require(low < boundary, "delete interval is inverted");
    std::set<std::pair<std::int32_t, std::int32_t>> touched;
    std::optional<std::size_t> next_current, next_validation;
    try {
        coaster_editor_detail::EditTransaction transaction { c, meshes(c) };
        const auto r_index = ride_index(c);
        auto& target = transaction.world.bindings.at(r_index);
        target.train_state.runtime_flags &= ~0x2000u;
        target.train_state.placement_flags |= 2u;
        target.geometry[low].geometry_flags |= 0x50000u;
        target.geometry[boundary].geometry_flags |= 0xA0000u;
        for (auto index = boundary; index > low + 1;) {
            --index;
            const auto& owner = transaction.world.placements.at(target.geometry_state[index].placement);
            touched.emplace(owner.tile_x, owner.tile_y);
            coaster_editor_detail::erase_node_raw(transaction, r_index, index);
        }
        const auto surviving_boundary = low + 1;
        next_current = low;
        next_validation = surviving_boundary;
        if (remove_tail) {
            target.geometry[low].geometry_flags &= ~0x50000u;
            target.train_state.placement_flags &= ~3u;
            while (!target.geometry.empty()) {
                const auto index = target.geometry.size() - 1;
                const auto& owner = transaction.world.placements.at(target.geometry_state[index].placement);
                if (!(owner.flags & 1u)) {
                    break;
                }
                touched.emplace(owner.tile_x, owner.tile_y);
                coaster_editor_detail::erase_node_raw(transaction, r_index, index);
            }
            require(!target.geometry.empty(), "trailing support removal consumed the complete coaster");
            next_current = target.geometry.size() - 1;
            next_validation.reset();
        }
        if (low + 1 == boundary) {
            auto reference = low;
            while (reference && (target.geometry[reference].geometry_flags & 0x40u)) {
                --reference;
            }
            const auto& owner = transaction.world.placements.at(target.geometry_state[reference].placement);
            if (owner.flags & 1u) {
                const auto initial_height = target.definition->balance.coaster.fInitialPylonLoftHeight;
                target.default_attachment_height = initial_height != 0 ? initial_height : 10.0f;
            } else {
                target.default_attachment_height = coaster_editor_detail::profile_position(transaction.meshes, transaction.world, target.geometry_state[reference])[1];
            }
        }
        coaster_editor_detail::finish(transaction, r_index);
        transaction.commit();
    } catch (const MeshError& error) {
        throw std::runtime_error(describe_mesh_error(error));
    }
    current = next_current;
    validation = next_validation;
    //Geometry destruction and World-cell ownership are separate native phases.
    //Reconcile only support cells; fixed station footprint cells stay owned by
    //their actual park object. Surviving stacked nodes retain real mesh slots.
    for (const auto [x, y] : touched) {
        const WorldCellCoordinates tile { x, y };
        if (!construction_detail::bounds(tile)) {
            continue;
        }
        auto& map_cell = construction_detail::map(c, construction_detail::cell_id(tile));
        if (map_cell.base.common.type != 21) {
            continue;
        }
        std::optional<std::uint32_t> slot;
        for (const auto& owner : world(c).placements) {
            if (owner.tile_x == x && owner.tile_y == y && !(owner.flags & 1u)) {
                for (const auto& ref : owner.links) {
                    if (ref) {
                        slot = world(c).bindings.at(ref->ride).geometry_state.at(ref->node).mesh_slot;
                        break;
                    }
                }
            }
        }
        auto& track = construction_detail::track(c, map_cell.base.index);
        track.base.common.mesh_instance = slot.value_or(0);
        if (!slot) {
            remove_map_cell_contents(c, &map_cell.base, 0, 0);
        }
    }
    range_anchor.reset();
    range_cursor.reset();
    interaction &= ~0xA0u;
    ride(c).selection_texture_bias = 0;
    mark(ride(c));
    transition_coaster_detail_state(c, meshes(c), ride(c), RideDetailState::inactive);
    if (recording) {
        record(c, { LayoutActionCode::coaster_delete_range, LayoutWordPair { first, second } });
    }
}
} //namespace simcoaster
