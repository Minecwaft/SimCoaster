#include "park_construction_internal.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_save.hpp"

namespace simcoaster {
namespace {
using namespace construction_detail;

bool raised_preview_cell(int type) {
    return type == 11 || type == 16 || type == 25 || type == 7 || footprint(type);
}

float cell_height(ConstructionWorld& c, const CellBase& row, float carried) {
    if (!raised_preview_cell(row.common.type))
        return carried;
    const auto p = coordinate(row.index);
    return static_cast<float>(fixed::add(fixed::truncate_integer(sample_world_map_cell_surface(c, p.x, p.y)), 9) / 10)
         * 10;
}

float block_height(ConstructionWorld& c, const CellBase& row) {
    //Native6804D0 carries the previous value across base,Y+1,X+1,X+1/Y+1.
    //A qualifying later cell REPLACES it; this is deliberately not a maximum.
    auto height = cell_height(c, row, 0);
    auto& base = map(c, row.index).base;
    height = cell_height(c, base, height);
    for (const auto offset : std::array<CellLocalOffset, 3>{ { { 0, 1 }, { 1, 0 }, { 1, 1 } } })
        if (const auto* neighbor = resolve_world_cell_by_scaled_local_offset(c, &base, offset, 1))
            height = cell_height(c, neighbor->base, height);
    return height;
}
} //namespace

void ParkConstructionRuntime::validate_placement_parent(ConstructionWorld& c, const ResearchCatalogDefinition& d) {
    const bool dependent = d.balance->AddOn.UpgradeType || d.balance->Info.IsPhotoBooth;
    const bool track_attachment = d.balance->AddOn.UpgradeType == 1;
    if (!dependent)
        return;
    refresh_origin(c);
    auto* parent = c.construction.park_object;
    if (!parent)
        throw std::invalid_argument("attachment placement requires a selected park object");
    if (!d.balance->AddOn.UpgradesAnyRide
        && d.balance->AddOn.UpgradesId
        && d.balance->AddOn.UpgradesId != parent->definition_id)
        throw std::invalid_argument("attachment is incompatible with the selected park object");
    if (track_attachment && !parent->track_ride_handle)
        throw std::invalid_argument("track attachment has no compatible ride runtime");
    //Upgrade templates can inherit WhichTrackType1 for water parents. Native
    //compatibility is the explicit UpgradesId plus the resolved track root.
    if (d.balance->Info.IsPhotoBooth && parent->photo_booth)
        throw std::invalid_argument("selected ride already has a photo booth");
}

bool
ParkConstructionRuntime::preview_track_cell(ConstructionWorld& c, CellBase& cell, std::uint32_t code, int x, int y) {
    using namespace construction_detail;
    const auto p = coordinate(cell.index);
    const auto height = block_height(c, cell);
    int material = 0, geometry = 1;
    if (code == 4 || ((code == 11 || code == 16) && x == 5)) {
        material = x;
        geometry = y;
    }
    const std::uint8_t direction = y == -2 ? 1 : y == 2 ? 16 : x == -2 ? 64 : x == 2 ? 4 : 0;
    auto result = evaluate_track(c, cell, static_cast<int>(code), false, direction);
    if (code == 139)
        result = TrackInteractionResult::track_endpoint;
    const bool accepted = result != TrackInteractionResult::rejected;
    if (accepted) {
        if (result != TrackInteractionResult::accept)
            material = std::to_underlying(result);
    } else {
        (void)evaluate_track(c, cell, static_cast<int>(code), true, direction);
        state.status_latch = true;
        material = 1;
        geometry = 1;
    }
    const auto face = [&](float level, std::uint32_t side, int style, int selector, bool skip) {
        if (state.faces.size() >= 2054 || (style == 1 && !state.show_red_faces))
            return;
        state.faces.push_back({ { static_cast<float>(p.x) * 10, level + 1.5f, static_cast<float>(p.y) * 10 },
                                side,
                                static_cast<std::uint32_t>(style),
                                20,
                                10,
                                selector,
                                skip });
    };
    //Native680A10 passes extent_selector1, which is a20-unit horizontal
    //square with10-unit vertical wall panels. The top never skips adjustment
    //unless the global slow-frame gate overrides it.
    face(height, 0, material, geometry, state.frame_rate_below_clip);
    for (const auto side : std::array<std::uint8_t, 4>{ 1, 4, 16, 64 }) {
        const auto* neighbor = resolve_track_cell_in_direction(c, &cell, side, 2);
        if (!neighbor)
            continue;
        const auto bottom = fixed::truncate_integer(block_height(c, neighbor->base));
        const auto top = fixed::truncate_integer(height);
        for (int level = bottom; level < top; level = fixed::add(level, 10))
            face(static_cast<float>(level),
                 side,
                 accepted ? 0 : 1,
                 0,
                 state.frame_rate_below_clip
                     || (accepted && result == TrackInteractionResult::skip_coordinate_adjustment));
    }
    return true;
}

bool ParkConstructionRuntime::preview_placement(ConstructionWorld& c,
                                                std::uint16_t id,
                                                WorldCellCoordinates p,
                                                int rotation,
                                                int cost_override) {
    using namespace construction_detail;
    if (c.construction.coaster_catalog && c.construction.coaster_catalog->handles(c, id))
        return c.construction.coaster_catalog->preview(c, id, p, rotation);
    if (c.construction.selected_object_definition_id != id || c.construction.mode != 4)
        begin_placement(c, id, rotation);
    if (!prepare_placement(c, id, p, rotation, cost_override, false)) {
        state.validation_failed = state.status_latch = true;
        return false;
    }
    const auto& d = definition(c, id);
    bool allowed = true;
    if (d.balance->UsageInfo.SingleInstance) {
        const auto* control = find_world_object_control(world(c).object_controls, id);
        allowed = !control || !control->live_instance_count;
    }
    const auto cost = state.cost;
    bool valid = false;
    {
        RestoreValue suppression{ c.construction.cell_costs_suppressed };
        c.construction.cell_costs_suppressed = 1;
        valid = process_grid(c, p, rotation, cost_override, false, true).has_value() && !state.validation_failed;
        state.validation_failed = state.stop = state.status_latch = false;
        (void)process_grid(c, p, rotation, cost_override, false, false);
    }
    const bool funds = c.construction.action_recording_disabled || cost <= bank(c).balance;
    valid = valid && allowed && funds;
    state.cost = cost;
    state.insufficient_funds = !funds;
    state.validation_failed = !valid;
    state.status_latch = !valid;
    if (!valid)
        for (auto& face : state.faces)
            face.material = funds ? 1u : 15u;
    return valid;
}

bool
ParkConstructionRuntime::preview_impl(ConstructionWorld& c, WorldCellCoordinates p, WorldInputModifierMask modifiers) {
    using namespace construction_detail;
    //Native65E170 starts a fresh feedback pass without changing line anchors.
    state.faces.clear();
    state.preview_coordinates = p;
    state.cost = 0;
    state.validation_failed = state.stop = state.existing_endpoint = false;
    state.track_reuse = state.queue_joined = state.insufficient_funds = state.status_latch = false;
    state.candidate_overwrite_priority = state.occupant_overwrite_priority = 0;
    if (!bounds(p)) {
        state.validation_failed = state.status_latch = true;
        return false;
    }
    const auto mode = c.construction.mode;
    if (mode == 21 || mode == 22) {
        if (!c.construction.editor)
            throw std::logic_error("coaster editor owner is not bound");
        return c.construction.editor->hover(c, *this, p, modifiers);
    }
    if (!mode)
        return true;
    if (mode == 14 || mode == 19)
        return preview_context_track_edit(c, p);
    if (mode == 26)
        return preview_context_hoarding_edit(c, p);
    if (mode == std::to_underlying(WorldInteractionMode::backtrack_preview))
        return preview_backtrack(c, p);
    if (mode == std::to_underlying(WorldInteractionMode::demolish_rectangle)) {
        state.cost = -9999; //Native65F206 quote sentinel, not a refund.
        const WorldCellCoordinates start{ c.construction.path_start_x, c.construction.path_start_y };
        if (bounds(start))
            preview_rectangle(c, start, p);
        else
            (void)command_line(c, 314u, p, p);
        return true; //Native679D90 rectangle feasibility always succeeds.
    }
    if (mode == 59)
        return preview_object_relocation(c, p);
    if (mode == 4)
        return preview_placement(c, c.construction.selected_object_definition_id, p, state.rotation_degrees);
    if (mode == std::to_underlying(WorldInteractionMode::hoarding_rectangle)) {
        bind_land_boundary_anchors(c);
        p = snap_land_boundary_corner(c, p);
        state.preview_coordinates = p;
        const WorldCellCoordinates start{ c.construction.path_start_x, c.construction.path_start_y };
        if (!bounds(start)) {
            auto end = p;
            (void)track_command_line(c, 256u | 57u, p, end);
        } else {
            state.validation_failed = !validate_land_rectangle(c, start, p);
            const int left = std::min(start.x, p.x), right = std::max(start.x, p.x), top = std::min(start.y, p.y),
                      bottom = std::max(start.y, p.y), inset = left == right ? 0 : 2;
            auto end = WorldCellCoordinates{ right - inset, top };
            (void)track_command_line(c, 256u | 57u, { left + inset, top }, end);
            end = { right, bottom };
            (void)track_command_line(c, 256u | 57u, { right, top }, end);
            end = { right - inset, bottom };
            (void)track_command_line(c, 256u | 57u, { left + inset, bottom }, end);
            end = { left, top };
            (void)track_command_line(c, 256u | 57u, { left, bottom }, end);
        }
        return !state.validation_failed;
    }
    if (mode == 11 || mode == 16 || mode == 53) {
        const auto snap = [](int value, int origin) {
            auto delta = value - origin;
            if (delta < 0 && delta % 2)
                --delta;
            return origin + 2 * (delta / 2);
        };
        p = { snap(p.x, c.construction.interaction_cursor_anchor[0]),
              snap(p.y, c.construction.interaction_cursor_anchor[1]) };
        state.preview_coordinates = p;
        if (!bounds(p)) {
            state.validation_failed = state.status_latch = true;
            return false;
        }
    }
    const WorldCellCoordinates start{ c.construction.path_start_x, c.construction.path_start_y };
    const auto from = bounds(start) ? start : p;
    auto end = p;
    //Native preview first fixes Y on an X-major tie, then rechecks X against
    //the adjusted Y. This differs from the raw axis wrapper's Y-major tie.
    if (std::abs(end.x - from.x) >= std::abs(end.y - from.y))
        end.y = from.y;
    else
        end.x = from.x;
    state.preview_coordinates = end;
    if (mode == 1 || mode == 3 || mode == 20) {
        const auto request = mode == 1 ? 1u : 3u;
        state.candidate_overwrite_priority = request == 1 ? 2 : 3;
        (void)command_line(c, 256u | request, from, end);
    } else if (mode == 11 || mode == 16 || mode == 25) {
        //Native65E170 uses priority3 for kart/water-track cursor construction.
        state.candidate_overwrite_priority = 3;
        (void)track_command_line(c, 256u | static_cast<unsigned>(mode), from, end);
    } else if (mode == 51) {
        (void)command(c, map(c, cell_id(p)).base, 256u | 58u);
    } else if (mode == 52) {
        (void)command_line(c, 256u | 58u, from, end);
    } else if (mode == 53) {
        //Native15 selects erase material16 on roots belonging to this ride.
        (void)track_command_line(c, 256u | 15u, from, end);
    } else
        throw std::logic_error("unsupported construction preview mode");
    state.preview_coordinates = end;
    return !state.validation_failed && !state.status_latch;
}

void ParkConstructionRuntime::mouse_down(ConstructionWorld& c,
                                         WorldCellCoordinates p,
                                         WorldInputModifierMask modifiers,
                                         bool use_preview_coordinates) {
    using namespace construction_detail;
    if (c.construction.mode == 21 || c.construction.mode == 22) {
        if (!c.construction.editor)
            throw std::logic_error("coaster editor owner is not bound");
        c.construction.editor->mouse_down(c, *this, p, modifiers, use_preview_coordinates);
        return;
    }
    state.mouse_down_pending = true;
    if (c.construction.action_recording_disabled) {
        state.validation_failed = false;
        if (!c.construction.selected_track_type)
            (void)preview(c, p, modifiers);
    }
    if (use_preview_coordinates)
        p = state.preview_coordinates;
    if (!bounds(p)
        || state.status_latch
        || c.construction.action_recording_disabled
        || !c.construction.action_record_streams_initialized)
        return;
    if (!c.construction.action_record_stream)
        throw std::logic_error("construction recorder stream is not bound");
    const auto value = std::to_underlying(modifiers);
    const std::array bytes{ static_cast<std::byte>(value ? 40 : 15),
                            static_cast<std::byte>(p.x),
                            static_cast<std::byte>(p.y) };
    (void)c.construction.action_record_stream->write(bytes);
    if (value) {
        const auto word = save::detail::encode(static_cast<std::uint16_t>(value));
        (void)c.construction.action_record_stream->write(word);
    }
}
} //namespace simcoaster
