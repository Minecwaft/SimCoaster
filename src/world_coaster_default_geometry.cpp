#include "coaster_editor_internal.hpp"
#include "coaster_station_geometry.hpp"

namespace simcoaster {
std::expected<void, MeshError>
initialize_fresh_coaster_geometry(AttractionWorld& c, WorldMeshRuntime& meshes, std::size_t r) {
    using namespace coaster_editor_detail;
    try {
        EditTransaction edit{ c, meshes };
        require(r < edit.world.bindings.size(), r);
        auto& binding = edit.world.bindings[r];
        require(binding.geometry.empty());
        auto* parent = meshes.instance(binding.mesh_slot);
        require(parent != nullptr, binding.mesh_slot);
        const auto& model = parent->playback.model;
        const auto poses = evaluate_mesh_pose(model);
        if (!poses)
            throw poses.error();
        const auto root_y = model.root ? model.nodes[*model.root].local_transform[13] : 0;
        constexpr std::array categories{ 3u, 10u, 2u, 9u, 10u }, variants{ 0u, 0u, 0u, 0u, 1u };
        //Every successfully configured coaster has native configuration bit0x20:
        //46D890 ORs0xA0 after resource construction. Resource IDs1/2/3 are
        //therefore the actual station entry, inline middle and exit controls for
        //this owner.
        for (std::uint32_t id = 1; id <= 3; ++id)
            if (const auto resource = model.find_resource(0x400000u, id)) {
                const auto transformed = mesh_attachment_transform(model, *poses, *resource);
                if (!transformed)
                    throw transformed.error();
                const std::array point{ (*transformed)[12], (*transformed)[13], (*transformed)[14] };
                CoasterPlacement owner;
                owner.flags = id == 2 ? 19u : 18u;
                owner.tile_x = static_cast<std::int32_t>(static_cast<double>(point[0]) * static_cast<double>(0.1f));
                owner.tile_y = static_cast<std::int32_t>(static_cast<double>(point[2]) * static_cast<double>(0.1f));
                CoasterGeometryObjectInput input;
                input.geometry_index = static_cast<std::uint32_t>(binding.geometry.size());
                input.inline_position = point;
                CoasterNodeState local;
                local.placement = edit.world.placements.size();
                if (id == 2) {
                    set_explicit_owner_position(c, owner, point);
                    input.geometry_flags = 0x1E00u;
                } else {
                    owner.definition = binding.definition;
                    const auto& data = binding.definition->pylons[0];
                    const auto template_id = binding.definition->pylon_templates[0];
                    const auto* source = meshes.definition(template_id);
                    require(source != nullptr, template_id);
                    const auto* terrain = c.construction.active_terrain_model_data
                                            ? c.construction.active_terrain_model_data->terrain_grid
                                            : nullptr;
                    require(terrain != nullptr);
                    const auto height =
                        mesh_placement_reference_height(*terrain, { owner.tile_x, owner.tile_y }, nullptr, 1, 1, 0, 0);
                    if (!height)
                        throw height.error();
                    owner.position = { static_cast<float>(owner.tile_x) * 10,
                                       *height,
                                       static_cast<float>(owner.tile_y) * 10 };
                    DefinitionGrid shape{};
                    shape.width = source->footprint_width;
                    shape.height = source->footprint_height;
                    edit.reserve_mesh_creation();
                    const auto created = meshes.create(c,
                                                       { static_cast<std::int32_t>(template_id),
                                                         0x3FAu,
                                                         { 0, owner.tile_x, owner.tile_y, &shape },
                                                         0,
                                                         0,
                                                         nullptr,
                                                         0,
                                                         0,
                                                         -1,
                                                         0 });
                    if (!created)
                        throw created.error();
                    local.mesh_slot = *created;
                    edit.created.push_back(*created);
                    for (std::size_t channel = 0; channel < 5; ++channel)
                        if (data.flags & (32u << channel)) {
                            const auto selected = meshes.request(c,
                                                                 *created,
                                                                 channel,
                                                                 { { categories[channel], variants[channel] }, 2, 1 });
                            if (!selected)
                                throw selected.error();
                        }
                    meshes.instance(*created)->playback.runtime_flags |= 0x81000u;
                    require(data.height_maximum != data.height_minimum);
                    local.controls[0] = coaster_station_detail::loft_parameter(data, point[1], root_y);
                    input.geometry_flags = id == 1 ? (0x80000u | 0x400u | 0x1800u) : (0x40000u | 0x200u);
                    input.has_runtime_data = true;
                    input.runtime_data_flags = data.flags;
                    input.minimum_tangent_dot = data.minimum_tangent_dot;
                }
                input.geometry_flags |= 0x200000u;
                owner.links.push_back(CoasterNodeReference{ r, binding.geometry.size() });
                edit.world.placements.push_back(std::move(owner));
                binding.geometry.push_back(std::move(input));
                binding.geometry_state.push_back(local);
                insert_coaster_geometry_peer_node(binding, binding.geometry.size() - 1);
            }
        require(binding.geometry.size() == 3, binding.mesh_slot);
        //Native also preallocates a separate zero-position preview object with
        //no place in the geometry chain. The host editor owns transient previews
        //only while editing; it does not retain that unused raw-pointer
        //placeholder.
        const auto placements = refresh_coaster_placements(c, meshes);
        if (!placements)
            throw placements.error();
        //476BE0 schedules profile/sample generation; it does not publish those
        //metrics during allocation. Preserve zero metrics, zero active trains and
        //the zero animation multiplier until the normal first frame consumes1001.
        binding.geometry_dirty = true;
        binding.train_state.runtime_flags |= 0x1001u;
        binding.saved_geometry.header.geometry_count = 0;
        edit.commit();
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
