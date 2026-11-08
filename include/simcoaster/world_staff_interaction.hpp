#pragma once
#include "world_domains_fwd.hpp"
#include "world_types.hpp"
#include <functional>
#include <variant>

namespace simcoaster {
enum class WorldGridObjectCategory : std::int32_t {
    unsupported = 0,
    rides = 1,
    shops = 2,
    sideshows = 3,
    features = 4,
    facilities = 5
};
enum class WorldInteractionHandlerKind : std::int32_t {
    default_ = 1,
    hired_staff = 5,
    held_staff = 6,
    alternate = 7,
    ride_track_pick = 9,
    staff_area = 10
};
enum class StaffAreaResizeHandle : std::int32_t {
    none = 0,
    left_bottom = 1,
    right_top = 2,
    left_top = 3,
    right_bottom = 4,
    left = 5,
    right = 6,
    top = 7,
    bottom = 8,
    interior = 9
};
enum class WorldPointerButton {
    left,
    right
};

struct StaffInteractionCursor {
    CellId cell{};
    std::array<float, 3> ray_point{}, ground_point{};
};

struct StaffInteractionOverlay {
    CellId cell{};
    std::uint8_t material{};
};

struct DefaultWorldInteraction {
    bool alternate{};
};

struct HiredStaffInteraction {
    bool placement_confirmed{};
};

struct HeldStaffInteraction {
    std::uint32_t resource{}, category{};
    std::uint8_t variant{};
};

struct RideTrackPickInteraction {
    //Copied presentation only. A click resolves its current cell afresh, so a
    //retired/reused ThingId never becomes an inherited camera target.
    ThingId hovered_object{};
    std::uint32_t help_row{};
    bool hover_eligible{};
    std::u16string display_name, help_text;
    std::function<void()> reset_ui;
};

struct StaffAreaSelectionInteraction {
    ThingId target{};
    std::uint32_t render_mode = 54;
    StaffAreaMembership preview{};
    std::vector<ThingId> preview_staff;
    CellId drag_origin{};
    StaffAreaResizeHandle resize_handle{};
    StaffAreaBounds selection{}, drag_anchor{};
    bool selection_active{}, panel_open{}, confirm_enabled{};
    std::uint32_t caption_row = 107;
};

//Native handler subclasses are represented by typed alternatives. This is
//owned x64 runtime state, independent of the original vptr/heap layouts.
struct WorldInteractionHandler {
    using Value = std::variant<DefaultWorldInteraction,
                               HiredStaffInteraction,
                               HeldStaffInteraction,
                               StaffAreaSelectionInteraction,
                               RideTrackPickInteraction>;
    Value value;
    bool completed{};
};

struct StaffTrainingAbilityRow {
    ThemeAbility ability{};
    std::u16string name;
};
enum class StaffTrainingSelectionResult {
    started,
    already_known,
    confirmation_required
};

struct StaffTrainingPicker {
    ThingId staff{};
    ThemeAbility selected{};
    bool open{}, confirmation_pending{};
    std::uint32_t message_row{};
    std::vector<StaffTrainingAbilityRow> rows;
};

struct StaffTrainingPendingRow {
    ThingId staff{};
    ThemeAbility ability{};
};

struct WorldStaffInteractionState {
    WorldInteractionHandler handler;
    //Native A434DC is the pointer-handler mode. It is independent of B20024,
    //the grid interaction/render mode stored in WorldRuntime.world_interaction_mode.
    std::int32_t pointer_mode_code{};
    bool initialized{}, left_button_held{}, right_button_held{}, suppress_default_cursor_refresh{};
    std::array<std::int32_t, 2> last_cursor{ -1, -1 };
    std::uint32_t cursor_presentation{};
    StaffInteractionCursor cursor;
    std::vector<StaffInteractionOverlay> overlays;
    StaffTrainingPicker training_picker;
    std::array<StaffTrainingPendingRow, 3> training_pending{};
};

WorldGridObjectCategory get_first_world_grid_object_ui_category(StaffWorld&, const CellBase&, ParkObjectRuntimeView*&);
CellId
select_world_map_cell_for_grid_object_interaction(StaffWorld&, WorldGridObjectCategory, CellId, ParkObjectRuntimeView*);
bool is_staff_placement_cell_admissible(StaffWorld&, CellId);
WorldInteractionHandlerKind world_staff_interaction_kind(const StaffQuery&);
void initialize_world_staff_interaction(StaffWorld&);
void cancel_world_staff_interaction(StaffWorld&);
void release_world_staff_interaction(StaffWorld&);
void activate_hired_staff_placement_interaction(StaffWorld&);
void start_staff_pickup_interaction(StaffWorld&, StaffRuntimeRecord&);
void drop_held_staff_member_at_cell(StaffWorld&, StaffHqRecord&, CellId);
ThingId place_staff_at_cursor(StaffWorld&);
void update_world_staff_interaction_cursor(StaffWorld&, StaffInteractionCursor);
void world_staff_interaction_button(StaffWorld&, WorldPointerButton, bool down);
void begin_staff_area_selection(StaffWorld&, StaffRuntimeRecord&, std::uint32_t render_mode = 54);
StaffAreaResizeHandle
classify_staff_area_selection_resize_handle(const StaffAreaSelectionInteraction&, std::int32_t x, std::int32_t y);
std::uint32_t staff_area_resize_cursor(bool active, StaffAreaResizeHandle, float heading);
void update_staff_area_selection_from_cursor(StaffWorld&, StaffAreaSelectionInteraction&, CellId, bool dragging);
bool update_staff_area_selection_validity(StaffWorld&, StaffAreaSelectionInteraction&);
void commit_staff_area_selection_to_target(StaffWorld&, StaffAreaSelectionInteraction&);
void render_staff_area_selection_overlay(StaffWorld&, StaffAreaSelectionInteraction&);
void close_staff_area_selection_panel(StaffWorld&, bool confirm);
void open_staff_training_picker(StaffWorld&, StaffRuntimeRecord&);
StaffTrainingSelectionResult select_staff_training_ability(StaffWorld&, ThemeAbility);
bool confirm_staff_training_ability_change(StaffWorld&);
void close_staff_training_picker(StaffWorld&);
} //namespace simcoaster
