#pragma once
#include "world_domains_fwd.hpp"
#include "world_grid.hpp"
#include <optional>

namespace simcoaster {
//These are separate native domains: cell requests, preview materials, and
//command STOP/CONTINUE. In particular, nonzero preview results can be valid.
enum class CellInteractionRequest : std::int32_t {
    remove = 0,
    path = 1,
    queue = 3,
    object = 4,
    entry = 9,
    exit = 10,
    kart = 11,
    water = 16,
    queue_owner = 20,
    support = 21,
    move_support = 22,
    special_object = 23,
    path_unless_locked = 24,
    special_object_27 = 27,
    orange = 31,
    light_blue = 34,
    accept = 54,
    erase58 = 58,
    erase61 = 61,
    erase62 = 62,
    patrol_preview = 136,
    red_preview = 137
};
enum class CellInteractionResult : std::int32_t {
    accept = 0,
    rejected = 1,
    orange = 2,
    light_blue = 8,
    end_marker = 14,
    erase = 16
};
enum class CellCommand : std::uint32_t {
    remove = 0,
    path = 1,
    queue = 3,
    object = 4,
    support = 21,
    reconnect = 128,
    refresh = 129,
    direction_from_step = 130,
    assign_origin = 131,
    assign_pylon = 132,
    reset_track_flags = 133,
    update_ride_links = 134,
    remove_lower_priority = 135,
    preview = 256,
    validate = 512
};
enum class TrackInteractionResult : std::int32_t {
    accept = 0,
    rejected = 1,
    track_endpoint = 12,
    end_marker = 14,
    skip_coordinate_adjustment = 16
};
enum class TrackCellCommand : std::uint32_t {
    remove = 50,
    connect = 128,
    refresh = 129,
    direction_from_step = 130,
    assign_footprint = 131,
    reset_segment_flags = 133,
    update_ride_links = 134,
    remove_lower_priority = 135
};

struct ConstructionFace {
    WorldVector3 position {};
    std::uint32_t direction {}, material {};
    float width = 10, height = 10;
    std::int32_t geometry_selector {};
    bool skip_coordinate_adjustment {};
};

struct ParkPlacementResult {
    WorldCellCoordinates placement_origin { -1, -1 }, entrance { -1, -1 };
    std::uint8_t entrance_direction {};
    ThingId created_object {};
};

struct TrackInteractionPreviewRange {
    CellId start {}, end {};
    std::int16_t start_segment = -1, end_segment = -1;
};

struct ParkInteractionCostQuote {
    //Native quote encoding is not the bank's accounting domain. Positive
    //values are dollars; small negative values are Golden Tickets; negative
    //cash refunds are biased by their actual producers. Preserve sentinels.
    std::int32_t value {}, mode {};
    std::uint16_t definition {};
    WorldCellCoordinates cursor { -1, -1 };
    std::optional<std::uint32_t> coaster_map;
    std::optional<std::size_t> coaster_node;
};

//Native510770 retains only an obsolete record pointer and a queue-point
//snapshot. Own the actual scalar selection metadata across event33 and ID
//reuse; neither old payload resources nor object attributes are retained.
struct ParkObjectRelocationState {
    ThingId source {}, related {};
    std::uint16_t definition {};
    std::uint64_t source_creation_time {}, related_creation_time {};
    const ThingHeader* related_identity {};
    std::int32_t angle {};
    bool cursor_event_seen {};
    std::uint8_t queue_exit_direction {};
    std::vector<WorldCellCoordinates> queue_points;
};

struct ParkConstructionState {
    std::u16string cursor_help;
    std::int32_t candidate_overwrite_priority {}, occupant_overwrite_priority {}, cost {};
    bool validation_failed {}, stop {}, existing_endpoint {}, queue_joined {}, queue_join_override {};
    bool insufficient_funds {}, status_latch {}, reuse_line_start {}, skip_nonpath_preview {};
    bool placement_from_origin {}, queue_placement_enabled = true, mouse_down_pending {};
    bool frame_rate_below_clip {}, show_red_faces = true;
    bool coordinate_adjustment_disabled {}, connection_placement_active {};
    bool track_reuse {}, track_first {}, track_endpoint {}, track_endpoint_stop {};
    std::int32_t previous_mode {}, rotation_degrees {}, object_rotation_degrees = -1;
    //Native B1A514 is distinct from the current rotation:673070 compares it
    //before recording, then replaces it with the old current angle.
    std::int32_t previous_rotation_degrees {};
    //Cancel replaces native kind3 with a fresh Path/Delete handler, whose
    //requested mode activates only on the next genuine World pointer input.
    std::optional<std::int32_t> shortcut_rearm_mode;
    std::int32_t backtrack_saved_mode {};
    TrackInteractionPreviewRange track_preview;
    CellId previous_origin {}, placement_cell {};
    CellId last_pointer_cell {};
    std::uint16_t next_pylon {};
    WorldCellCoordinates origin { -1, -1 }, preview_coordinates { -1, -1 };
    WorldCellCoordinates grid_origin { -1, -1 }, placement_origin { -1, -1 }, hoarding_origin { -1, -1 };
    MapCell* entry_neighbor {};
    TrackCell *track {}, *secondary_track {};
    std::uint8_t direction {};
    ParkPlacementResult result;
    std::optional<ParkObjectRelocationState> relocation;
    std::vector<ConstructionFace> faces;
    std::vector<WorldCellCoordinates> path_points;
    //Native6666B0 seeds the renderer's eased feedback reference from the
    //unfinished track endpoint, independently of the hovered preview cell.
    std::optional<WorldCellCoordinates> preview_reference_reset;
    //Published by a current preview pass, consumed by the HUD once per frame.
    //Absence is distinct from a real zero-dollar quote and hidden sentinels.
    std::optional<ParkInteractionCostQuote> pending_interaction_quote;
};

//Process-only endpoint state from native677CF0/5D7A70. Binding does not alter
//land, cells, or saved objects; it is also needed before type25 mesh restore.
void bind_land_boundary_anchors(ConstructionWorld&);
//Modern drawable ownership for native group5 cells. The original updater
//invalidates these slots; the host renders them through its existing native
//hoarding template factory, including after save restoration.
void sync_land_boundary_meshes(ConstructionWorld&);
enum class LandBoundarySource {
    installed_park,
    fresh_editor
};

//Owns only transient interaction state. World records, catalog definitions,
//meshes, scripts and money stay with WorldRuntime and its production Services.
//NativeServices may borrow this owner; no global pointer registry is used.
class ParkConstructionRuntime {
    friend void initialize_source_hoarding_interaction(WorldRuntime&, ParkConstructionRuntime&);

public:
    ParkConstructionState state;
    void reset_after_restore();
    void refresh_origin(ConstructionWorld&, bool record_action = false);
    void set_mode(ConstructionWorld&, std::int32_t mode, bool record_action = false);
    void set_mode_preserving_path(ConstructionWorld&, std::int32_t mode, bool record_action = false);
    //UI context initialization is not a fabricated mouse click. Valid track
    //keeps14/19 endpoint selection; unfinished track enters live11/16.
    void begin_context_queue_edit(ConstructionWorld&, ParkObjectRuntimeView&);
    bool begin_object_relocation(ConstructionWorld&, ParkObjectRuntimeView&);
    bool preview_object_relocation(ConstructionWorld&, WorldCellCoordinates);
    void dispatch_object_relocation(ConstructionWorld&, WorldCellCoordinates, WorldInputModifierMask);
    void bind_relocation_parent(ConstructionWorld&);
    void resume_object_connection_placement(ConstructionWorld&, WorldCellCoordinates);
    void retire_relocation_reference(ThingId);
    void finish_object_relocation(ConstructionWorld&, WorldInputModifierMask = WorldInputModifierMask::none);
    void begin_context_track_edit(ConstructionWorld&, ParkObjectRuntimeView&, CellId fresh_cursor);
    //Complete a recorded/live20/14/19 entry after the selected owner is bound.
    //Does not record input or move the camera.
    void initialize_context_mode(ConstructionWorld&);
    CellInteractionResult evaluate(ConstructionWorld&, const CellBase&, CellInteractionRequest, bool bypass_failure = false);
    bool command(ConstructionWorld&, CellBase&, std::uint32_t command, std::int32_t x = 0, std::int32_t y = 0);
    void begin_placement(ConstructionWorld&, std::uint16_t definition_id, std::int32_t rotation = 0);
    std::optional<ParkPlacementResult> process_grid(ConstructionWorld&, WorldCellCoordinates, std::int32_t rotation, std::int32_t cost_override, bool commit, bool validate, DefinitionGrid* override_grid = nullptr);
    std::optional<ParkPlacementResult> place(ConstructionWorld&, std::uint16_t definition_id, WorldCellCoordinates, std::int32_t rotation = 0, std::int32_t cost_override = 0);
    bool preview(ConstructionWorld&, WorldCellCoordinates, WorldInputModifierMask = WorldInputModifierMask::none);
    void update_cursor_feedback(ConstructionWorld&, WorldCellCoordinates);
    //Live World Map Action handler adapter (4D32F0/4D2E40/4D2E90).
    //Temporary139 belongs to the saved tool; raw replay dispatch stays separate.
    bool pointer_hover(ConstructionWorld&, WorldCellCoordinates, WorldInputModifierMask);
    void pointer_down(ConstructionWorld&, WorldCellCoordinates, WorldInputModifierMask, bool use_preview_coordinates = true);
    std::uint8_t pointer_up(ConstructionWorld&, WorldCellCoordinates, WorldInputModifierMask, bool use_preview_coordinates = true);
    void invalidate_interaction_cost_quote() noexcept;
    //674EF0's native keyboard path: preserve the seed, pop one endpoint and
    //refresh owner links through the same implementation as pointer backtrack.
    bool backtrack_current_path(ConstructionWorld&);
    bool resume_shortcut_tool(ConstructionWorld&, std::optional<WorldCellCoordinates> pointer_cell = {});
    std::optional<std::int32_t> take_interaction_cost_quote(const ConstructionQuery&);
    void mouse_down(ConstructionWorld&, WorldCellCoordinates, WorldInputModifierMask, bool use_preview_coordinates = false);
    bool preview_placement(ConstructionWorld&, std::uint16_t definition_id, WorldCellCoordinates, std::int32_t rotation = 0, std::int32_t cost_override = 0);
    bool demolish(ConstructionWorld&, WorldCellCoordinates, bool from_track_update = false);
    std::uint8_t dispatch(ConstructionWorld&, WorldCellCoordinates, WorldInputModifierMask, bool use_preview_coordinates = false);
    bool command_line(ConstructionWorld&, std::uint32_t, WorldCellCoordinates start, WorldCellCoordinates end);
    bool refresh_track_path(ConstructionWorld&, bool preserve_runtime = false);
    TrackInteractionResult evaluate_track(ConstructionWorld&, const CellBase&, std::int32_t operation, bool followup = false, std::uint8_t direction = 0);
    bool track_command(ConstructionWorld&, CellBase&, std::uint32_t, std::int32_t x_step = 0, std::int32_t y_step = 0);
    bool track_command_line(ConstructionWorld&, std::uint32_t, WorldCellCoordinates start, WorldCellCoordinates& end);
    //Import the installed park's Hoardings.sam, or the separate native CEM
    //startup outline for a fresh editor. Both use the recovered click-list and
    //ownership kernels. Never substitute the small CEM outline for an existing
    //campaign park. Rectangle expansion validates boundary/raster connectivity.
    void initialize_land_editor_boundary(ConstructionWorld&, LandBoundarySource = LandBoundarySource::installed_park);
    bool validate_land_rectangle(ConstructionWorld&, WorldCellCoordinates first, WorldCellCoordinates second);
    bool expand_land_rectangle(ConstructionWorld&, WorldCellCoordinates first, WorldCellCoordinates second);

private:
    friend class CoasterCatalogRuntime;
    bool backtrack_path_step(ConstructionWorld&, ParkObjectRuntimeView*, bool refresh_links);
    bool preview_impl(ConstructionWorld&, WorldCellCoordinates, WorldInputModifierMask);
    bool demolish_rectangle(ConstructionWorld&, WorldCellCoordinates, WorldCellCoordinates);
    bool preview_backtrack(ConstructionWorld&, WorldCellCoordinates);
    bool preview_track_chain(ConstructionWorld&, TrackCell&, TrackCell&);
    bool preview_context_track_edit(ConstructionWorld&, WorldCellCoordinates);
    void dispatch_context_track_edit(ConstructionWorld&, WorldCellCoordinates);
    bool preview_context_hoarding_edit(ConstructionWorld&, WorldCellCoordinates);
    void dispatch_context_hoarding_edit(ConstructionWorld&, WorldCellCoordinates);
    void trace_hoarding_edit_path(ConstructionWorld&);
    std::int32_t process_context_track_chain(ConstructionWorld&, TrackCell&, TrackCell&, bool reconcile_endpoints);
    void preview_rectangle(ConstructionWorld&, WorldCellCoordinates, WorldCellCoordinates);
    void validate_placement_parent(ConstructionWorld&, const ResearchCatalogDefinition&);
    bool prepare_placement(ConstructionWorld&, std::uint16_t definition_id, WorldCellCoordinates&, std::int32_t& rotation, std::int32_t cost_override, bool select_definition = true);
    bool preview_track_cell(ConstructionWorld&, CellBase&, std::uint32_t operation, std::int32_t x_step, std::int32_t y_step);
    void finish_placement(ConstructionWorld&, const ParkPlacementResult&, bool alternate = false);
    void finish_queue_placement(ConstructionWorld&, const ParkPlacementResult&, bool alternate = false);
    ParkObjectRuntimeView* create_at_placement(ConstructionWorld&, std::int32_t rotation, std::int32_t cost_override);
    bool special_placement_block(ConstructionWorld&, WorldCellCoordinates, std::int32_t rotation, bool commit, bool validate, std::uint8_t direction);
    void refresh_connected_paths(ConstructionWorld&, MapCell&, bool last_option = false);
    void remove_lower_priority(ConstructionWorld&, CellBase&);
    void refresh_track_links(ConstructionWorld&, CellBase&);
    bool reconcile_track_links(ConstructionWorld&, ParkObjectRuntimeView&, bool opposite, bool activate, bool animate, bool valid);
    std::uint8_t trace_queue(ConstructionWorld&, ParkObjectRuntimeView&);
    void trace_track(ConstructionWorld&, ParkObjectRuntimeView&, bool opposite);
    void remove_traced_path(ConstructionWorld&, bool track);
    void refresh_hoarding_cell(ConstructionWorld&, CellBase&);
    void trace_land_boundary(ConstructionWorld&, bool update_direction);
    void apply_land_boundary_ownership(ConstructionWorld&);
};
} //namespace simcoaster
