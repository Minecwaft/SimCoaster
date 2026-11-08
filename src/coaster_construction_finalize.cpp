#include "coaster_editor_internal.hpp"
#include <limits>

namespace simcoaster {
std::expected<void, MeshError> finalize_coaster_geometry_edit(AttractionWorld& c, WorldMeshRuntime& meshes, std::size_t r) {
    using namespace coaster_editor_detail;
    try {
        auto& world = runtime(meshes);
        require(r < world.bindings.size(), r);
        auto& binding = world.bindings[r];
        const auto owners = refresh_coaster_placements(c, meshes);
        if (!owners) {
            throw owners.error();
        }
        const auto generated = refresh_coaster_geometry(c, meshes, r);
        if (!generated) {
            throw generated.error();
        }
        //481B30 clears this geometry-build latch after samples/profile/bounds and
        //generated mesh publication. The frame's train-update latch is separate.
        binding.train_state.runtime_flags &= ~0x4000u;
        const auto old = std::move(binding.saved_geometry.geometry);
        binding.saved_geometry.geometry.clear();
        constexpr std::array runtime_bits { 0x80u, 0x40u, 0x100u, 0x10000u, 0x20000u, 0x40000u, 0x80000u, 0x200000u, 0x400000u };
        constexpr std::array wire_bits { 8u, 0x10u, 0x20u, 0x40u, 0x80u, 0x100u, 0x200u, 0x400u, 0x800u };
        require(binding.geometry.size() == binding.geometry_state.size());
        for (std::size_t i = 0; i < binding.geometry_state.size(); ++i) {
            auto& local = binding.geometry_state[i];
            const auto& owner = world.placements.at(local.placement);
            const auto flags = binding.geometry[i].geometry_flags;
            if (owner.flags & 0x10u) {
                local.saved_ordinal.reset();
                continue;
            }
            CoasterGeometrySave saved;
            if (local.saved_ordinal && *local.saved_ordinal < old.size()) {
                saved.peers = old[*local.saved_ordinal].peers;
            }
            auto& header = saved.header;
            header.packed_flags = flags & 7u;
            for (std::size_t bit = 0; bit < wire_bits.size(); ++bit) {
                if (flags & runtime_bits[bit]) {
                    header.packed_flags |= wire_bits[bit];
                }
            }
            header.mesh_slot = local.mesh_slot;
            header.tile_x = static_cast<std::int16_t>(owner.tile_x);
            header.tile_y = static_cast<std::int16_t>(owner.tile_y);
            const auto link = level(owner, { r, i });
            require(link <= std::numeric_limits<std::uint8_t>::max(), link);
            header.link_slot = static_cast<std::uint8_t>(link);
            header.loft = local.controls[0];
            header.angle_a = local.angles[1];
            header.angle_b = local.angles[2];
            header.rotation = local.angles[3];
            header.wobble = local.controls[4];
            local.saved_ordinal = binding.saved_geometry.geometry.size();
            binding.saved_geometry.geometry.push_back(std::move(saved));
        }
        require(binding.saved_geometry.geometry.size() <= std::numeric_limits<std::uint16_t>::max());
        binding.saved_geometry.header.geometry_count = static_cast<std::uint16_t>(binding.saved_geometry.geometry.size());
        capture_coaster_geometry_peer_snapshot(binding);
        //Geometry rebuild and train reseeding are separate native phases. Retain
        //the dirty/latch flags until the normal full-frame consumer has installed
        //the rebuilt train count, placements, car poses and texture-phase owners.
        binding.geometry_dirty = true;
        binding.train_state.runtime_flags |= 0x1001u;
        return {};
    } catch (const MeshError& error) {
        return std::unexpected(error);
    }
}
} //namespace simcoaster
