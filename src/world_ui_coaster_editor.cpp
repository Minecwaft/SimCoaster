#include "simcoaster/require_bound.hpp"
#include "simcoaster/entity_identity.hpp"
#include "simcoaster/world_ui_coaster_editor.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_event.hpp"
#include "simcoaster/park_object_context_geometry.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/mesh_runtime_queries.hpp"
#include <format>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace simcoaster {
namespace {
struct ToolControl {
    std::uint32_t id;
    CoasterEditorTool tool;
    std::uint32_t availability, help;
    std::int32_t advisor, activation_cursor = -1, selection_cursor = -1;
};

//52EE90 help bindings;52E4E0 callbacks;52E1A0 availability. The two loop
//controls share availability bit2, but retain different selection bits.
constexpr std::array tools { ToolControl { 106, CoasterEditorTool::build_supports, 1, 121, 237 },
                             ToolControl { 99, CoasterEditorTool::build_complex_loop, 2, 122, 243 },
                             ToolControl { 105, CoasterEditorTool::move_supports, 4, 126, 238 },
                             ToolControl { 108, CoasterEditorTool::build_loop_the_loop, 2, 337, 497 },
                             ToolControl { 100, CoasterEditorTool::adjust_support_height, 16, 123, 267, 12, 12 },
                             ToolControl { 103, CoasterEditorTool::pivot_track, 8, 125, 269, 14, 14 },
                             ToolControl { 104, CoasterEditorTool::rotate_supports, 32, 124, 268, 13, 13 },
                             //Native callback and programmatic-selection cursor IDs are reversed for
                             //these two controls. Preserve each distinct call path.
                             ToolControl { 101, CoasterEditorTool::stack_track_up, 128, 127, 270, 16, 15 },
                             ToolControl { 102, CoasterEditorTool::stack_track_down, 256, 128, 271, 15, 16 },
                             ToolControl { 107, CoasterEditorTool::insert_support, 4096, 340, 496 },
                             ToolControl { 109, CoasterEditorTool::delete_single_support, 2048, 338, 498, 10, 10 },
                             ToolControl { 66, CoasterEditorTool::delete_track_section, 64, 339, 241, 10, 10 } };
//52E3A0 tests selection bits in this priority, independently of toolbar order.
constexpr std::array<std::uint32_t, 12> selection_order { 105, 106, 99, 108, 107, 109, 103, 100, 104, 66, 101, 102 };
} //namespace

struct WorldCoasterEditorUiController::State {
    WorldRuntime& c;
    UiRuntime& ui;
    UiControl *primary {}, *control {}, *group {};
    WorldUiPanelHost host;
    NativeCoasterEditor& editor;
    UiCallback previous_callback;
    ThingId object {};
    const ThingHeader* identity {};
    std::uint64_t creation_time {};
    bool active {}, closing {}, entry_blocked {};
    std::int32_t saved_cursor = -1;
    std::optional<std::uint32_t> previous_selection;

    static NativeCoasterEditor& backend(const WorldUiPanelHost& owner) {
        return require_bound<std::invalid_argument>(owner.native_coaster_editor, "Coaster UI requires its native editor owner");
    }

    State(WorldRuntime& context, UiRuntime& runtime, UiControl* parent, WorldUiPanelHost callbacks) : c(context), ui(runtime), primary(parent), host(std::move(callbacks)), editor(backend(host)) {
        if (!ui.owns(primary)) {
            throw std::invalid_argument("Coaster UI requires its persistent primary root");
        }
        if (!host.set_editor_toolbar_active || !host.synchronize_focus || !host.camera_key) {
            throw std::invalid_argument("Coaster UI requires toolbar, focus and camera-key owners");
        }
        control = ui.find(primary, 97);
        if (!control || !(group = ui.find(control, 98))) {
            throw std::runtime_error("Original primary template has no coaster controls97/98");
        }
        auto* toolbar = ui.find(primary, 62);
        if (!toolbar) {
            throw std::runtime_error("Original primary template has no toolbar62");
        }
        //52EE90 aligns only X against toolbar62, retaining editor97's own Y.
        ui.translate(*control, toolbar->rect.left - control->rect.left, 0);
        ui.set_visible(*control, false);
        for (const auto& tool : tools) {
            auto* button = ui.find(group, tool.id);
            if (!button) {
                throw std::runtime_error(std::format("Original coaster toolbar lacks control {}", tool.id));
            }
            button->help_index = button->template_metadata = tool.help;
        }
        if (!ui.find(control, 110)) {
            throw std::runtime_error("Original coaster toolbar lacks close control110");
        }
        previous_callback = control->primary_callback;
        control->primary_callback = [this](UiControl& sender, const UiEvent& event) {
            return on_event(sender, event);
        };
    }

    ~State() {
        close();
        if (ui.owns(control)) {
            control->primary_callback = std::move(previous_callback);
        }
    }

    ParkObjectRuntimeView* live() const {
        return EntityIdentity { object, identity, creation_time }.get<ParkObjectRuntimeView, true>(c.entities);
    }

    void cancel_advisor(std::int32_t action) {
        const auto* world = c.referenced_world();
        if (!world) {
            return;
        }
        auto* header = c.entities.at(world->saved.advisor).payload;
        if (!header || header->kind != ThingKind::advisor) {
            throw std::logic_error("Coaster UI has no live Advisor");
        }
        auto& advisor = *reinterpret_cast<AdvisorRecord*>(header);
        //Native503B40 clears only the delayed-panel flag before cancellation.
        advisor.from_ui_panel = 0;
        world_operations::cancel_advisor_action_by_id(c, &advisor, action);
    }

    const ToolControl* selected(std::uint32_t mask) const {
        for (auto id : selection_order) {
            const auto found = std::ranges::find(tools, id, &ToolControl::id);
            if (mask & std::to_underlying(found->tool)) {
                return &*found;
            }
        }
        return nullptr;
    }

    void project(bool update_cursor = true) {
        if (!active || !ui.owns(control) || !ui.owns(group)) {
            return;
        }
        const auto availability = editor.tool_availability(c);
        for (const auto& tool : tools) {
            ui.set_enabled(*ui.find(group, tool.id), (availability & tool.availability) != 0);
        }
        //52F440 always reinstalls cb_loop and disables both loops for0200h.
        auto& loop = *ui.find(group, 99);
        loop.asset_hash = ui_model_asset_hash("cb_loop");
        loop.help_index = loop.template_metadata = 122;
        if ((availability & 0x200u) || !(availability & 2u)) {
            ui.set_enabled(loop, false);
            ui.set_enabled(*ui.find(group, 108), false);
        }
        const auto mask = editor.tool_selection();
        const auto* tool = selected(mask);
        ui.set_selected(*group, tool ? static_cast<std::int32_t>(tool->id) : -1, false);
        if (update_cursor && previous_selection != mask && tool && tool->selection_cursor >= 0) {
            primary->cursor_id = tool->selection_cursor;
        }
        previous_selection = mask;
    }

    bool open(ThingId id, CoasterEditorEntryTool entry, bool present = true) {
        auto* header = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
        if (!header || header->retirement_pending || header->kind != ThingKind::park_object) {
            return false;
        }
        if (!ui.owns(control) || !ui.owns(group)) {
            throw std::logic_error("Retained coaster controls were destroyed");
        }
        if (present && host.dismiss) {
            host.dismiss();
        }
        if (!active) {
            saved_cursor = primary->cursor_id;
        }
        //52F870 marks this before any entry/visibility/tool work.
        auto& record = *reinterpret_cast<ParkObjectRuntimeView*>(header);
        record.prebuilt_track_edited = 1;
        editor.enter_tool(c, id, entry);
        object = id;
        identity = header;
        creation_time = record.creation_file_time;
        active = c.construction.coaster_editor_active = true;
        previous_selection.reset();
        if (present && !c.construction.action_recording_disabled) {
            ui.set_visible(*control, true);
        }
        if (present) {
            host.set_editor_toolbar_active(true);
            ui.set_focus(control);
            project();
        }
        return true;
    }

    bool open_world(ThingId id, bool placement_entry) {
        auto* header = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
        if (!header || header->retirement_pending || header->kind != ThingKind::park_object) {
            return false;
        }
        auto& record = *reinterpret_cast<ParkObjectRuntimeView*>(header);
        record.prebuilt_track_edited = 1;
        if (entry_blocked || (active && live() == &record)) {
            return false;
        }
        const auto* opening = world_operations::find_mesh_runtime_opening_state(c, record.mesh_instance_id);
        if (!opening) {
            return false;
        }
        const bool placement = (opening->placement_flags & 1u) && !(opening->placement_flags & 2u);
        if (!placement) {
            if (adjust_park_object_context_coaster_camera(c, record, c.staff.interaction.cursor.cell) && host.request) {
                host.request(WorldCameraAction { 0, 0 });
            }
            return open(id, CoasterEditorEntryTool::build_supports);
        }
        const auto result = open(id, placement_entry ? CoasterEditorEntryTool::adjust_support_height : CoasterEditorEntryTool::delete_track_section, placement_entry);
        primary->cursor_id = placement_entry ? 12 : 10;
        return result;
    }

    void close() {
        if (closing) {
            return;
        }
        closing = true;
        if (active) {
            for (const auto& tool : tools) {
                cancel_advisor(tool.advisor);
            }
            if (ui.owns(control)) {
                ui.set_visible(*control, false);
            }
            host.set_editor_toolbar_active(false);
            if (editor.active_map() && c.construction.coaster_editor_active) {
                editor.deactivate_ui_context(c);
            }
            active = c.construction.coaster_editor_active = false;
            if (ui.owns(primary)) {
                primary->cursor_id = saved_cursor;
            }
            object = 0;
            identity = nullptr;
            previous_selection.reset();
            host.synchronize_focus();
        }
        if (editor.consume_followup()) {
            world_operations::queue_scored_advisor_action(c, 642);
        }
        closing = false;
    }

    void advance() {
        if (!active) {
            return;
        }
        if (!live() || !editor.active_object(c)) {
            close();
            return;
        }
        project();
    }

    std::intptr_t on_event(UiControl& sender, const UiEvent& event) {
        if (event.type == 20) {
            control = group = nullptr;
            close();
            return ui.secondary(sender, event);
        }
        if (event.type == 29) {
            entry_blocked = event.id == 1;
        }
        if (!closing && active) {
            if (event.type == 256 && event.id == 110) {
                close();
            } else if (event.type == 257) {
                const auto found = std::ranges::find(tools, event.id, &ToolControl::id);
                if (found != tools.end()) {
                    const bool activating = event.value == 1;
                    //Approved repair of52EA50: finish the tool activated by this row.
                    const auto tool = found->tool;
                    if (activating && found->activation_cursor >= 0) {
                        primary->cursor_id = found->activation_cursor;
                    }
                    editor.select_tool(c, tool, activating);
                    if (activating) {
                        world_operations::queue_scored_advisor_action(c, found->advisor);
                    } else {
                        cancel_advisor(found->advisor);
                    }
                    project(false);
                }
            } else if (event.type == 65546 || event.type == 65547) {
                const bool camera_handled = host.camera_key && host.camera_key(event);
                if (!camera_handled) {
                    const auto key = event.id > 255 ? event.id >> 8 : event.id;
                    const auto modifiers = (static_cast<std::uint32_t>(event.value) >> 16) & 255u;
                    if (event.type == 65547 && key == 27 && !modifiers) {
                        close(); //Native42F9E0 is an unregistered4-instruction thunk.
                    } else if (!modifiers) {
                        (void)editor.coaster_key(c, event.id, static_cast<std::uint32_t>(event.type));
                    }
                }
            }
        }
        return ui.secondary(sender, event);
    }
};

WorldCoasterEditorUiController::WorldCoasterEditorUiController(WorldRuntime& c, UiRuntime& ui, UiControl* primary, const std::filesystem::path&, WorldUiPanelHost host) : state_(std::make_unique<State>(c, ui, primary, std::move(host))) {}

WorldCoasterEditorUiController::~WorldCoasterEditorUiController() = default;

bool WorldCoasterEditorUiController::open(ThingId id, CoasterEditorEntryTool tool) {
    return state_->open(id, tool);
}

bool WorldCoasterEditorUiController::open() const {
    return state_->active;
}

bool WorldCoasterEditorUiController::open_world_target(ThingId id, bool placement_entry) {
    return state_->open_world(id, placement_entry);
}

void WorldCoasterEditorUiController::delete_context_geometry(ThingId id, std::uint32_t mesh_slot) {
    (void)state_->open_world(id, false);
    if (state_->editor.active_object(state_->c) == id) {
        state_->editor.delete_context_geometry(state_->c, mesh_slot);
    }
}

bool WorldCoasterEditorUiController::entry_blocked() const {
    return state_->entry_blocked;
}

ThingId WorldCoasterEditorUiController::target() const {
    return state_->object;
}

UiControl* WorldCoasterEditorUiController::root() const {
    return state_->control;
}

void WorldCoasterEditorUiController::close() {
    state_->close();
}

void WorldCoasterEditorUiController::advance() {
    state_->advance();
}

void WorldCoasterEditorUiController::retire(ThingId id) {
    if (state_->object == id) {
        state_->close();
    }
}

void WorldCoasterEditorUiController::handle_event(const WorldEvent& event) {
    if (event.type() == 33) {
        retire(event.get<RetirementEvent>().retired_id);
    }
}
} //namespace simcoaster
