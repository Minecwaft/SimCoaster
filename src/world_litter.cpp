#include "world_person_internal.hpp"
#include "simcoaster/world_grid.hpp"

namespace simcoaster {
void initialize_world_litter_record(WorldRuntime& c, MapCell& cell, LitterSelector selector) {
    person_detail::initialize_mode(c);
    if (c.mode.game == GameMode::online || cell.litter_selector)
        return;
    const auto code = static_cast<std::uint32_t>(selector);
    auto program = code >= 1 && code <= 12 ? code : 6;
    if (selector == LitterSelector::vomit)
        add_world_re_cell_effect_profile(c.simulation.region_effects,
                                         RegionEffectProfileId::vomit_puddle,
                                         cell.base.index);
    else if (selector == LitterSelector::stink_bomb)
        add_world_re_cell_effect_profile(c.simulation.region_effects,
                                         RegionEffectProfileId::stink_bomb,
                                         cell.base.index);
    cell.litter_request = world_operations::allocate_resource_variant(c,
                                                                      { ResourceProgramFamily::litter, program },
                                                                      { 15, 0, 0, 0, 0, 0 });
    auto& world = person_detail::base_world(c);
    world.saved.random_seed = cell.base.index;
    const auto index = static_cast<std::uint32_t>(cell.base.index) - 1u;
    const auto tile_x = static_cast<float>(index & 127u), tile_y = static_cast<float>(index >> 7);
    const auto x_jitter = (static_cast<double>(next_world_prng_value(world) & 3) - 2.0) * 0.1;
    const auto y = static_cast<float>(
        (static_cast<double>(next_world_prng_value(person_detail::base_world(c)) & 3) - 2.0) * 0.1 + tile_y + 0.5);
    const auto x = static_cast<float>(x_jitter + tile_x + 0.5);
    world_operations::set_resource_variant_transform(c, cell.litter_request, { x * 10.0f, 0.0f, y * 10.0f }, 0);
    cell.litter_collector = 0;
    cell.litter_selector = code;
    if (selector == LitterSelector::stink_bomb) {
        const auto coordinate = [](float value) {
            return std::bit_cast<std::int32_t>(
                static_cast<std::uint32_t>(static_cast<std::int64_t>(static_cast<double>(value) * 10240.0)));
        };
        cell.litter_auxiliary_request =
            c.ports.effects.create_particle_effect(c, 50, { coordinate(x), 0, coordinate(y) }, 0);
    }
}
} //namespace simcoaster
