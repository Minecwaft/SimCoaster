#include "native_coaster_editor_internal.hpp"
#include <cstring>
#include <tuple>

namespace simcoaster {
using namespace native_coaster_detail;

void NativeCoasterEditor::State::insert_intermediate_support(AttractionWorld& c, std::size_t index, std::optional<WorldCellCoordinates> chosen, bool recording) {
    auto& initial = ride(c);
    if (index + 1 >= initial.geometry.size() || (initial.geometry[index].geometry_flags & 128u) || (initial.geometry[index].geometry_flags & 0x10000u) || (initial.geometry[index + 1].geometry_flags & 0x10000u)) {
        return;
    }
    const auto& first = world(c).placements.at(initial.geometry_state[index].placement);
    const auto& second = world(c).placements.at(initial.geometry_state[index + 1].placement);
    const auto a = coordinate(first), b = coordinate(second);
    const auto pylon_index = (initial.geometry[index].geometry_flags & 64u) || (interaction & 0x4040u) ? 1u : 0u;
    const auto rules = coaster_placement_rules(initial.definition->balance.pylons.at(pylon_index), pylon_index != 0);
    const bool exception = index != 2 && (first.flags & 2u);
    const WorldCellCoordinates midpoint { std::min(a.x, b.x) + std::abs(b.x - a.x) / 2, std::min(a.y, b.y) + std::abs(b.y - a.y) / 2 };
    const auto valid = [&](WorldCellCoordinates p) {
        if (!construction_detail::bounds(p) || construction_detail::map(c, construction_detail::cell_id(p)).base.common.type) {
            return false;
        }
        const auto distance = [&](WorldCellCoordinates endpoint) {
            const auto dx = p.x - endpoint.x, dy = p.y - endpoint.y;
            const auto squared = static_cast<std::uint32_t>(dx * dx + dy * dy);
            return exception || (squared >= rules.minimum_distance_squared && squared <= rules.maximum_distance_squared);
        };
        return distance(a) && distance(b);
    };
    //48DEC0 advances before each test, includes the successor endpoint, favors
    //the first strict Manhattan improvement, and uses X as the major-axis tie.
    const auto dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
    const auto sx = (b.x > a.x) - (b.x < a.x), sy = (b.y > a.y) - (b.y < a.y);
    auto p = a;
    auto error = (dx >= dy ? dx : dy) / 2;
    std::optional<WorldCellCoordinates> best;
    int best_distance = 99999;
    for (int step = 0, count = std::max(dx, dy); step < count; ++step) {
        if (dx >= dy) {
            error += dy;
            if (error >= dx) {
                error -= dx;
                p.y += sy;
            }
            p.x += sx;
        } else {
            error += dx;
            if (error >= dy) {
                error -= dy;
                p.x += sx;
            }
            p.y += sy;
        }
        if (valid(p)) {
            const auto distance = std::abs(p.x - midpoint.x) + std::abs(p.y - midpoint.y);
            if (distance < best_distance) {
                best_distance = distance;
                best = p;
            }
        }
    }
    if (chosen) {
        require(best && equal(*best, *chosen), "recorded insertion midpoint does not match the native candidate scan");
        best = chosen;
    }
    if (!best) {
        return;
    }
    require(index <= std::numeric_limits<std::uint16_t>::max() && *map <= std::numeric_limits<std::uint16_t>::max(), "insertion exceeds recorder IDs");
    const CoasterInsertReplayCommand command { static_cast<std::uint16_t>(*map), static_cast<std::uint16_t>(index), static_cast<std::uint8_t>(best->x), static_cast<std::uint8_t>(best->y) };
    const bool writes = recording && !c.construction.action_recording_disabled && c.construction.action_record_streams_initialized;
    auto* destination = writes ? dynamic_cast<LayoutActionStream*>(c.construction.action_record_stream) : nullptr;
    require(!writes || (destination && destination->cursor() == destination->bytes().size()), "insertion needs the owned append-position recorder");
    const auto record_size = destination ? destination->bytes().size() : 0;
    LayoutActionStream journal, packet;
    if (writes) {
        const auto encoded = write_layout_action(packet, make_coaster_insert_action(command));
        require(encoded.has_value(), "cannot encode complete insertion");
    }
    State saved = *this;
    auto saved_construction = construction.state;
    auto saved_staff = c.staff.interaction;
    auto context_fields = std::tie(c.construction.mode,
                                   c.construction.path_start_x,
                                   c.construction.path_start_y,
                                   c.construction.previous_path_step_x,
                                   c.construction.previous_path_step_y,
                                   c.construction.track_type,
                                   c.construction.selected_track_type,
                                   c.construction.coaster_editor_active,
                                   c.construction.handler,
                                   c.construction.grid_change_serial_counter,
                                   c.construction.line_endpoint_dispatch,
                                   c.construction.endpoint_stop_pending,
                                   c.construction.last_successful_type,
                                   c.construction.cell_line_first_dispatch_pending,
                                   c.construction.track_endpoint_phase,
                                   c.construction.track_endpoint_stop_pending,
                                   c.construction.track_anchor_removal_enabled,
                                   c.construction.origin_cell,
                                   c.construction.park_object,
                                   c.construction.interaction_cursor_anchor,
                                   c.construction.interaction_secondary_anchor,
                                   c.construction.world_cell_render_scratch,
                                   c.construction.world_cell_mesh_render_scratch,
                                   c.construction.cell_costs_suppressed,
                                   c.construction.force_cell_removal,
                                   c.view.secondary_cursor_record_snapshot,
                                   c.simulation.current->saved.random_seed,
                                   c.construction.tile_variant_toggle,
                                   c.resources.renderer_cache,
                                   c.resources.renderer_cache_generation);
    auto saved_context = std::apply(
        [](const auto&... value) {
            return std::tuple { value... };
        },
        context_fields);
    std::array<std::uint32_t, 33 * 33> saved_grid;
    static_assert(sizeof(saved_grid) == sizeof(c.simulation.current->grid_change_serials));
    std::memcpy(saved_grid.data(), c.simulation.current->grid_change_serials, sizeof(saved_grid));
    const auto cells = std::make_unique<decltype(World::map_cells)>(c.simulation.current->map_cells);
    const auto tracks = std::make_unique<decltype(World::track_cells)>(c.simulation.current->track_cells);
    auto& bank = construction_detail::bank(c);
    const auto saved_bank = bank;
    auto* analyser = c.entities.at_payload<ParkAnalyserState>(c.simulation.current->saved.park_analyser);
    require(analyser != nullptr, "insertion has no park analyser");
    const std::array saved_finance { analyser->long_histories[3].temporary, analyser->long_histories[6].temporary };
    construction_detail::RestoreValue restore_recorder { c.construction.action_record_stream };
    if (!c.construction.action_recording_disabled && c.construction.action_record_streams_initialized) {
        c.construction.action_record_stream = &journal;
    }
    try {
        coaster_editor_detail::EditTransaction transaction { c, meshes(c), true };
        const auto successor_tile = b;
        const auto map_id = *map;
        delete_range(c, construction, static_cast<std::uint16_t>(index), static_cast<std::uint16_t>(index + 1), true);
        const auto retained_current = current;
        const auto retained_validation = validation;
        auto* editor = c.construction.editor;
        require(editor != nullptr, "coaster insertion has no editor owner");
        restore_build_tools_after_delete(c);
        //487270(1) deactivates retained UI/context.48E270 then calls raw46EF00,
        //which restores the runtime's gap pointers without a SETEDIT packet.
        editor->bind_catalog_context(c, map_id);
        current = retained_current;
        validation = retained_validation;
        construction.set_mode(c, 21, false);
        interaction &= ~0x4040u;
        stage = Stage::idle;
        remaining = 0;
        //Native48E270 invokes474B60 directly here. The preceding Bresenham scan
        //owns midpoint eligibility; adding473D60's live recursive turn check can
        //reject an insertion the original reconstruction explicitly performs.
        if (!place(c, construction, *best, true, coaster_editor_detail::PlacementValidation::native_reconstruction)) {
            throw std::logic_error("Native coaster insertion failed to create its eligible midpoint");
        }
        //48E270's second474B60 call suppresses cost; it normally meets the retained
        //validation boundary and closes the gap rather than duplicating that node.
        if (!place(c, construction, successor_tile, false, coaster_editor_detail::PlacementValidation::native_reconstruction)) {
            throw std::logic_error("Coaster insertion failed to restore its successor");
        }
        cleanup(c, construction);
        selected_tools = std::to_underlying(CoasterEditorTool::adjust_support_height);
        active_tool = CoasterEditorTool::adjust_support_height;
        geometry_action_enabled = false; //Native edit flags0 after closure.
        //Nested helpers retain native economic gates, but their incomplete packets
        //are discarded in favor of this complete operation.
        if (writes && destination->write(packet.bytes()) != static_cast<std::int32_t>(packet.bytes().size())) {
            throw std::runtime_error("cannot publish complete coaster insertion");
        }
        transaction.commit();
    } catch (...) {
        if (destination) {
            destination->truncate(record_size);
        }
        c.simulation.current->map_cells = *cells;
        c.simulation.current->track_cells = *tracks;
        bank = saved_bank;
        construction.state = std::move(saved_construction);
        c.staff.interaction = std::move(saved_staff);
        context_fields = std::move(saved_context);
        std::memcpy(c.simulation.current->grid_change_serials, saved_grid.data(), sizeof(saved_grid));
        analyser->long_histories[3].temporary = saved_finance[0];
        analyser->long_histories[6].temporary = saved_finance[1];
        restore_snapshot(std::move(saved));
        throw;
    }
}

void NativeCoasterEditor::State::restore_snapshot(State&& saved) {
    template for (constexpr auto member : std::define_static_array(std::meta::nonstatic_data_members_of(^^State, std::meta::access_context::current()))) {
        using Member = [:std::meta::type_of(member):];
        if constexpr (!std::is_reference_v<Member>) {
            this->[:member:] = std::move(saved.[:member:]);
        }
    }
}

void NativeCoasterEditor::insert_support(AttractionWorld& c, const CoasterInsertReplayCommand& command, bool recording) {
    const auto decoded = decode_coaster_insert_action(make_coaster_insert_action(command));
    require(decoded && decoded->has_value(), "invalid insertion operands");
    auto& s = *state_;
    require(s.map && *s.map == command.map, "insertion has no matching active context");
    const auto& r = s.ride(c);
    require(command.ordinal + 1u < r.geometry.size() && !(r.geometry[command.ordinal].geometry_flags & (128u | 0x10000u)) && !(r.geometry[command.ordinal + 1u].geometry_flags & 0x10000u), "insertion ordinal is not eligible");
    s.insert_intermediate_support(c, command.ordinal, WorldCellCoordinates { command.x, command.y }, recording);
}
} //namespace simcoaster
