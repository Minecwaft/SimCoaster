#include "park_construction_internal.hpp"
#include "simcoaster/track_ride_runtime.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include <bitset>

namespace simcoaster {
namespace {
using namespace construction_detail;

void animate_segment(ConstructionWorld& c, const CellBase& cell, bool valid) {
    if (!cell.common.mesh_instance) {
        return;
    }
    if (!c.resources.meshes) {
        throw std::logic_error("construction mesh owner is not bound");
    }
    const auto result = c.resources.meshes->request(c, cell.common.mesh_instance, 0, { { valid ? 5u : 12u, 0 }, 7, 1 });
    if (!result) {
        throw result.error();
    }
}

bool root(const TrackCell* cell) {
    return cell && world_cell_is_track_root(&cell->base);
}

bool segment(const TrackCell* cell) {
    return cell && world_cell_has_track_segment_flags(&cell->base);
}

void segment_flag(CellBase& cell, std::uint16_t flag) {
    clear_world_cell_flags(&cell, 15);
    assign_world_cell_flags(&cell, flag);
}

void pulse_segment(ConstructionWorld& c, const CellBase& cell, bool on) {
    if (!cell.common.mesh_instance) {
        return;
    }
    if (!c.resources.meshes) {
        throw std::logic_error("construction mesh owner is not bound");
    }
    auto* mesh = c.resources.meshes->instance(cell.common.mesh_instance);
    if (!mesh) {
        throw std::out_of_range("construction track mesh");
    }
    if (on) {
        mesh->playback.runtime_flags |= 0x4000000u;
    } else {
        mesh->playback.runtime_flags &= ~0x4000000u;
    }
}
} //namespace

bool ParkConstructionRuntime::reconcile_track_links(ConstructionWorld& c, ParkObjectRuntimeView& owner, bool opposite, bool activate, bool animate, bool valid) {
    using namespace construction_detail;
    WorldCellCoordinates p { owner.base.x >> 8, owner.base.y >> 8 };
    auto direction = object_anchor(p, std::bit_cast<int>(owner.angle), opposite);
    state.direction = direction;
    const bool missing_start = !bounds(p) || !world_cell_is_track_root(&track(c, cell_id(p)).base);
    bool stop = missing_start;
    std::bitset<map_cell_count> visited;
    while (bounds(p)) {
        auto& cell = track(c, cell_id(p)).base;
        if (visited.test(cell.index - 1u)) {
            throw std::length_error("cyclic track endpoint reconciliation");
        }
        visited.set(cell.index - 1u);
        if (cell.common.type == 11 || cell.common.type == 16) {
            const auto neighbors = get_world_cell_neighbor_mask(c, &cell);
            const bool corner = get_world_cell_corner_neighbor_mask(c, &cell) != 0;
            if (corner) {
                auto reverse = opposite_world_cell_direction(direction);
                if (!(neighbors & reverse)) {
                    reverse = direction;
                }
                direction = reverse ^ neighbors;
            } else if (world_cell_has_track_segment_flags(&cell)) {
                auto* ahead = resolve_track_cell_in_direction(c, &cell, direction, 2);
                auto* behind = resolve_track_cell_in_direction(c, &cell, opposite_world_cell_direction(direction), 2);
                const auto count = count_world_cell_neighbor_bits(c, &cell);
                if (opposite && animate) {
                    animate_segment(c, cell, valid);
                }
                if (count == 2) {
                    segment_flag(cell, segment(ahead) ? (segment(behind) ? 4 : opposite ? 8 : 2) : (segment(behind) ? opposite ? 2 : 8 : 1));
                }
                const auto boundary = [&](const TrackCell* target) {
                    return !root(target) || target->base.common.parent_id != cell.common.parent_id;
                };
                const auto overwrite = [&](TrackCell* peer, bool invert = false) {
                    if (world_cell_has_track_segment_flags(&cell)) {
                        segment_flag(cell, root(ahead) ? (invert ? 2 : 8) : (invert ? 8 : 2));
                    }
                    if (segment(peer)) {
                        segment_flag(cell, 4);
                    }
                };
                if (!opposite) {
                    if (boundary(behind) || (root(ahead) && count == 1 && (neighbors & direction))) {
                        overwrite(ahead);
                    }
                    if (boundary(ahead) || (root(behind) && count == 1 && (neighbors & opposite_world_cell_direction(direction)))) {
                        overwrite(behind);
                    }
                } else {
                    if (boundary(behind)) {
                        overwrite(ahead);
                    }
                    if (root(behind) && count == 1 && (neighbors & direction)) {
                        overwrite(ahead);
                    }
                    if (boundary(ahead)) {
                        overwrite(behind);
                    }
                    if (root(ahead) && count == 1 && (neighbors & opposite_world_cell_direction(direction))) {
                        overwrite(behind, true);
                    }
                }
                update_world_cell_mesh(c, &cell);
                if (!opposite && animate) {
                    animate_segment(c, cell, valid);
                }
            }
            if (!get_world_cell_corner_neighbor_mask(c, &cell) && !(get_world_cell_neighbor_mask(c, &cell) & direction)) {
                stop = true;
                pulse_segment(c, cell, !opposite && activate);
            }
            auto* next = resolve_track_cell_in_direction(c, &cell, direction, 2);
            if (next && track_variant(c, next->base) && next->base.common.parent_id == cell.common.parent_id && (get_world_cell_neighbor_mask(c, &next->base) & opposite_world_cell_direction(direction))) {
                return true;
            }
        }
        if (stop) {
            return missing_start;
        }
        p = stepped(p, direction, 2);
    }
    return missing_start;
}

void ParkConstructionRuntime::refresh_track_links(ConstructionWorld& c, CellBase& cell) {
    using namespace construction_detail;
    auto& row = root_track(c, cell).base;
    if (!world_cell_is_track_root(&row) || row.common.type == 25 || !row.common.parent_id) {
        return;
    }
    auto* owner = object(c, find_first_park_object_id_in_map_cell(c, &map(c, row.common.parent_id)));
    if (!owner) {
        return;
    }
    const bool valid = static_cast<std::uint8_t>(owner->is_track_ride_valid) != 0;
    if (!reconcile_track_links(c, *owner, false, true, true, valid)) {
        (void)reconcile_track_links(c, *owner, true, false, true, valid);
    }
}

//Native0x66A9E0 rebuilds the auxiliary path, retaining the TrackRide owner and
//its original template geometry. Dynamic cars are released before pruning.
bool ParkConstructionRuntime::refresh_track_path(ConstructionWorld& c, bool preserve_runtime) {
    using namespace construction_detail;
    auto start = WorldCellCoordinates { c.construction.interaction_cursor_anchor[0], c.construction.interaction_cursor_anchor[1] };
    match (state.object_rotation_degrees) {
        case 0   => start.x -= 4;
        case 90  => start.y += 4;
        case 180 => start.x += 4;
        case 270 => start.y -= 4;
        case _   => {
        }
    } if (!bounds(start)) {
        return false;
    }
    auto& initial = root_track(c, track(c, cell_id(start)).base).base;
    if (!world_cell_is_track_root(&initial) || !initial.common.parent_id) {
        return false;
    }
    auto* owner = object(c, find_first_park_object_id_in_map_cell(c, &map(c, initial.common.parent_id)));
    if (!owner || !owner->track_ride_handle) {
        return false;
    }
    if (!preserve_runtime) {
        prune_track_ride_dynamic_path(c, owner->track_ride_handle);
    }
    auto p = start;
    auto direction = state.direction;
    std::uint32_t ordinal = 0, cached_mesh = 0, cached_definition = 0, visibility = 0;
    std::bitset<map_cell_count> visited;
    for (std::uint32_t index = 0; index < map_cell_count; ++index) {
        if (!bounds(p)) {
            return false;
        }
        auto& row = track(c, cell_id(p)).base;
        if (visited.test(row.index - 1u)) {
            return p.x == start.x && p.y == start.y;
        }
        visited.set(row.index - 1u);
        bool valid = world_cell_is_track_root(&row);
        const auto neighbors = get_world_cell_neighbor_mask(c, &row), corner = get_world_cell_corner_neighbor_mask(c, &row), branch = get_world_cell_branch_neighbor_mask(c, &row);
        std::uint32_t flags = 0, mesh = 0;
        if (!valid) {
            direction = 0;
        } else if (corner) {
            match (corner) {
                case 5  => flags = 5;
                case 20 => flags = 8;
                case 65 => flags = 6;
                case 80 => flags = 7;
                case _  => {
                }
            } if (!preserve_runtime) {
                ordinal = 0;
                cached_mesh = 0;
            }
        } else if (branch) {
            flags = 11;
            if (!preserve_runtime) {
                ordinal = 0;
                cached_mesh = 0;
            }
        } else if (neighbors == 17 || neighbors == 68) {
            const auto station = index < 2 ? 0x10000u : 0u;
            if (row.common.flags & 16u) {
                if (auto* linked = gate_or_object_in(c, map(c, row.index).base)) {
                    cached_mesh = linked->mesh_instance_id;
                    cached_definition = static_cast<std::uint32_t>(definition(c, linked->definition_id).balance->Info.Id);
                }
                mesh = cached_mesh;
            } else {
                ordinal = 0;
                cached_mesh = 0;
            }
            flags = station | (neighbors == 17 ? ((row.common.flags & 16u) ? 13u : 10u) : ((row.common.flags & 16u) ? 12u : 9u));
        }
        if (flags && !preserve_runtime) {
            TrackRideAuxiliary item {};
            item.flags = flags;
            item.x = multiply_integer(p.x, 3072);
            item.y = multiply_integer(p.y, 3072);
            item.track_cell_ordinal = ordinal;
            item.mesh_instance_id = mesh;
            item.definition_metadata_word_10 = cached_definition;
            if (!append_track_ride_auxiliary(c, owner->track_ride_handle, std::move(item))) {
                throw std::runtime_error("track route auxiliary pool exhausted");
            }
        }
        if (row.common.flags & 16u) {
            if (!++ordinal) {
                visibility = 0;
            }
            if (auto* linked = gate_or_object_in(c, map(c, row.index).base)) {
                const auto attached = [&](std::uint8_t selector) {
                    const auto* next = resolve_track_cell_in_direction(c, &row, selector, 2);
                    return next && (((next->base.common.type == 11 || next->base.common.type == 16) && next->base.common.mesh_instance) || next->base.common.type == 13 || next->base.common.type == 18);
                };
                const auto cell_direction = get_world_cell_direction(&row);
                if (attached(cell_direction)) {
                    visibility |= 16;
                }
                if (attached(opposite_world_cell_direction(cell_direction))) {
                    visibility |= 1;
                }
                if (ordinal == 2 && (visibility == 0 || visibility == 1 || visibility == 16 || visibility == 17) && linked->mesh_instance_id) {
                    if (!c.resources.meshes) {
                        throw std::logic_error("track upgrade mesh owner is not bound");
                    }
                    auto* mesh_instance = c.resources.meshes->instance(linked->mesh_instance_id);
                    if (!mesh_instance) {
                        throw std::out_of_range("track upgrade mesh");
                    }
                    auto& model = mesh_instance->playback.model;
                    for (const auto selector : std::array<std::uint32_t, 2> { 1, 16 }) {
                        if (const auto resource = model.find_resource(1, selector)) {
                            auto& node = model.nodes.at(model.resources.at(*resource).node_index);
                            if (visibility & selector) {
                                node.flags |= 0x80000010u;
                            } else {
                                node.flags &= ~16u;
                            }
                        }
                    }
                }
            }
        } else if (!row.common.mesh_instance && (row.common.type == 11 || row.common.type == 16)) {
            update_world_cell_mesh(c, &row);
        }
        if (corner) {
            auto reverse = opposite_world_cell_direction(direction);
            if (!(neighbors & reverse)) {
                reverse = direction;
            }
            direction = reverse ^ neighbors;
            if (get_world_cell_direction(&row) != direction) {
                set_world_cell_direction(&row, direction);
                update_world_cell_mesh(c, &row);
            }
        }
        if (!direction || count_world_cell_neighbor_bits(c, &row) < 2) {
            valid = false;
        }
        if (count_world_cell_neighbor_bits(c, &row) == 3) {
            auto* next = resolve_track_cell_in_direction(c, &row, direction, 2);
            if (next && world_cell_is_track_root(&next->base) && ((!get_world_cell_neighbor_mask(c, &next->base)) & direction)) {
                valid = false;
            }
            if (!next || !next->base.common.type) {
                valid = false;
            }
        }
        track(c, row.index).segment_number = static_cast<std::uint16_t>(index);
        if (!direction) {
            return false;
        }
        p = stepped(p, direction, 2);
        if ((p.x == start.x && p.y == start.y) || !valid) {
            return valid;
        }
    }
    throw std::length_error("track path exceeds map capacity");
}
} //namespace simcoaster
