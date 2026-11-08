#include "simcoaster/world_grid.hpp"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <stdexcept>

namespace simcoaster {
namespace {
const RegionEffectProfile& profile(WorldRegionEffectsView view, RegionEffectProfileId id) {
    if (!view.cells || !view.profiles) {
        throw std::invalid_argument("unbound region-effect arrays");
    }
    const auto index = std::to_underlying(id);
    if (index < 0 || index >= 15) {
        throw std::out_of_range("region-effect profile");
    }
    return view.profiles[index];
}

std::int32_t apply(WorldRegionEffectsView view, const RegionEffectProfile& selected, bool add, CellId center, std::uint16_t radius) {
    const auto cell = static_cast<std::uint32_t>(center) - 1u;
    const auto cx = static_cast<std::int32_t>(cell & 127u), cy = static_cast<std::int32_t>(cell >> 7);
    const auto r = static_cast<std::int32_t>(radius), x_end = std::min(cx + r, 127), y_end = std::min(cy + r, 127);
    const auto sign = add ? 1 : -1;
    constexpr auto channels = std::define_static_array(std::meta::nonstatic_data_members_of(^^RegionEffectCell, std::meta::access_context::current()));
    for (auto x = std::max(cx - r, 0); x <= x_end; ++x) {
        for (auto y = std::max(cy - r, 0); y <= y_end; ++y) {
            auto& target = view.cells[static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)];
            const auto distance = std::abs(cx - x) + std::abs(cy - y) + 1;
            template for (constexpr auto member : channels) {
                auto& value = target.[:member:];
                const auto delta = sign * selected.effects.[:member:] / distance;
                value = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(std::bit_cast<std::uint16_t>(value) + delta));
            }
        }
    }
    //Incidental final native loop value, preserved for the thin add/remove APIs.
    return cx - x_end - 1;
}
} //namespace

std::int32_t apply_world_re_cell_effect_profile(WorldRegionEffectsView view, bool add, RegionEffectProfileId id, CellId center) {
    const auto& selected = profile(view, id);
    return apply(view, selected, add, center, selected.radius);
}

std::int32_t apply_world_re_cell_effect_profile_with_radius(WorldRegionEffectsView view, bool add, RegionEffectProfileId id, CellId center, std::uint16_t radius) {
    return apply(view, profile(view, id), add, center, radius);
}

std::int32_t add_world_re_cell_effect_profile(WorldRegionEffectsView view, RegionEffectProfileId id, CellId center) {
    return apply_world_re_cell_effect_profile(view, true, id, center);
}

std::int32_t remove_world_re_cell_effect_profile(WorldRegionEffectsView view, RegionEffectProfileId id, CellId center) {
    return apply_world_re_cell_effect_profile(view, false, id, center);
}

std::int32_t add_world_re_cell_effect_profile_with_radius(WorldRegionEffectsView view, RegionEffectProfileId id, CellId center, std::uint16_t radius) {
    return apply_world_re_cell_effect_profile_with_radius(view, true, id, center, radius);
}

std::int32_t remove_world_re_cell_effect_profile_with_radius(WorldRegionEffectsView view, RegionEffectProfileId id, CellId center, std::uint16_t radius) {
    return apply_world_re_cell_effect_profile_with_radius(view, false, id, center, radius);
}
} //namespace simcoaster
