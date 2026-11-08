#pragma once
#include "park_construction.hpp"
#include <filesystem>

namespace simcoaster {
class ParkRuntime;
enum class CoasterEditorEntryTool : std::uint32_t;
enum class ParkAuthoringTool {
    inspect,
    object,
    path,
    queue,
    track,
    demolish,
    erase_path,
    erase_track,
    hoarding_chain,
    land_boundary,
    demolish_rectangle
};
enum class ParkCatalogAvailability {
    available,
    research_locked,
    single_instance,
    requires_selection,
    incompatible_selection,
    coaster_editor
};
enum class ParkLevelUpgradeAvailability {
    available,
    no_selection,
    not_upgradeable,
    maximum_level,
    research_locked,
    needs_mechanic,
    insufficient_funds,
    pending
};

struct ParkLevelUpgrade {
    ParkLevelUpgradeAvailability availability = ParkLevelUpgradeAvailability::no_selection;
    std::uint32_t level {};
    std::int32_t cost {};
};

struct ParkAuthoringCatalogEntry {
    std::uint16_t definition {};
    ObjectUiCategory category {};
    std::u16string name;
    std::int32_t cost {};
    ParkCatalogAvailability availability {};
    bool attachment {};
};

struct ParkAuthoringFeedback {
    std::u16string message, rejection;
    std::optional<WorldCellCoordinates> cursor;
    std::int32_t cost {}, balance {}, rotation {};
    bool accepted {}, insufficient_funds {};
};

//Frontend policy and gesture state. All persistent edits go through the
//production construction/object APIs; this class owns no World records.
class ParkAuthoringController {
    ParkRuntime& park_;
    ParkAuthoringTool tool_ = ParkAuthoringTool::inspect;
    ThingId selected_ {};
    std::uint16_t selected_definition_ {}, placement_definition_ {};
    std::uint64_t selected_creation_time_ {};
    bool mouse_down_ {};
    bool track_changed_ {}, reopen_after_cancel_ {};
    std::uint64_t interaction_revision_ {};
    ParkAuthoringFeedback feedback_;
    ParkObjectRuntimeView* selected_object();
    void remember_selection(ParkObjectRuntimeView&);
    void bind_selected(bool record = false);
    void reset_feedback();
    void synchronize_tool();
    void finish_interaction(WorldInputModifierMask, bool record_action);

public:
    explicit ParkAuthoringController(ParkRuntime&);
    bool replaying() const;

    ParkAuthoringTool tool() const {
        return tool_;
    }

    std::uint64_t interaction_revision() const {
        return interaction_revision_;
    }

    const ParkAuthoringFeedback& feedback() const {
        return feedback_;
    }

    ThingId selected();
    std::u16string selected_name();
    std::vector<ParkAuthoringCatalogEntry> catalog();
    ParkLevelUpgrade level_upgrade();
    bool request_level_upgrade();
    bool cancel_level_upgrade();
    bool select(WorldCellCoordinates);
    bool choose_object(std::uint16_t);
    //Native Build13000 policy: catalog gates, current purchase quote and
    //view-aligned initial rotation. WorldRuntime Buy retains its separate policy.
    bool choose_build_object(std::uint16_t);
    //Original object-context actions resolve the live receiver before any
    //tool transition. Deletion joins grid removal/refund; Buy keeps its angle.
    bool delete_object(ThingId);
    bool buy_object(ThingId);
    //Direct506280 clicks install an input mode without the context menu's
    //camera shift. Hover has already checked the cell-specific entry contract.
    void begin_cursor_map_action(std::int32_t mode, ThingId owner = 0);
    //Original panels can change the engine construction state directly.
    //Adopt those transitions without canceling or executing them again.
    void synchronize_from_world();
    void retire(ThingId);
    bool edit_object_queue(ThingId);
    bool edit_object_track(ThingId);
    bool edit_coaster_tool(ThingId, CoasterEditorEntryTool);
    void ride_object(ThingId);
    bool relocate_object(ThingId);
    bool choose_design(const std::filesystem::path&);
    bool choose_build_design(const std::filesystem::path&, std::int32_t quote);
    bool choose_tool(ParkAuthoringTool);
    bool select_toolbar_tool(std::uint32_t id, bool selected);
    void rotate(int quarter_turns = 1);
    void hover(std::optional<WorldCellCoordinates>, WorldInputModifierMask = WorldInputModifierMask::none);
    void mouse_down(WorldCellCoordinates, bool keep_tool = false);
    void mouse_down(WorldCellCoordinates, WorldInputModifierMask);
    void cancel_pointer_input(WorldInputModifierMask = WorldInputModifierMask::none);
    bool click(WorldCellCoordinates, bool keep_tool = false);
    bool click(WorldCellCoordinates, WorldInputModifierMask);
    bool activate_selected();
    bool set_park_open(bool);
    bool open_selected();
    bool close_selected();
    bool select_coaster_node(WorldCellCoordinates);
    bool stage_coaster_node(bool special = false, bool select_pylon = false);
    bool move_coaster_node();
    bool finalize_coaster();
    void cancel(WorldInputModifierMask = WorldInputModifierMask::none);
    //Native42F710/5070A0 finish the old handler, then673230 reinstalls the
    //selected toolbar tool. UI takeover501490 instead selects Pointer.
    void cancel_interaction(WorldInputModifierMask = WorldInputModifierMask::none);
    void reset_for_ui();
    void save(const std::filesystem::path&, WorldInputModifierMask = WorldInputModifierMask::none);
};
} //namespace simcoaster
