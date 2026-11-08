#include "native_coaster_editor_internal.hpp"
#include <bit>
#include <limits>
#include <set>

namespace simcoaster {
using namespace native_coaster_detail;

namespace {
namespace core = coaster_editor_detail;
constexpr std::int32_t cash_bias = 250539758; //4760AD,513920: UI quote encoding only.

std::vector<std::uint32_t> signature(WorldRuntime& c, const WorldCoasterBinding& ride, const WorldCoasterRuntime& world, WorldCellCoordinates tile) {
    std::vector<std::uint32_t> result;
    const auto integer = [&](std::size_t value) {
        require(value <= std::numeric_limits<std::uint32_t>::max(), "preview graph is too large");
        result.push_back(static_cast<std::uint32_t>(value));
    };
    const auto scalar = [&](float value) {
        result.push_back(std::bit_cast<std::uint32_t>(value));
    };
    integer(ride.map_id);
    integer(ride.train_state.placement_flags);
    integer(ride.selection_texture_bias);
    integer(ride.speed_setting);
    scalar(ride.default_attachment_height);
    integer(ride.geometry.size());
    integer(world.placements.size());
    for (std::size_t n = 0; n < ride.geometry.size(); ++n) {
        const auto& input = ride.geometry[n];
        const auto& local = ride.geometry_state.at(n);
        integer(input.geometry_flags & ~0x200000u);
        integer(local.mesh_slot);
        integer(local.placement);
        for (const auto value : input.inline_position) {
            scalar(value);
        }
        for (const auto value : local.controls) {
            scalar(value);
        }
        for (const auto value : local.angles) {
            scalar(value);
        }
    }
    for (const auto& owner : world.placements) {
        integer(owner.flags);
        integer(static_cast<std::uint32_t>(owner.tile_x));
        integer(static_cast<std::uint32_t>(owner.tile_y));
        integer(owner.pylon);
        for (const auto value : owner.position) {
            scalar(value);
        }
        integer(owner.links.size());
        for (const auto& ref : owner.links) {
            integer(ref ? ref->ride : std::numeric_limits<std::uint32_t>::max());
            integer(ref ? ref->node : std::numeric_limits<std::uint32_t>::max());
        }
    }
    //A terrain edit can change a new support's reference Y without changing an
    //existing placement/control. Include the actual queried reference, not an
    //invented flat-ground height or a permanent temporary support handle.
    const auto* terrain = c.construction.active_terrain_model_data ? c.construction.active_terrain_model_data->terrain_grid : nullptr;
    require(terrain != nullptr, "preview terrain is missing");
    const auto height = mesh_placement_reference_height(*terrain, { tile.x, tile.y }, nullptr, 1, 1, 0, 0);
    if (!height) {
        throw height.error();
    }
    scalar(*height);
    return result;
}
} //namespace

void native_coaster_detail::capture_preview(NativeCoasterPreview& output, const core::EditTransaction& edit, std::size_t r) {
    const auto& ride = edit.world.bindings.at(r);
    output.map_id = ride.map_id;
    output.track = ride.generated_mesh;
    std::set<std::uint32_t> captured;
    for (const auto& local : ride.geometry_state) {
        if (local.mesh_slot && captured.insert(local.mesh_slot).second) {
            const auto* mesh = edit.meshes.instance(local.mesh_slot);
            require(mesh != nullptr, "preview support model is missing");
            const bool created = std::ranges::find(edit.created, local.mesh_slot) != edit.created.end();
            output.models.push_back({ created ? 0 : local.mesh_slot, mesh->template_id, mesh->playback.registered, mesh->playback.model, mesh->hoarding && mesh->hoarding->registered ? std::optional { mesh->hoarding->model } : std::nullopt, mesh->terrain_texture_cells });
        }
    }
}

void NativeCoasterEditor::State::rebuild_preview(WorldRuntime& c, bool update_feedback) {
    if (parameter_drag) {
        rebuild_parameter_preview(c, update_feedback);
        return;
    }
    const bool placing = c.construction.mode == 21 && !edit_event_pending && stage != Stage::idle;
    const bool moving = c.construction.mode == 22 && detached && preview;
    if (!map || (!placing && !moving) || !construction_detail::bounds(hover_tile)) {
        geometry_preview.reset();
        preview_signature.clear();
        return;
    }
    auto& m = meshes(c);
    auto& w = world(c);
    auto& initial = ride(c);
    const auto selected = anchor(c);
    if (!selected || *selected >= initial.geometry.size()) {
        geometry_preview.reset();
        preview_signature.clear();
        return;
    }
    const auto& origin = initial.geometry[*selected].inline_position;
    const auto dx = static_cast<double>(origin[0]) - static_cast<double>(hover_tile.x) * 10;
    const auto dz = static_cast<double>(origin[2]) - static_cast<double>(hover_tile.y) * 10;
    if (dx * dx + dz * dz > 90000.0) {
        geometry_preview.reset();
        preview_signature.clear();
        return;
    }
    const auto selected_tile = coordinate(w.placements.at(initial.geometry_state[*selected].placement));
    if (placing && equal(selected_tile, hover_tile)) {
        geometry_preview.reset();
        preview_signature.clear();
        return;
    }
    try {
        auto key = signature(c, initial, w, hover_tile);
        const auto append = [&](std::uint32_t value) {
            key.push_back(value);
        };
        const auto index = [&](std::optional<std::size_t> value) {
            append(value ? static_cast<std::uint32_t>(*value) : std::numeric_limits<std::uint32_t>::max());
        };
        append(static_cast<std::uint32_t>(c.construction.mode));
        append(static_cast<std::uint32_t>(hover_tile.x));
        append(static_cast<std::uint32_t>(hover_tile.y));
        append(interaction);
        append(remaining);
        append(std::to_underlying(stage));
        append(std::to_underlying(edit));
        index(current);
        index(preview);
        index(validation);
        append(preview_candidate_valid ? 1u : 0u);
        if (key != preview_signature || !geometry_preview || preview_clocks.simulation != m.clocks.simulation || preview_clocks.unscaled != m.clocks.unscaled) {
            NativeCoasterPreview output;
            output.valid = preview_candidate_valid;
            {
                //475CF0 temporarily inserts the special geometry/placement, changes
                //existing stack poses, renumbers and rebuilds the real
                //spline/profile. Use that same core synchronously, then retain render
                //values only. The rollback restores bindings, saved ordinals, support
                //poses, allocator identities and event tail, so save/picking/RSSE
                //never see a fake node.
                core::EditTransaction transaction { c, m };
                const auto r = ride_index(c);
                construction_detail::RestoreValue recording { c.construction.action_recording_disabled };
                c.construction.action_recording_disabled = 1;
                auto& target = transaction.world.bindings.at(r);
                if (moving) {
                    const auto ordinal = *preview;
                    const auto source = target.geometry_state.at(ordinal).placement;
                    const auto& source_owner = transaction.world.placements.at(source);
                    bool compatible = true;
                    for (std::size_t i = 0; i < transaction.world.placements.size(); ++i) {
                        const auto& owner = transaction.world.placements[i];
                        if ((owner.flags & 5u) || !equal(coordinate(owner), hover_tile) || i == source) {
                            continue;
                        }
                        compatible = compatible && owner.definition == source_owner.definition && owner.pylon == source_owner.pylon;
                        if (owner.definition) {
                            compatible = compatible && owner.links.size() < core::rules(owner).maximum_stack;
                        }
                        for (const auto& ref : owner.links) {
                            compatible = compatible && ref && ref->ride == r;
                        }
                        if (!owner.links.empty() && owner.links.front()) {
                            compatible = compatible && !(transaction.world.bindings.at(owner.links.front()->ride).geometry.at(owner.links.front()->node).geometry_flags & 0x600u);
                        }
                    }
                    if (!compatible) {
                        geometry_preview.reset();
                        preview_signature.clear();
                        return;
                    }
                    const auto old_piece = core::truncated_cost(core::piece_cost(transaction.world, r, ordinal, false, false));
                    const bool following = (target.train_state.placement_flags & 1u) && ordinal + 1 < target.geometry.size();
                    const auto old_next = following ? core::truncated_cost(core::piece_cost(transaction.world, r, ordinal + 1, false, false)) : 0;
                    core::move_node_raw(transaction, r, ordinal, hover_tile.x, hover_tile.y, false);
                    auto expense = core::rounded_cost(static_cast<float>(core::piece_cost(transaction.world, r, ordinal, false, true)));
                    if (following) {
                        const auto& center = transaction.world.placements.at(target.geometry_state[ordinal].placement).position;
                        expense = core::sum_costs(expense, core::rounded_cost(static_cast<float>(core::piece_cost(transaction.world, r, ordinal + 1, false, false, std::array { center[0] + 5, center[2] + 5 }))));
                    }
                    output.piece_cost = fixed::subtract(expense, core::sum_costs(old_piece, old_next));
                    if (!output.valid) {
                        target.geometry.at(ordinal).geometry_flags |= 0x4000u;
                    }
                    core::finish(transaction, r);
                } else {
                    std::optional<std::size_t> existing;
                    for (std::size_t i = 0; i < transaction.world.placements.size(); ++i) {
                        if (!(transaction.world.placements[i].flags & 5u) && equal(coordinate(transaction.world.placements[i]), hover_tile)) {
                            existing = i;
                            break;
                        }
                    }
                    const auto type = (interaction & 0x4040u) ? 1u : 0u;
                    bool compatible = true;
                    if (existing) {
                        const auto& owner = transaction.world.placements[*existing];
                        compatible = owner.definition == target.definition && owner.pylon == type;
                        for (const auto& ref : owner.links) {
                            compatible = compatible && ref && ref->ride == r;
                        }
                        if (validation && target.geometry_state.at(*validation).placement == *existing) {
                            target.train_state.runtime_flags |= 8u;
                            target.geometry.at(*current).geometry_flags &= ~0x50000u;
                            auto& flags = target.geometry.at(*validation).geometry_flags;
                            flags = (flags | (output.valid ? 0x800000u : 0x804000u)) & ~0xA0000u;
                            core::finish(transaction, r);
                            capture_preview(output, transaction, r);
                            compatible = false; //Boundary closure has no extra temporary node.
                        } else {
                            compatible = compatible && owner.links.size() < core::rules(owner).maximum_stack;
                            for (const auto& ref : owner.links) {
                                if (ref && ref->ride == r && ref->node == *selected) {
                                    compatible = false;
                                }
                            }
                        }
                    }
                    if (compatible) {
                        CoasterNodePlacement request;
                        request.tile_x = hover_tile.x;
                        request.tile_y = hover_tile.y;
                        request.pylon = type;
                        request.second_piece = remaining == 1;
                        request.charge_cost = false;
                        request.insertion_after = *selected;
                        const auto ordinal = core::place_node_raw(transaction, r, request, false);
                        auto& inserted = target.geometry.at(ordinal);
                        inserted.geometry_flags |= 0x8000u;
                        if (output.valid) {
                            inserted.geometry_flags &= ~0x4000u;
                        } else {
                            inserted.geometry_flags |= 0x4000u;
                        }
                        if (ordinal + 1 < target.geometry.size()) {
                            auto& before = target.geometry[ordinal - 1].geometry_flags;
                            before &= (before & 0x200u) ? ~0x10000u : ~0x50000u;
                            inserted.geometry_flags |= 0x50000u;
                        }
                        if (!existing) {
                            transaction.world.placements.at(target.geometry_state[ordinal].placement).flags |= 8u;
                        }
                        core::mark(target, ordinal);
                        if (ordinal > 1) {
                            core::mark(target, ordinal - 1);
                        }
                        core::finish(transaction, r);
                        output.piece_cost = core::truncated_cost(core::piece_cost(transaction.world, r, ordinal, true, false));
                        capture_preview(output, transaction, r);
                    }
                }
                if (moving) {
                    capture_preview(output, transaction, r);
                }
            }
            if (output.piece_cost < 0) {
                output.interaction_cost = fixed::subtract(output.piece_cost, cash_bias);
            } else {
                output.interaction_cost = output.piece_cost;
            }
            preview_signature = std::move(key);
            preview_clocks = m.clocks;
            if (output.track || !output.models.empty()) {
                geometry_preview = std::move(output);
            } else {
                geometry_preview.reset();
            }
        }
        if (update_feedback && geometry_preview && !c.construction.action_recording_disabled) {
            construction.state.cost = fixed::add(construction.state.cost, geometry_preview->interaction_cost);
        }
    } catch (const MeshError& error) {
        geometry_preview.reset();
        preview_signature.clear();
        throw std::runtime_error(describe_mesh_error(error));
    }
}
} //namespace simcoaster
