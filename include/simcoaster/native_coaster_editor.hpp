#pragma once
#include "world_domains_fwd.hpp"
#include "layout_world_runtime.hpp"
#include "native_coaster_preview.hpp"
#include <memory>

namespace simcoaster {
//52F870 request ordinals,48AB40 selection bits and48B520 availability bits
//are distinct native domains. In particular Build Supports is ordinal2/bit1.
enum class CoasterEditorEntryTool : std::uint32_t {
    delete_track_section = 1,
    build_supports = 2,
    build_complex_loop = 3,
    build_loop_the_loop = 4,
    pivot_track = 5,
    stack_track_up = 6,
    stack_track_down = 7,
    adjust_support_height = 8,
    rotate_supports = 9,
    move_supports = 10
};
enum class CoasterEditorTool : std::uint32_t {
    build_supports = 1,
    build_complex_loop = 2,
    move_supports = 4,
    pivot_track = 8,
    adjust_support_height = 16,
    rotate_supports = 32,
    delete_track_section = 64,
    stack_track_up = 128,
    stack_track_down = 256,
    build_loop_the_loop = 1024,
    delete_single_support = 2048,
    insert_support = 4096
};
enum class CoasterGeometryPointerPhase {
    hover,
    press,
    release,
    cancel
};
enum class CoasterEditorInputRoute {
    world_default,
    map_action,
    geometry
};

//Shared live/replay editor owner. Map IDs and geometry ordinals remain values;
//no pointer into a vector survives an insertion, removal, or World restore.
//Native MOVE60 begins a detached move preview. Its XY WORDs carry no meaning.
class NativeCoasterEditor final : public LayoutCoasterActionBackend {
    struct State;
    std::unique_ptr<State> state_;
    void refresh_placement_tool(AttractionWorld&);

public:
    explicit NativeCoasterEditor(ParkConstructionRuntime&);
    ~NativeCoasterEditor() override;
    NativeCoasterEditor(const NativeCoasterEditor&) = delete;
    NativeCoasterEditor& operator=(const NativeCoasterEditor&) = delete;
    std::optional<std::uint32_t> active_map() const;
    std::optional<std::size_t> active_node() const;
    const NativeCoasterPreview* preview() const;
    //Revalidate render artifacts after direct parameter edits or a World frame.
    //This does not clear/replace mouse-down feedback or execute an input phase.
    void refresh_preview(AttractionWorld&);
    void activate(AttractionWorld&, std::uint32_t map_id, bool record_action = true);
    //Raw46EF00 catalog binding. These deliberately omit ACT68/admission/detail
    //transitions; catalog callers own their complete construction transaction.
    void bind_catalog_context(AttractionWorld&, std::uint32_t map_id);
    void clear_catalog_context(AttractionWorld&);
    //475220 completion leaves input-mode/UI policy to its caller.
    void complete_supports(AttractionWorld&);
    void retire_context(AttractionWorld&, std::uint32_t map_id);
    void activate_object(AttractionWorld&, ThingId, bool record_action = true);
    void enter_tool(AttractionWorld&, ThingId, CoasterEditorEntryTool, bool record_action = true);
    void delete_context_geometry(AttractionWorld&, std::uint32_t mesh_slot);
    void select_tool(AttractionWorld&, CoasterEditorTool, bool active = true, bool record_action = true);
    std::uint32_t tool_selection() const;
    std::uint32_t tool_availability(AttractionWorld&) const;
    std::optional<CoasterEditorEntryTool> entry_tool() const;
    std::optional<ThingId> active_object(AttractionWorld&) const;
    bool geometry_interaction_active() const;
    CoasterEditorInputRoute input_route() const;
    std::optional<std::int32_t> geometry_interaction_cost() const;
    //Mesh slots come from the actual world ray hit; pixels are physical client
    //coordinates, as in488230. No terrain-cell stand-in for a selected pylon.
    bool geometry_pointer(AttractionWorld&,
                          std::uint32_t mesh_slot,
                          std::array<std::int32_t, 2> pixel,
                          CoasterGeometryPointerPhase);
    bool coaster_key(AttractionWorld&, std::uint32_t packed_key, std::uint32_t event_type);
    void deactivate_ui_context(AttractionWorld&);
    bool consume_followup() noexcept;
    bool select_at_tile(AttractionWorld&, WorldCellCoordinates, bool record_action = true);
    void begin_move(AttractionWorld&, std::size_t ordinal, bool record_action = true);
    void stage(AttractionWorld&, bool special, bool select_pylon = false, bool record_action = true);
    void delete_range(AttractionWorld&, std::uint16_t first, std::uint16_t second, bool record_action = true);
    //Shared complete insertion operation; live selection records its midpoint.
    void insert_support(AttractionWorld&, const CoasterInsertReplayCommand&, bool record_action = false);
    bool hover(AttractionWorld&,
               ParkConstructionRuntime&,
               WorldCellCoordinates,
               WorldInputModifierMask = WorldInputModifierMask::none);
    void mouse_down(AttractionWorld&,
                    ParkConstructionRuntime&,
                    WorldCellCoordinates,
                    WorldInputModifierMask,
                    bool use_preview_coordinates = false);
    std::uint8_t mouse_up(AttractionWorld&,
                          ParkConstructionRuntime&,
                          WorldCellCoordinates,
                          WorldInputModifierMask,
                          bool use_preview_coordinates = false);
    //cancel restores the detached source cell/selection and leaves the active
    //context. finalize generates the native closing support and vehicle state.
    void cancel(AttractionWorld&, ParkConstructionRuntime&) override;
    void finalize(AttractionWorld&, ParkConstructionRuntime&);
    void reset_after_restore();
    void dispatch(AttractionWorld&, ParkConstructionRuntime&, const LayoutAction&) override;
};
} //namespace simcoaster
