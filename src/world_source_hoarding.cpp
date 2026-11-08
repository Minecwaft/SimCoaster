#include "simcoaster/world_source_hoarding.hpp"
#include "park_construction_internal.hpp"
#include "simcoaster/sam_record.hpp"
#include <filesystem>

namespace simcoaster {
namespace {
using HoardingClicks = HoardingBalance;

struct ResetHoardingMode {
    WorldRuntime& context;

    ~ResetHoardingMode() {
        context.construction.world_hoarding_mode = 0;
    }
};
} //namespace

void initialize_source_hoarding_interaction(WorldRuntime& c, ParkConstructionRuntime& editor) {
    using namespace construction_detail;
    bind_land_boundary_anchors(c);
    const WorldCellCoordinates start{ c.construction.hoarding_chain_start_x, c.construction.hoarding_chain_start_y };
    const bool small_debug_boundary = (c.mode.graphics & 0x400u) != 0;
    std::vector<WorldCellCoordinates> points;
    if (small_debug_boundary) {
        for (const auto delta :
             std::array<CellLocalOffset, 6>{ { { 0, 0 }, { 2, 0 }, { 2, 4 }, { -10, 4 }, { -10, 0 }, { -8, 0 } } })
            points.push_back({ start.x + delta.x, start.y + delta.y });
    } else if (c.mode.game == GameMode::code_3) {
        const auto path = std::filesystem::path{ build_selected_resource_levels_path(c) } / "cem_Hoardings.sam";
        const auto source = c.ports.assets.read_balance_asset(c, path.native_encoded_string());
        HoardingClicks clicks;
        if (!source || !sam::parse_record(clicks, *source))
            throw std::runtime_error("Missing or malformed original CEM hoarding clicks");
        bool terminated{};
        for (const auto& point : clicks.points) {
            if (point.x == 512) {
                terminated = true;
                break;
            }
            const auto x = std::int64_t(start.x) + point.x;
            const auto y = std::int64_t(start.y) + point.y;
            if (x < 0 || x >= 128 || y < 0 || y >= 128)
                throw std::runtime_error("Original CEM hoarding click lies outside World");
            points.push_back({ static_cast<std::int32_t>(x), static_cast<std::int32_t>(y) });
        }
        if (!terminated)
            throw std::runtime_error("Original CEM hoarding clicks have no X512 terminator");
    }
    c.construction.world_hoarding_mode = 1;
    ResetHoardingMode reset{ c };
    editor.state.direction = 4;
    editor.state.hoarding_origin = start;
    editor.set_mode(c, 25, true);
    editor.refresh_origin(c, true);
    if (small_debug_boundary || c.mode.game == GameMode::code_3) {
        for (const auto point : points)
            (void)editor.dispatch(c, point, WorldInputModifierMask::none);
        set_hoarding_chain_nomodify(c, 1);
        editor.apply_land_boundary_ownership(c);
        set_hoarding_chain_nomodify(c, 0);
        editor.trace_land_boundary(c, true);
        editor.set_mode(c, 0, true);
    }
    if (c.mode.game == GameMode::code_3) {
        //Native indices are zero-based map array offsets, not guessed positions
        //relative to the current gate. Preserve the original raw map command5.
        (void)editor.command(c, world(c).map_cells[2223].base, 5, 0, 0);
        (void)editor.command(c, world(c).map_cells[2224].base, 5, 0, 0);
    }
    for (int x = c.construction.hoarding_target_x + 2; x < start.x; ++x)
        for (int y = start.y; y <= start.y + 1; ++y) {
            auto& cell = map(c, cell_id({ x, y })).base;
            clear_world_cell_flags_mask(&cell, 64);
            refresh_world_cell_render_state(c, &cell);
        }
    c.construction.world_hoarding_mode = 0;
    if (!small_debug_boundary && c.mode.game != GameMode::code_3) {
        const auto path = std::filesystem::path{ build_selected_resource_levels_path(c) } / "hoardings.map";
        if (!apply_hoardings_map_cells_to_world(c, path.native_encoded_string()))
            //Original677CF0 ignores the loader's false result. Intro intentionally
            //ships no map; malformed/short maps retain their already applied prefix.
            c.ports.diagnostics.log_debug_message(c, "Original source hoardings.map was absent or incomplete");
    }
    c.ports.meshes.rebuild_terrain_cell_shading(c);
    world_operations::refresh_active_park_object_meshes(c);
    sync_land_boundary_meshes(c);
}
} //namespace simcoaster
