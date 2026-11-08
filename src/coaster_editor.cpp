#include "coaster_editor_internal.hpp"
#include "simcoaster/world_bank.hpp"
#include <limits>

namespace simcoaster {
namespace coaster_editor_detail {
namespace {
std::size_t
find_or_create_owner(EditTransaction& edit, std::size_t r, const CoasterNodePlacement& request, bool validate) {
    auto& world = edit.world;
    const auto& binding = world.bindings.at(r);
    for (std::size_t i = 0; i < world.placements.size(); ++i) {
        const auto& owner = world.placements[i];
        if ((owner.flags & 5u) || owner.tile_x != request.tile_x || owner.tile_y != request.tile_y)
            continue;
        require(owner.definition == binding.definition && owner.pylon == request.pylon, i);
        //474B60 searches every runtime's placement list. The COS reader490390
        //calls it directly, so a compatible occupied support may acquire links
        //from another coaster. Only live473D60 validation rejects that foreign
        //runtime; applying its gate to raw import broke genuine installed COS.
        for (const auto& ref : owner.links)
            require(ref && (!validate || ref->ride == r), i);
        return i;
    }
    const auto* terrain = edit.context.construction.active_terrain_model_data
                            ? edit.context.construction.active_terrain_model_data->terrain_grid
                            : nullptr;
    require(terrain
            && request.tile_x >= 0
            && request.tile_y >= 0
            && static_cast<std::uint32_t>(request.tile_x) < terrain->width_cells
            && static_cast<std::uint32_t>(request.tile_y) < terrain->height_cells);
    auto height = mesh_placement_reference_height(*terrain, { request.tile_x, request.tile_y }, nullptr, 1, 1, 0, 0);
    if (!height)
        throw height.error();
    CoasterPlacement owner;
    owner.tile_x = request.tile_x;
    owner.tile_y = request.tile_y;
    owner.definition = binding.definition;
    owner.pylon = request.pylon;
    owner.position = { static_cast<float>(request.tile_x) * 10, *height, static_cast<float>(request.tile_y) * 10 };
    world.placements.push_back(std::move(owner));
    return world.placements.size() - 1;
}

void validate_connection(WorldCoasterRuntime& world,
                         std::size_t r,
                         std::size_t insertion,
                         const CoasterNodePlacement& request,
                         std::size_t owner_id) {
    const auto& binding = world.bindings.at(r);
    if (!insertion)
        return;
    const auto& previous = binding.geometry_state.at(insertion - 1);
    const auto& owner = world.placements.at(previous.placement);
    CoasterConnectionCandidate candidate{ tile(owner),
                                          { request.tile_x, request.tile_y },
                                          {},
                                          static_cast<std::uint32_t>(insertion - 1),
                                          owner.flags };
    if (insertion > 1)
        candidate.preceding = tile(world.placements.at(binding.geometry_state[insertion - 2].placement));
    const auto rule_pylon = (binding.geometry[insertion - 1].geometry_flags & 0x40u) ? 1 : request.pylon;
    const auto& limits = binding.definition->balance.pylons[rule_pylon];
    require(validate_coaster_connection(coaster_placement_rules(limits, rule_pylon != 0), candidate)
                == CoasterPlacementValidity::valid,
            insertion);
    //Insertion and movement also affect the following turn(s), as in recursive
    //473D60. Validate them against the proposed predecessor before committing.
    for (std::size_t n = insertion; n < std::min(binding.geometry.size(), insertion + 2); ++n) {
        const auto& local = binding.geometry_state[n];
        const auto& next = world.placements[local.placement];
        CoasterConnectionCandidate following;
        following.origin =
            n == insertion ? candidate.candidate : tile(world.placements[binding.geometry_state[n - 1].placement]);
        following.candidate = tile(next);
        following.preceding = n == insertion ? std::optional{ candidate.origin } : std::optional{ candidate.candidate };
        following.origin_ordinal = static_cast<std::uint32_t>(n);
        following.origin_owner_flags = n == insertion ? world.placements[owner_id].flags
                                                      : world.placements[binding.geometry_state[n - 1].placement].flags;
        if (binding.geometry[n - 1].geometry_flags & 0x10000u)
            break;
        const auto& origin_owner = world.placements[binding.geometry_state[n ? n - 1 : 0].placement];
        const auto following_pylon =
            n == insertion ? request.pylon : (origin_owner.definition ? origin_owner.pylon : 0u);
        require(validate_coaster_connection(
                    coaster_placement_rules(binding.definition->balance.pylons[following_pylon], following_pylon != 0),
                    following)
                    == CoasterPlacementValidity::valid,
                n);
    }
}
} //namespace

std::size_t place_node_raw(EditTransaction& edit, std::size_t r, const CoasterNodePlacement& request, bool validate) {
    auto& world = edit.world;
    auto& binding = world.bindings.at(r);
    require(request.pylon < 2);
    const auto template_id = binding.definition->pylon_templates[request.pylon];
    require(template_id != 0, request.pylon);
    const auto insertion = request.insertion_after ? *request.insertion_after + 1 : binding.geometry.size();
    require(insertion <= binding.geometry.size() && binding.geometry.size() < std::numeric_limits<std::uint16_t>::max(),
            insertion);
    require(!(binding.train_state.placement_flags & 1u) || insertion + 1 <= binding.geometry.size(), insertion);
    if (validate)
        validate_top_level_terrain(edit.context, request.tile_x, request.tile_y);
    const auto owner_id = find_or_create_owner(edit, r, request, validate);
    auto& owner = world.placements[owner_id];
    if (validate)
        validate_connection(world, r, insertion, request, owner_id);
    const auto limits = rules(owner);
    const auto& data = pylon(owner);
    require(owner.links.size() < limits.maximum_stack, owner_id);
    if (validate && !owner.links.empty()) {
        require(owner.links.front().has_value(), owner_id);
        //4743D2/47440A reject station boundary controls when reusing an occupied
        //tile. Selected insertion tests600h; ordinary append tests200h.
        const auto forbidden =
            ((binding.train_state.placement_flags & 2u) || request.insertion_after) ? 0x600u : 0x200u;
        require(!(node(world, *owner.links.front()).geometry_flags & forbidden), owner_id);
    }
    float height = request.attachment_height.value_or(binding.default_attachment_height);
    if (!request.attachment_height && (data.flags & 0x4000u) && request.second_piece && insertion) {
        const auto* previous = previous_placement_owner(world, r, owner_id);
        require(previous != nullptr, owner_id);
        const auto dx = owner.position[0] - previous->position[0], dz = owner.position[2] - previous->position[2];
        auto normal = insertion - 1;
        while (normal && (binding.geometry[normal].geometry_flags & 0x40u))
            --normal;
        const auto& normal_local = binding.geometry_state[normal];
        const auto& normal_owner = world.placements[normal_local.placement];
        const auto reference_y = (normal_owner.flags & 1u) ? binding.geometry[normal].inline_position[1]
                                                           : profile_position(edit.meshes, world, normal_local)[1];
        height = static_cast<float>(std::hypot(static_cast<double>(dx), static_cast<double>(dz)) * 0.5 + reference_y);
        if (insertion > 1) {
            const auto& previous_data = pylon(world.placements[binding.geometry_state[insertion - 1].placement]);
            require(previous_data.height_maximum != previous_data.height_minimum);
            binding.geometry_state[insertion - 1].controls[0] =
                static_cast<float>(std::clamp((static_cast<double>(height) - previous_data.height_minimum)
                                                  / (previous_data.height_maximum - previous_data.height_minimum),
                                              0.0,
                                              1.0));
        }
    }
    std::vector<CoasterSupportLevel> levels;
    for (const auto& ref : owner.links) {
        require(ref.has_value());
        levels.push_back({ state(world, *ref).controls[0], node(world, *ref).geometry_flags });
    }
    const auto selected = select_coaster_support_level(height, limits, data, levels);
    const auto link = request.link_slot.value_or(selected.level);
    height = selected.height;
    require(link <= owner.links.size(), link);
    std::optional<double> upper;
    if (link < owner.links.size())
        upper = cumulative(world, owner, link);
    const auto* source = edit.meshes.definition(template_id);
    require(source != nullptr);
    DefinitionGrid shape{};
    shape.width = source->footprint_width;
    shape.height = source->footprint_height;
    edit.reserve_mesh_creation();
    const auto created = edit.meshes.create(edit.context,
                                            { static_cast<std::int32_t>(template_id),
                                              0x3FAu,
                                              { 0, request.tile_x, request.tile_y, &shape },
                                              0,
                                              0,
                                              nullptr,
                                              0,
                                              0,
                                              -1,
                                              0 });
    if (!created)
        throw created.error();
    edit.created.push_back(*created);
    constexpr std::array categories{ 3u, 10u, 2u, 9u, 10u }, variants{ 0u, 0u, 0u, 0u, 1u };
    for (std::size_t i = 0; i < 5; ++i)
        if (data.flags & (32u << i)) {
            const auto result =
                edit.meshes.request(edit.context, *created, i, { { categories[i], variants[i] }, 2, 1 });
            if (!result)
                throw result.error();
        }
    edit.meshes.instance(*created)->playback.runtime_flags |= 0x81000u;
    for (auto& placement : world.placements)
        for (auto& ref : placement.links)
            if (ref && ref->ride == r && ref->node >= insertion)
                ++ref->node;
    CoasterGeometryObjectInput input;
    input.geometry_index = static_cast<std::uint32_t>(insertion);
    input.inline_position = owner.position;
    input.inline_position[0] += 5;
    input.inline_position[2] += 5;
    input.has_runtime_data = true;
    input.runtime_data_flags = data.flags;
    input.minimum_tangent_dot = data.minimum_tangent_dot;
    if (request.pylon != 0)
        input.geometry_flags = request.second_piece ? 0x60u : 0xE0u;
    if (binding.train_state.placement_flags & 2u) {
        //473420 transfers the selected gap's current-object marker to the new
        //geometry; its validation-object pointer remains on the successor.
        require(insertion > 0, insertion);
        binding.geometry[insertion - 1].geometry_flags &= ~0x50000u;
        input.geometry_flags |= 0x50000u;
    }
    CoasterNodeState local;
    local.placement = owner_id;
    local.mesh_slot = *created;
    if ((data.flags & 0x4000u) && request.second_piece)
        local.controls[4] = 0.5f;
    binding.geometry.insert(binding.geometry.begin() + static_cast<std::ptrdiff_t>(insertion), std::move(input));
    binding.geometry_state.insert(binding.geometry_state.begin() + static_cast<std::ptrdiff_t>(insertion), local);
    insert_coaster_geometry_peer_node(binding, insertion);
    owner.links.insert(owner.links.begin() + static_cast<std::ptrdiff_t>(link), CoasterNodeReference{ r, insertion });
    if (upper) {
        const auto lower = link ? cumulative(world, owner, link - 1) + data.height_offset : 0.0;
        const auto maximum = *upper - data.height_offset - limits.minimum_height;
        const auto resolved =
            std::max(static_cast<double>(limits.minimum_height), std::min(static_cast<double>(height), maximum) - lower)
            + lower;
        set_height(world, owner, link, resolved, true);
        set_height(world, owner, link + 1, *upper);
    } else
        set_height(world, owner, link, height);
    if (owner.links.size() > 1)
        for (std::size_t n = owner.links.size();
             n > 0 && cumulative(world, owner, owner.links.size() - 1) > limits.maximum_height;
             --n)
            set_height(world,
                       owner,
                       n - 1,
                       (n > 1 ? cumulative(world, owner, n - 2) + data.height_offset : 0) + limits.minimum_height,
                       true);
    if (!(data.flags & 0x4000u))
        binding.default_attachment_height = height;
    for (std::size_t i = insertion; i < binding.geometry.size(); ++i)
        binding.geometry[i].geometry_index = static_cast<std::uint32_t>(i);
    //Creation473420/472E20 immediately updates pylon model transforms, even
    //while COS import defers spline/profile generation until its second pass.
    //The next loop piece consumes those real predecessor attachment heights.
    const auto refreshed = refresh_coaster_placements(edit.context, edit.meshes);
    if (!refreshed)
        throw refreshed.error();
    binding.train_state.placement_flags |= 4u;
    mark_owner(world, owner);
    return insertion;
}

void detach_node_from_stack(WorldCoasterRuntime& world, std::size_t r, std::size_t index) {
    auto& binding = world.bindings.at(r);
    auto& owner = world.placements.at(binding.geometry_state.at(index).placement);
    const auto link = level(owner, { r, index });
    std::optional<double> next_height;
    if (!(owner.flags & 1u) && link + 1 < owner.links.size())
        next_height = cumulative(world, owner, link + 1);
    owner.links.erase(owner.links.begin() + static_cast<std::ptrdiff_t>(link));
    if (next_height)
        set_height(world, owner, link, *next_height, true);
}

void erase_node_raw(EditTransaction& edit, std::size_t r, std::size_t index) {
    auto& world = edit.world;
    auto& binding = world.bindings.at(r);
    require(index < binding.geometry.size(), index);
    const auto slot = binding.geometry_state[index].mesh_slot;
    const auto owner_id = binding.geometry_state[index].placement;
    detach_node_from_stack(world, r, index);
    erase_coaster_geometry_peer_node(binding, index);
    binding.geometry.erase(binding.geometry.begin() + static_cast<std::ptrdiff_t>(index));
    binding.geometry_state.erase(binding.geometry_state.begin() + static_cast<std::ptrdiff_t>(index));
    //48A480 uses the predecessor in selected editing mode and the new tail in
    //ordinary backtracking. Station controls restore the SAM/default reference;
    //a loop walks backwards to the preceding normal profile attachment.
    if (!binding.geometry.empty()) {
        auto reference = (binding.train_state.placement_flags & 2u) && index ? index - 1 : binding.geometry.size() - 1;
        const auto& reference_owner = world.placements[binding.geometry_state[reference].placement];
        if (reference_owner.flags & 1u) {
            const auto initial = binding.definition->balance.coaster.fInitialPylonLoftHeight;
            binding.default_attachment_height = initial != 0 ? initial : 10.0f;
            if (!(binding.train_state.placement_flags & 2u))
                binding.train_state.placement_flags &= ~4u;
        } else {
            while (reference && (binding.geometry[reference].geometry_flags & 0x40u))
                --reference;
            require(!(binding.geometry[reference].geometry_flags & 0x40u), reference);
            binding.default_attachment_height =
                profile_position(edit.meshes, world, binding.geometry_state[reference])[1];
        }
    }
    for (auto& owner : world.placements)
        for (auto& ref : owner.links)
            if (ref && ref->ride == r && ref->node > index)
                --ref->node;
    mark_owner(world, world.placements[owner_id]);
    for (std::size_t i = index; i < binding.geometry.size(); ++i)
        binding.geometry[i].geometry_index = static_cast<std::uint32_t>(i);
    if (slot)
        edit.retired.push_back(slot);
    if (index)
        mark(binding, index - 1);
    else {
        binding.geometry_dirty = true;
        binding.train_state.runtime_flags |= 0x1001u;
    }
}

void finish(EditTransaction& edit, std::size_t ride) {
    //Compact only empty placements; shared stacks retain both their owner and
    //their ordering. Saved ordinals are regenerated from the committed graph.
    std::vector<std::size_t> remap(edit.world.placements.size());
    std::size_t next = 0;
    for (std::size_t i = 0; i < edit.world.placements.size(); ++i)
        if (!edit.world.placements[i].links.empty()) {
            remap[i] = next;
            if (i != next)
                edit.world.placements[next] = std::move(edit.world.placements[i]);
            ++next;
        }
    edit.world.placements.resize(next);
    for (auto& binding : edit.world.bindings)
        for (auto& local : binding.geometry_state)
            local.placement = remap.at(local.placement);
    for (std::size_t r = 0; r < edit.world.bindings.size(); ++r)
        if (r == ride || edit.world.bindings[r].geometry_dirty) {
            const auto result = finalize_coaster_geometry_edit(edit.context, edit.meshes, r);
            if (!result)
                throw result.error();
        }
}

void
move_node_raw(EditTransaction& edit, std::size_t r, std::size_t index, std::int32_t x, std::int32_t y, bool validate) {
    auto& c = edit.context;
    auto& meshes = edit.meshes;
    auto& binding = edit.world.bindings.at(r);
    require(index < binding.geometry.size(), index);
    const auto old_owner_id = binding.geometry_state[index].placement;
    const auto old_owner = edit.world.placements[old_owner_id];
    require(!(old_owner.flags & 0x10u));
    if (old_owner.tile_x == x && old_owner.tile_y == y) {
        finish(edit, r);
        return;
    }
    if (validate)
        validate_top_level_terrain(c, x, y);
    const bool closed = (binding.train_state.placement_flags & 1u) != 0;
    //Native begin-object-edit48C640 clears the per-generation action latch.
    binding.train_state.runtime_flags &= ~0x2000u;
    CoasterNodePlacement destination;
    destination.tile_x = x;
    destination.tile_y = y;
    destination.pylon = old_owner.pylon;
    destination.insertion_after = index ? std::optional{ index - 1 } : std::nullopt;
    //Validate the incoming and outgoing turns with this node's candidate
    //location substituted, rather than validating its old tile twice.
    if (validate) {
        const auto validate_at = [&](std::size_t target,
                                     std::array<std::int32_t, 2> target_tile,
                                     std::array<std::int32_t, 2> origin,
                                     std::optional<std::array<std::int32_t, 2>> preceding,
                                     std::uint32_t owner_flags) {
            const auto& owner = edit.world.placements[binding.geometry_state[target ? target - 1 : 0].placement];
            require(validate_coaster_connection(rules(owner),
                                                { origin,
                                                  target_tile,
                                                  preceding,
                                                  static_cast<std::uint32_t>(target ? target - 1 : 0),
                                                  owner_flags })
                        == CoasterPlacementValidity::valid,
                    target);
        };
        if (index) {
            const auto& previous = edit.world.placements[binding.geometry_state[index - 1].placement];
            validate_at(index,
                        { x, y },
                        tile(previous),
                        index > 1
                            ? std::optional{ tile(edit.world.placements[binding.geometry_state[index - 2].placement]) }
                            : std::nullopt,
                        previous.flags);
        }
        if (!(binding.geometry[index].geometry_flags & 0x10000u) && index + 1 < binding.geometry.size()) {
            const auto& next = edit.world.placements[binding.geometry_state[index + 1].placement];
            validate_at(index + 1,
                        tile(next),
                        { x, y },
                        index
                            ? std::optional{ tile(edit.world.placements[binding.geometry_state[index - 1].placement]) }
                            : std::nullopt,
                        old_owner.flags);
            if (!(binding.geometry[index + 1].geometry_flags & 0x10000u)) {
                if (index + 2 < binding.geometry.size())
                    validate_at(index + 2,
                                tile(edit.world.placements[binding.geometry_state[index + 2].placement]),
                                tile(next),
                                std::array{ x, y },
                                next.flags);
                else if (closed && binding.geometry.size() > 2) {
                    //4744BD wraps the moved closed tail's missing second successor to
                    //station head->next, so the closing turn changes with the proposal.
                    validate_at(1,
                                tile(edit.world.placements[binding.geometry_state[1].placement]),
                                tile(next),
                                std::array{ x, y },
                                next.flags);
                }
            }
        }
    }
    const auto old_height = static_cast<float>(cumulative(edit.world, old_owner, level(old_owner, { r, index })));
    const auto previous_count = edit.world.placements.size();
    auto destination_id = find_or_create_owner(edit, r, destination, validate);
    const auto& destination_owner = edit.world.placements[destination_id];
    if (!destination_owner.links.empty()) {
        require(destination_owner.links.front().has_value(), destination_id);
        require(!(node(edit.world, *destination_owner.links.front()).geometry_flags & 0x600u), destination_id);
    }
    if (destination_id == previous_count && old_owner.links.size() == 1) {
        auto replacement = std::move(edit.world.placements.back());
        edit.world.placements.pop_back();
        replacement.links = old_owner.links;
        edit.world.placements[old_owner_id] = std::move(replacement);
        destination_id = old_owner_id;
    } else {
        detach_node_from_stack(edit.world, r, index);
        auto& owner = edit.world.placements[destination_id];
        const auto limits = rules(owner);
        require(owner.links.size() < limits.maximum_stack);
        std::vector<CoasterSupportLevel> levels;
        for (const auto& ref : owner.links) {
            require(ref.has_value());
            levels.push_back({ state(edit.world, *ref).controls[0], node(edit.world, *ref).geometry_flags });
        }
        const auto selected = select_coaster_support_level(old_height, limits, pylon(owner), levels, true);
        std::optional<double> upper;
        if (selected.level < owner.links.size())
            upper = cumulative(edit.world, owner, selected.level);
        owner.links.insert(owner.links.begin() + static_cast<std::ptrdiff_t>(selected.level),
                           CoasterNodeReference{ r, index });
        set_height(edit.world, owner, selected.level, selected.height);
        if (upper)
            set_height(edit.world, owner, selected.level + 1, *upper);
    }
    binding.geometry_state[index].placement = destination_id;
    const auto slot = binding.geometry_state[index].mesh_slot;
    auto* mesh = meshes.instance(slot);
    require(mesh != nullptr, slot);
    mesh->metadata.placement_x = x;
    mesh->metadata.placement_y = y;
    const auto refreshed = meshes.refresh_terrain(c, slot);
    if (!refreshed)
        throw refreshed.error();
    mark_owner(edit.world, edit.world.placements[old_owner_id]);
    if (destination_id != old_owner_id)
        mark_owner(edit.world, edit.world.placements[destination_id]);
    finish(edit, r);
}
} //namespace coaster_editor_detail

std::expected<std::size_t, MeshError>
coaster_editor_detail::place_node_transaction(AttractionWorld& c,
                                              WorldMeshRuntime& meshes,
                                              std::uint32_t map,
                                              const CoasterNodePlacement& placement,
                                              coaster_editor_detail::PlacementValidation placement_validation) {
    using namespace coaster_editor_detail;
    try {
        EditTransaction edit{ c, meshes };
        const auto r = ride_index(edit.world, map);
        const auto index = place_node_raw(edit, r, placement, placement_validation == PlacementValidation::interactive);
        finish(edit, r);
        if (placement.charge_cost && !c.construction.action_recording_disabled) {
            const auto cost = truncated_cost(static_cast<float>(piece_cost(edit.world, r, index)));
            require(c.simulation.current != nullptr);
            auto* payload = c.entities.at(c.simulation.current->saved.bank_account).payload;
            require(payload && payload->kind == ThingKind::bank_account);
            (void)charge_world_bank_account_expense(c, reinterpret_cast<BankRecord*>(payload), cost);
        }
        edit.commit();
        return index;
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<std::size_t, MeshError> place_coaster_node(AttractionWorld& c,
                                                         WorldMeshRuntime& meshes,
                                                         std::uint32_t map,
                                                         const CoasterNodePlacement& placement) {
    return coaster_editor_detail::place_node_transaction(c,
                                                         meshes,
                                                         map,
                                                         placement,
                                                         coaster_editor_detail::PlacementValidation::interactive);
}

std::expected<void, MeshError> set_coaster_node_parameters(AttractionWorld& c,
                                                           WorldMeshRuntime& meshes,
                                                           std::uint32_t map,
                                                           std::size_t index,
                                                           const CoasterNodeParameters& parameters) {
    using namespace coaster_editor_detail;
    try {
        EditTransaction edit{ c, meshes };
        const auto r = ride_index(edit.world, map);
        auto& binding = edit.world.bindings[r];
        require(index < binding.geometry.size(), index);
        require(
            std::isfinite(
                parameters.loft + parameters.heading + parameters.bank + parameters.incline + parameters.loop_rotation),
            index);
        auto& local = binding.geometry_state[index];
        auto& input = binding.geometry[index];
        local.controls[0] = parameters.loft;
        local.angles[3] = parameters.heading;
        input.geometry_flags &= ~7u;
        if (parameters.hold_heading)
            input.geometry_flags |= 1;
        if (parameters.hold_incline)
            input.geometry_flags |= 2;
        if (parameters.hold_bank)
            input.geometry_flags |= 4;
        if (input.geometry_flags & 0x40u) {
            local.angles[1] = local.angles[2] = 0;
            local.controls[4] = parameters.loop_rotation;
        } else {
            local.angles[1] = parameters.bank;
            local.angles[2] = parameters.incline;
            local.controls[4] = 0;
        }
        if (index + 1 == binding.geometry.size()) {
            const auto& owner = edit.world.placements[local.placement];
            if (!(owner.flags & 1u) && !(pylon(owner).flags & 0x4000u))
                binding.default_attachment_height =
                    static_cast<float>((static_cast<double>(pylon(owner).height_maximum) - pylon(owner).height_minimum)
                                           * std::clamp(parameters.loft, 0.0f, 1.0f)
                                       + pylon(owner).height_minimum);
        }
        mark_owner(edit.world, edit.world.placements[local.placement]);
        finish(edit, r);
        edit.commit();
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> swap_coaster_stack_levels(AttractionWorld& c,
                                                         WorldMeshRuntime& meshes,
                                                         std::uint32_t map,
                                                         std::size_t first,
                                                         std::size_t second) {
    using namespace coaster_editor_detail;
    try {
        EditTransaction edit{ c, meshes };
        const auto r = ride_index(edit.world, map);
        auto& binding = edit.world.bindings[r];
        require(first < binding.geometry.size() && second < binding.geometry.size());
        auto& a = binding.geometry_state[first];
        auto& b = binding.geometry_state[second];
        require(a.placement == b.placement);
        auto& owner = edit.world.placements[a.placement];
        require(!(owner.flags & 1u));
        const auto la = level(owner, { r, first }), lb = level(owner, { r, second });
        std::swap(owner.links[la], owner.links[lb]);
        std::swap(a.controls[0], b.controls[0]);
        mark_owner(edit.world, owner);
        finish(edit, r);
        edit.commit();
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError>
remove_coaster_node(AttractionWorld& c, WorldMeshRuntime& meshes, std::uint32_t map, std::size_t index) {
    using namespace coaster_editor_detail;
    try {
        EditTransaction edit{ c, meshes };
        const auto r = ride_index(edit.world, map);
        auto& binding = edit.world.bindings[r];
        require(index < binding.geometry.size(), index);
        require(!(edit.world.placements[binding.geometry_state[index].placement].flags & 0x10u), index);
        //A non-tail command is a selected edit:48A480 takes the predecessor as
        //its new reference, while ordinary open backtracking takes the tail.
        //Keep that selection local to this atomic host command.
        const auto selected = binding.train_state.placement_flags & 2u;
        if (index + 1 < binding.geometry.size())
            binding.train_state.placement_flags |= 2u;
        erase_node_raw(edit, r, index);
        binding.train_state.placement_flags = (binding.train_state.placement_flags & ~2u) | selected;
        finish(edit, r);
        edit.commit();
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}

std::expected<void, MeshError> move_coaster_node(AttractionWorld& c,
                                                 WorldMeshRuntime& meshes,
                                                 std::uint32_t map,
                                                 std::size_t index,
                                                 std::int32_t x,
                                                 std::int32_t y) {
    using namespace coaster_editor_detail;
    try {
        EditTransaction edit{ c, meshes };
        const auto r = ride_index(edit.world, map);
        auto& binding = edit.world.bindings.at(r);
        require(index < binding.geometry.size(), index);
        const auto& source = edit.world.placements.at(binding.geometry_state[index].placement);
        if (source.tile_x == x && source.tile_y == y) {
            move_node_raw(edit, r, index, x, y, true);
            edit.commit();
            return {};
        }
        auto* bank = editing_bank(c);
        const bool closed = (binding.train_state.placement_flags & 1u) != 0;
        const auto old_cost = truncated_cost(piece_cost(edit.world, r, index, false, false));
        const auto old_following = closed && index + 1 < binding.geometry.size()
                                     ? truncated_cost(piece_cost(edit.world, r, index + 1, false, false))
                                     : 0;
        move_node_raw(edit, r, index, x, y, true);
        auto expense = rounded_cost(static_cast<float>(piece_cost(edit.world, r, index, false, true)));
        if (closed && index + 1 < binding.geometry.size()) {
            const auto& center = edit.world.placements.at(binding.geometry_state[index].placement).position;
            expense = sum_costs(
                expense,
                rounded_cost(static_cast<float>(
                    piece_cost(edit.world, r, index + 1, false, false, std::array{ center[0] + 5, center[2] + 5 }))));
        }
        edit.commit();
        settle_cost(c, bank, sum_costs(old_cost, old_following), expense);
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
