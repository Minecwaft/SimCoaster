#include "simcoaster/park_authoring.hpp"
#include "simcoaster/coaster_catalog.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/object_catalog.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/park_special_ride_construction.hpp"
#include "simcoaster/ui_control.hpp"
#include "simcoaster/world_object_controls.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_staff_interaction.hpp"
#include "world_ui_build_catalog.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
bool bounds(WorldCellCoordinates p) {
    return is_world_map_coordinate_in_bounds(p.x, p.y);
}

CellId id(WorldCellCoordinates p) {
    return static_cast<CellId>(p.y * 128 + p.x + 1);
}

bool dependent(const ObjectDefinitionBalance& b) {
    return b.AddOn.UpgradeType || b.Info.IsPhotoBooth;
}
} //namespace

ParkAuthoringController::ParkAuthoringController(ParkRuntime& park) : park_(park) {}

bool ParkAuthoringController::replaying() const {
    return park_.action_recording().progress().state == LayoutReplayState::replaying;
}

void ParkAuthoringController::remember_selection(ParkObjectRuntimeView& candidate) {
    auto& c = park_.context();
    auto* object = &candidate;
    //Native refresh_origin redirects track attachments to their owning ride.
    //Keep the frontend value identity aligned with that actual edit receiver.
    if (candidate.upgrade_parent && get_park_object_balance(c, candidate).AddOn.UpgradeType == 1) {
        auto* parent = c.entities.at(candidate.upgrade_parent).payload;
        if (!parent || parent->kind != ThingKind::park_object || parent->retirement_pending) {
            throw std::logic_error("selected attachment has no live owning ride");
        }
        object = reinterpret_cast<ParkObjectRuntimeView*>(parent);
    }
    selected_ = object->base.header.id;
    selected_definition_ = object->definition_id;
    selected_creation_time_ = object->creation_file_time;
}

ParkObjectRuntimeView* ParkAuthoringController::selected_object() {
    auto& c = park_.context();
    auto* header = selected_ ? c.entities.at(selected_).payload : nullptr;
    if (!header || header->retirement_pending || header->kind != ThingKind::park_object) {
        selected_ = 0;
        return nullptr;
    }
    auto* object = reinterpret_cast<ParkObjectRuntimeView*>(header);
    if (object->definition_id != selected_definition_ || object->creation_file_time != selected_creation_time_) {
        selected_ = 0;
        return nullptr;
    }
    return object;
}

void ParkAuthoringController::bind_selected(bool record) {
    auto& c = park_.context();
    c.construction.park_object = selected_object();
    c.view.secondary_cursor_record_snapshot = c.construction.park_object ? &c.construction.park_object->base.header : nullptr;
    park_.construction().refresh_origin(c, record);
    if (record) {
        park_.construction().state.previous_origin = c.construction.origin_cell;
    }
}

ThingId ParkAuthoringController::selected() {
    return selected_object() ? selected_ : 0;
}

std::u16string ParkAuthoringController::selected_name() {
    const auto* object = selected_object();
    if (!object) {
        return {};
    }
    std::u16string name;
    const auto append = [&](const auto& part) {
        const auto end = std::ranges::find(part, char16_t {});
        if (end == part.begin()) {
            return;
        }
        if (!name.empty()) {
            name += u' ';
        }
        name.append(part.begin(), end);
    };
    append(object->name_a);
    append(object->name_b);
    return name.empty() ? get_catalog_object_display_name(park_.context(), object->definition_id) : name;
}

std::vector<ParkAuthoringCatalogEntry> ParkAuthoringController::catalog() {
    auto& c = park_.context();
    auto* parent = selected_object();
    std::vector<ParkAuthoringCatalogEntry> rows;
    for (const auto* definition : c.resources.object_catalog.entries) {
        if (!definition || !definition->balance) {
            continue;
        }
        const auto& b = *definition->balance;
        if (!b.Info.Shape || b.Info.WhichUIType == 4 || b.Info.WhichUIType == 6) {
            continue;
        }
        ParkCatalogAvailability availability = ParkCatalogAvailability::available;
        const auto* control = find_world_object_control(c.simulation.current->object_controls, definition->id);
        if (!control || !control->available) {
            availability = ParkCatalogAvailability::research_locked;
        } else if (b.UsageInfo.SingleInstance && control->live_instance_count) {
            availability = ParkCatalogAvailability::single_instance;
        } else if (is_coaster_catalog_definition(*definition) && !park_.coaster_catalog().available(c, definition->id)) {
            availability = ParkCatalogAvailability::coaster_editor;
        } else if (dependent(b)) {
            if (!parent) {
                availability = ParkCatalogAvailability::requires_selection;
            } else if ((!b.AddOn.UpgradesAnyRide && b.AddOn.UpgradesId && b.AddOn.UpgradesId != parent->definition_id) || (b.AddOn.UpgradeType == 1 && !parent->track_ride_handle) || (b.Info.IsPhotoBooth && parent->photo_booth)) {
                availability = ParkCatalogAvailability::incompatible_selection;
            }
        }
        rows.push_back({ definition->id, definition->which_ui_type, get_catalog_object_display_name(c, definition->id), b.Upgrades[0].CostOfUpgrade, availability, dependent(b) });
    }
    std::ranges::sort(rows, [](const auto& a, const auto& b) {
        return a.category == b.category ? a.name < b.name : a.category < b.category;
    });
    return rows;
}

ParkLevelUpgrade ParkAuthoringController::level_upgrade() {
    auto* object = selected_object();
    if (!object) {
        return {};
    }
    auto& c = park_.context();
    ParkLevelUpgrade result;
    result.level = static_cast<std::uint32_t>(object->current_upgrade) + 1u;
    const auto& balance = get_park_object_balance(c, *object);
    if (result.level < balance.Upgrades.size()) {
        result.cost = balance.Upgrades.at(result.level).CostOfUpgrade;
    }
    if (object->state == ParkObjectState::condemned || !(std::to_underlying(object->capabilities) & std::to_underlying(ParkObjectCapabilities::is_choosable))) {
        result.availability = ParkLevelUpgradeAvailability::not_upgradeable;
        return result;
    }
    if (object->state == ParkObjectState::upgrade_requested) {
        result.availability = ParkLevelUpgradeAvailability::pending;
        return result;
    }
    if (object->current_upgrade >= 2) {
        result.availability = ParkLevelUpgradeAvailability::maximum_level;
        return result;
    }
    const auto* control = find_world_object_control(c.simulation.current->object_controls, object->definition_id);
    if (!control || static_cast<std::int32_t>(result.level) > std::bit_cast<std::int32_t>(control->research_option_index)) {
        result.availability = ParkLevelUpgradeAvailability::research_locked;
        return result;
    }
    bool trained = false;
    std::size_t visited = 0;
    for (auto staff = c.simulation.current->saved.first_mechanic; staff;) {
        if (++visited > c.entities.size()) {
            throw std::length_error("cyclic mechanic roster");
        }
        const auto* header = c.entities.at(staff).payload;
        if (!header || header->kind != ThingKind::mechanic) {
            throw std::logic_error("mechanic roster contains an invalid record");
        }
        const auto& mechanic = *reinterpret_cast<const StaffRuntimeRecord*>(header);
        if (!header->retirement_pending && world_staff_first_known_ability_matches(mechanic, ThemeAbility::ride_upgrading)) {
            trained = true;
        }
        staff = mechanic.next_staff;
    }
    if (!trained) {
        result.availability = ParkLevelUpgradeAvailability::needs_mechanic;
        return result;
    }
    const auto* account = c.entities.at(c.simulation.current->saved.bank_account).payload;
    if (!account || account->kind != ThingKind::bank_account) {
        throw std::logic_error("park authoring requires its live bank");
    }
    //Native595130 uses strict greater-than for level-ups. Requesting does
    //not debit cash: the assigned mechanic charges upon actual completion.
    const auto funds = reinterpret_cast<const BankRecord*>(account)->balance;
    result.availability = funds > result.cost ? ParkLevelUpgradeAvailability::available : ParkLevelUpgradeAvailability::insufficient_funds;
    return result;
}

bool ParkAuthoringController::request_level_upgrade() {
    if (replaying()) {
        return false;
    }
    const auto option = level_upgrade();
    if (option.availability != ParkLevelUpgradeAvailability::available) {
        match (option.availability) {
            case ParkLevelUpgradeAvailability::needs_mechanic => {
                feedback_.message = u"A mechanic trained in ride upgrading is required";
            }
            case ParkLevelUpgradeAvailability::research_locked => {
                feedback_.message = u"Research the next upgrade level first";
            }
            case ParkLevelUpgradeAvailability::insufficient_funds => {
                feedback_.message = u"Level upgrades require cash greater than their quoted cost";
            }
            case ParkLevelUpgradeAvailability::pending => feedback_.message = u"This upgrade is already awaiting a mechanic";
            case _                                     => feedback_.message = u"This object has no available level upgrade";
        } return false;
    }
    cancel();
    bind_selected();
    auto& c = park_.context();
    auto& object = *selected_object();
    //Native5E3600: publish the real state event, then select upgrade hoarding.
    transition_park_object_state(c, object, ParkObjectState::upgrade_requested);
    world_operations::select_park_object_mesh_animation_variant(c, object.mesh_instance_id, ParkObjectMeshVariant::upgrade);
    feedback_.message = u"Upgrade requested; its cost is charged when the mechanic completes it";
    return true;
}

bool ParkAuthoringController::cancel_level_upgrade() {
    if (replaying()) {
        return false;
    }
    auto* object = selected_object();
    if (!object || object->state != ParkObjectState::upgrade_requested) {
        return false;
    }
    cancel();
    bind_selected();
    auto& c = park_.context();
    object = selected_object();
    //The native cancellation helper reverses hoarding and takes its ordinary
    //open-object path; it neither increments the level nor refunds a charge.
    world_operations::configure_mesh_instance_deformation_animation(c, object->mesh_instance_id);
    open_park_object(c, *object);
    feedback_.message = u"Upgrade request cancelled";
    return true;
}

bool ParkAuthoringController::select(WorldCellCoordinates p) {
    if (replaying()) {
        return false;
    }
    if (!bounds(p)) {
        return false;
    }
    auto& c = park_.context();
    auto& row = c.simulation.current->map_cells.at(static_cast<std::size_t>(id(p)) - 1);
    const auto type = row.base.common.type;
    CellId owner = (type == 4 || type == 9 || type == 10 || row.is_obstacle) ? row.base.common.parent_id : row.base.index;
    if (!owner || owner > map_cell_count) {
        return false;
    }
    const auto object_id = find_first_park_object_id_in_map_cell(c, &c.simulation.current->map_cells.at(owner - 1u));
    auto* header = object_id ? c.entities.at(object_id).payload : nullptr;
    if (!header || header->retirement_pending || header->kind != ThingKind::park_object) {
        selected_ = 0;
        feedback_.message = u"No park object at this position";
        return false;
    }
    auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(header);
    remember_selection(object);
    bind_selected(true);
    feedback_.message = selected_name();
    return true;
}

bool ParkAuthoringController::choose_object(std::uint16_t definition) {
    if (replaying()) {
        return false;
    }
    const auto rows = catalog();
    const auto found = std::ranges::find(rows, definition, &ParkAuthoringCatalogEntry::definition);
    if (found == rows.end() || found->availability != ParkCatalogAvailability::available) {
        return false;
    }
    cancel();
    bind_selected();
    auto& c = park_.context();
    cancel_world_staff_interaction(c);
    placement_definition_ = definition;
    park_.construction().begin_placement(c, definition, feedback_.rotation);
    tool_ = ParkAuthoringTool::object;
    feedback_.message = found->name;
    return true;
}

bool ParkAuthoringController::choose_build_object(std::uint16_t id) {
    if (replaying()) {
        return false;
    }
    auto& c = park_.context();
    const auto* definition = find_object_definition_by_id(&c.resources.object_catalog, id);
    if (!definition || !is_build_catalog_definition_available(c, *definition)) {
        return false;
    }
    const auto* control = find_world_object_control(c.simulation.current->object_controls, id);
    const auto* bank = c.entities.at(c.simulation.current->saved.bank_account).payload;
    if (!control || !bank || bank->kind != ThingKind::bank_account) {
        throw std::logic_error("Build selection requires its object control/bank");
    }
    if ((definition->balance->UsageInfo.SingleInstance == 1 && control->live_instance_count) || std::bit_cast<std::int32_t>(control->purchase_cost) > reinterpret_cast<const BankRecord*>(bank)->balance) {
        return false;
    }
    //Native6730F0: nearest cardinal view heading, with exact half-turn ties
    //toward the lower magnitude. WorldRuntime Buy deliberately does not use this.
    auto rotation = feedback_.rotation;
    if (definition->balance->Prebuilt.IsPrebuiltCoaster != 1) {
        const double heading = c.view.camera.heading_radians;
        auto remainder = std::fmod(heading, 1.570796370506287);
        double snapped;
        if (remainder > .78539819) {
            snapped = 1.5707964 - remainder + heading;
        } else {
            if (remainder < -.78539819) {
                remainder += 1.5707964;
            }
            snapped = heading - remainder;
        }
        if (!std::isfinite(snapped)) {
            throw std::invalid_argument("Build camera heading is not finite");
        }
        rotation = static_cast<int>(snapped * 57.29578) % 360;
        if (rotation < 0) {
            rotation += 360;
        }
        //The placement API carries the cardinal orientation selected above;
        //normalize the degree representation at the geometry boundary.
        rotation = ((rotation + 1) / 90 * 90) % 360;
    }
    cancel();
    bind_selected();
    cancel_world_staff_interaction(c);
    placement_definition_ = id;
    feedback_.rotation = rotation;
    park_.construction().begin_placement(c, id, rotation);
    tool_ = ParkAuthoringTool::object;
    feedback_.message = get_catalog_object_display_name(c, id);
    return true;
}

bool ParkAuthoringController::delete_object(ThingId target) {
    if (replaying()) {
        return false;
    }
    auto& c = park_.context();
    auto* record = target && target < c.entities.size() ? c.entities[target].payload : nullptr;
    if (!record || record->retirement_pending || record->kind != ThingKind::park_object) {
        return false;
    }
    auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(record);
    //556620/556710 use the receiver's fixed-point map coordinates, then
    //refresh origin, record mode51 and dispatch the complete cell operation.
    const WorldCellCoordinates tile { object.base.x >> 8, object.base.y >> 8 };
    if (!bounds(tile)) {
        return false;
    }
    cancel();
    remember_selection(object);
    bind_selected(true);
    auto& editor = park_.construction();
    editor.set_mode(c, 51, true);
    (void)editor.dispatch(c, tile, WorldInputModifierMask::none, false);
    const bool removed = record->retirement_pending != 0;
    synchronize_tool();
    if (removed) {
        selected_ = 0;
        feedback_.message = u"Park updated";
    }
    return removed;
}

bool ParkAuthoringController::buy_object(ThingId target) {
    if (replaying()) {
        return false;
    }
    auto& c = park_.context();
    auto* record = target && target < c.entities.size() ? c.entities[target].payload : nullptr;
    if (!record || record->retirement_pending || record->kind != ThingKind::park_object) {
        return false;
    }
    auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(record);
    const auto definition = object.definition_id;
    const auto rotation = std::bit_cast<std::int32_t>(object.angle);
    const auto name = get_catalog_object_display_name(c, definition);
    cancel();
    bind_selected();
    cancel_world_staff_interaction(c);
    placement_definition_ = definition;
    //Native556A00 copies an existing definition without catalog-availability
    //gates, then copies its actual degree angle after
    //installing placement mode4; it does not place a replacement at the old
    //cell.
    feedback_.rotation = rotation;
    park_.construction().begin_placement(c, definition, rotation);
    tool_ = ParkAuthoringTool::object;
    feedback_.message = name;
    return true;
}

bool ParkAuthoringController::choose_design(const std::filesystem::path& path) {
    if (replaying()) {
        return false;
    }
    cancel();
    auto& c = park_.context();
    park_.coaster_catalog().select_design_file(c, path, feedback_.rotation);
    placement_definition_ = c.construction.selected_object_definition_id;
    const auto* control = find_world_object_control(c.simulation.current->object_controls, placement_definition_);
    if (!control || !control->available) {
        cancel();
        feedback_.message = u"Coaster design requires an available ride";
        return false;
    }
    cancel_world_staff_interaction(c);
    tool_ = ParkAuthoringTool::object;
    feedback_.message = u"Choose coaster placement; R rotates";
    return true;
}

bool ParkAuthoringController::choose_build_design(const std::filesystem::path& path, std::int32_t quote) {
    if (replaying()) {
        return false;
    }
    auto& c = park_.context();
    const auto* bank = c.entities.at(c.simulation.current->saved.bank_account).payload;
    if (!bank || bank->kind != ThingKind::bank_account) {
        throw std::logic_error("Ready-made placement requires its live bank");
    }
    if (quote > reinterpret_cast<const BankRecord*>(bank)->balance) {
        return false;
    }
    auto input = c.ports.assets.open_asset_stream(c, path.native_encoded_string());
    if (!input) {
        throw std::runtime_error("Unable to open selected ready-made coaster");
    }
    auto design = read_coaster_design(*input);
    if (!design) {
        throw std::invalid_argument("Invalid selected ready-made coaster stream");
    }
    //Native52D030 retains the displayed quote while re-reading the design's
    //names/grid/offsets. It does not replace that quote with the new header
    //cost.
    cancel();
    park_.coaster_catalog().select_design(c, std::move(*design), feedback_.rotation, quote);
    placement_definition_ = c.construction.selected_object_definition_id;
    cancel_world_staff_interaction(c);
    tool_ = ParkAuthoringTool::object;
    return true;
}

bool ParkAuthoringController::choose_tool(ParkAuthoringTool tool) {
    if (replaying()) {
        return false;
    }
    if (tool == ParkAuthoringTool::object) {
        return false;
    }
    if ((tool == ParkAuthoringTool::queue || tool == ParkAuthoringTool::track || tool == ParkAuthoringTool::erase_track) && !selected_object()) {
        feedback_.message = u"Select a ride first";
        return false;
    }
    if (tool == ParkAuthoringTool::track || tool == ParkAuthoringTool::erase_track) {
        const auto& b = get_park_object_balance(park_.context(), *selected_object());
        if (b.Bumper.WhichTrackType != 1 && b.Bumper.WhichTrackType != 2 && !(tool == ParkAuthoringTool::track && b.Bumper.WhichTrackType == 3)) {
            feedback_.message = u"Select a compatible ride to edit its track";
            return false;
        }
    }
    if (tool == ParkAuthoringTool::queue || tool == ParkAuthoringTool::track || tool == ParkAuthoringTool::erase_track) {
        const auto& object = *selected_object();
        const auto& balance = get_park_object_balance(park_.context(), object);
        if (balance.Info.IsSpecial && !object.activated) {
            feedback_.message = u"Activate this special ride before editing it";
            return false;
        }
        if (tool == ParkAuthoringTool::queue && (!balance.Info.HasQueue || !object.entry_cell)) {
            feedback_.message = u"This object has no editable queue";
            return false;
        }
    }
    cancel();
    bind_selected();
    auto& c = park_.context();
    auto& editor = park_.construction();
    cancel_world_staff_interaction(c);
    if (tool == ParkAuthoringTool::track && get_park_object_balance(c, *selected_object()).Bumper.WhichTrackType == 3) {
        if (!c.construction.editor) {
            throw std::logic_error("coaster editor is not bound");
        }
        c.construction.editor->activate_object(c, selected_);
        tool_ = tool;
        feedback_.message = u"N: select node under cursor | G: add node | M: move " u"selected node | Enter: finish";
        return true;
    }
    int mode = 0;
    match (tool) {
        case ParkAuthoringTool::inspect => {
        }
        case ParkAuthoringTool::path  => mode = 1;
        case ParkAuthoringTool::queue => mode = 20;
        case ParkAuthoringTool::track => {
            mode = get_park_object_balance(c, *selected_object()).Bumper.WhichTrackType == 1 ? 11 : 16;
            reopen_after_cancel_ = selected_object()->can_load != 0;
            close_park_object(c, *selected_object());
        }
        case ParkAuthoringTool::demolish    => mode = 51;
        case ParkAuthoringTool::erase_path  => mode = 52;
        case ParkAuthoringTool::erase_track => {
            mode = 53;
            reopen_after_cancel_ = selected_object()->can_load != 0;
            close_park_object(c, *selected_object());
        }
        case ParkAuthoringTool::hoarding_chain     => mode = 25;
        case ParkAuthoringTool::land_boundary      => mode = 57;
        case ParkAuthoringTool::demolish_rectangle => mode = 58;
        case ParkAuthoringTool::object             => {
        }
        case _ => {
        }
    } editor.set_mode(c, mode, true);
    if (mode == 20) {
        editor.initialize_context_mode(c);
    }
    tool_ = tool;
    feedback_.message = tool == ParkAuthoringTool::inspect ? u"Click a park object to select it" : u"Click to start; click the next point to build; Escape finishes";
    return true;
}

void ParkAuthoringController::rotate(int turns) {
    if (replaying()) {
        return;
    }
    if (tool_ != ParkAuthoringTool::object) {
        return;
    }
    feedback_.rotation = ((feedback_.rotation + 90 * (turns % 4)) % 360 + 360) % 360;
    park_.construction().state.rotation_degrees = feedback_.rotation;
    const auto recorded = park_.action_recording().record(park_.context(), { LayoutActionCode::set_rotation, LayoutWord { static_cast<std::uint16_t>(feedback_.rotation) } });
    if (!recorded) {
        throw std::runtime_error(describe_layout_error(recorded.error()));
    }
}

void ParkAuthoringController::reset_feedback() {
    auto& c = park_.context();
    const auto* bank_header = c.entities.at(c.simulation.current->saved.bank_account).payload;
    if (!bank_header || bank_header->kind != ThingKind::bank_account) {
        throw std::logic_error("park authoring requires its live bank");
    }
    feedback_.balance = reinterpret_cast<const BankRecord*>(bank_header)->balance;
    feedback_.accepted = false;
    feedback_.cost = 0;
    feedback_.insufficient_funds = false;
    feedback_.rejection.clear();
}

void ParkAuthoringController::hover(std::optional<WorldCellCoordinates> cursor, WorldInputModifierMask modifiers) {
    //No cursor, another UI owner, or an inspect/replay pass cannot republish
    //the previous construction quote merely because its raw accumulator lives.
    park_.construction().invalidate_interaction_cost_quote();
    if (replaying()) {
        return;
    }
    if (cursor && bounds(*cursor)) {
        park_.construction().resume_shortcut_tool(park_.context(), cursor);
    }
    synchronize_from_world();
    if (park_.coaster_editor().geometry_interaction_active()) {
        park_.construction().state.faces.clear();
        return; //Kind8 requires a mesh hit and physical pixels, not a cell.
    }
    if ((tool_ == ParkAuthoringTool::queue || tool_ == ParkAuthoringTool::track || tool_ == ParkAuthoringTool::erase_track) && !selected_object()) {
        cancel(modifiers);
        feedback_.message = u"The selected ride is no longer in the park";
    }
    reset_feedback();
    feedback_.cursor = cursor;
    if (!cursor || !bounds(*cursor)) {
        park_.construction().state.faces.clear();
        return;
    }
    auto& c = park_.context();
    auto& editor = park_.construction();
    auto& state = editor.state;
    if (tool_ == ParkAuthoringTool::inspect) {
        if (!c.construction.mode) {
            state.faces.clear();
        }
        return;
    }
    if (tool_ == ParkAuthoringTool::object) {
        if (c.construction.mode == 59) {
            editor.bind_relocation_parent(c);
        } else {
            bind_selected();
        }
    }
    state.rotation_degrees = feedback_.rotation;
    feedback_.accepted = editor.pointer_hover(c, *cursor, modifiers);
    feedback_.cursor = state.preview_coordinates;
    if (tool_ == ParkAuthoringTool::demolish) {
        const auto& row = c.simulation.current->map_cells.at(id(*cursor) - 1u);
        const auto type = row.base.common.type;
        bool eligible = (type == 4 || type == 9 || type == 10 || row.is_obstacle) && row.base.common.parent_id && row.base.common.parent_id <= map_cell_count;
        if (eligible) {
            const auto target = find_first_park_object_id_in_map_cell(c, &c.simulation.current->map_cells.at(row.base.common.parent_id - 1u));
            const auto* header = target ? c.entities.at(target).payload : nullptr;
            eligible = header && header->kind == ThingKind::park_object && !header->retirement_pending;
        }
        if (!eligible) {
            feedback_.accepted = false;
            state.validation_failed = state.status_latch = true;
            feedback_.rejection = u"There is no park object to demolish here";
            for (auto& face : state.faces) {
                face.material = 1;
            }
        }
    }
    if (tool_ == ParkAuthoringTool::erase_path || tool_ == ParkAuthoringTool::erase_track) {
        //Raw remove-cell commands are also used by whole-object destruction.
        //A player's path eraser must never clear an object's footprint without
        //retiring its owner. Validate the complete gesture before dispatch.
        const auto end = state.preview_coordinates;
        const WorldCellCoordinates pending { c.construction.path_start_x, c.construction.path_start_y };
        auto current = bounds(pending) ? pending : end;
        const bool horizontal = current.x != end.x;
        const int scale = tool_ == ParkAuthoringTool::erase_track ? 2 : 1;
        const int step = (horizontal ? current.x < end.x : current.y < end.y) ? scale : -scale;
        bool allowed = bounds(current) && bounds(end);
        for (std::size_t visited = 0; allowed && visited < map_cell_count; ++visited) {
            const auto& row = c.simulation.current->map_cells.at(id(current) - 1u);
            if (tool_ == ParkAuthoringTool::erase_path) {
                const auto type = row.base.common.type;
                allowed = (type == 0 || type == 1 || type == 3) && !(row.base.common.flags & 32u) && !row.is_obstacle;
            } else {
                auto* track = resolve_track_cell_for_grid_entry(c, &row.base);
                if (track && world_cell_is_track_child(&track->base)) {
                    track = track->base.common.parent_id ? &c.simulation.current->track_cells.at(track->base.common.parent_id - 1u) : nullptr;
                }
                allowed = track && (track->base.common.type == 11 || track->base.common.type == 16) && track->base.common.parent_id == c.construction.origin_cell;
            }
            if (current.x == end.x && current.y == end.y) {
                break;
            }
            if (horizontal) {
                current.x += step;
            } else {
                current.y += step;
            }
            allowed = allowed && bounds(current);
        }
        allowed = allowed && current.x == end.x && current.y == end.y;
        if (!allowed) {
            feedback_.accepted = false;
            state.validation_failed = state.status_latch = true;
            for (auto& face : state.faces) {
                face.material = 1;
            }
            feedback_.rejection = tool_ == ParkAuthoringTool::erase_path ? u"Use Demolish for park objects; entrance paths are protected" : u"Only ordinary track belonging to the selected ride can be " u"removed";
        }
    }
    feedback_.cost = state.cost;
    feedback_.insufficient_funds = state.insufficient_funds;
}

void ParkAuthoringController::mouse_down(WorldCellCoordinates p, bool keep) {
    mouse_down(p, static_cast<WorldInputModifierMask>(keep ? 12u : 0u));
}

void ParkAuthoringController::mouse_down(WorldCellCoordinates p, WorldInputModifierMask modifiers) {
    if (!replaying() && bounds(p)) {
        park_.construction().resume_shortcut_tool(park_.context(), p);
    }
    synchronize_from_world();
    if (replaying() || tool_ == ParkAuthoringTool::inspect || park_.coaster_editor().geometry_interaction_active()) {
        return;
    }
    if (!feedback_.cursor) {
        hover(p, modifiers);
    }
    park_.construction().pointer_down(park_.context(), p, modifiers, true);
    mouse_down_ = true;
}

void ParkAuthoringController::cancel_pointer_input(WorldInputModifierMask modifiers) {
    if (mouse_down_) {
        cancel_interaction(modifiers);
    }
}

void ParkAuthoringController::synchronize_tool() {
    if (!std::holds_alternative<DefaultWorldInteraction>(park_.context().staff.interaction.handler.value)) {
        //Patrol rendering publishes mode54 without transferring its handler to
        //terrain construction. Hiring and held Staff also own separate input.
        tool_ = ParkAuthoringTool::inspect;
        return;
    }
    const auto* coaster = park_.context().construction.editor;
    if (coaster && coaster->active_map() && coaster->input_route() != CoasterEditorInputRoute::map_action) {
        //Numeric mode21 survives Insert Support's raw rebind, but its native
        //handler is the ordinary World handler. Geometry tools likewise retain
        //their handler after deselection; neither path may place terrain track.
        tool_ = coaster->input_route() == CoasterEditorInputRoute::geometry ? ParkAuthoringTool::track : ParkAuthoringTool::inspect;
        return;
    }
    const auto mode = park_.context().construction.mode;
    match (mode == std::to_underlying(WorldInteractionMode::backtrack_preview) ? park_.construction().state.backtrack_saved_mode : mode) {
        case 0                                => tool_ = ParkAuthoringTool::inspect;
        case 1                                => tool_ = ParkAuthoringTool::path;
        case 3 or 20                          => tool_ = ParkAuthoringTool::queue;
        case 4 or 59                          => tool_ = ParkAuthoringTool::object;
        case 11 or 14 or 16 or 19 or 21 or 22 => tool_ = ParkAuthoringTool::track;
        case 51                               => tool_ = ParkAuthoringTool::demolish;
        case 52                               => tool_ = ParkAuthoringTool::erase_path;
        case 53                               => tool_ = ParkAuthoringTool::erase_track;
        case 25 or 26                         => tool_ = ParkAuthoringTool::hoarding_chain;
        case 57                               => tool_ = ParkAuthoringTool::land_boundary;
        case 58                               => tool_ = ParkAuthoringTool::demolish_rectangle;
        case _                                => throw std::logic_error("unexpected authoring interaction mode");
    }
}

bool ParkAuthoringController::click(WorldCellCoordinates p, bool keep) {
    return click(p, static_cast<WorldInputModifierMask>(keep ? 12u : 0u));
}

bool ParkAuthoringController::click(WorldCellCoordinates p, WorldInputModifierMask modifiers) {
    const bool keep = std::to_underlying(modifiers) == 12;
    if (replaying()) {
        return false;
    }
    if (bounds(p)) {
        park_.construction().resume_shortcut_tool(park_.context(), p);
    }
    synchronize_from_world();
    if (!std::holds_alternative<DefaultWorldInteraction>(park_.context().staff.interaction.handler.value)) {
        return false;
    }
    if (park_.coaster_editor().geometry_interaction_active()) {
        return false;
    }
    if (tool_ == ParkAuthoringTool::inspect && !mouse_down_) {
        return select(p);
    }
    //Live4D2E90 dispatches mouse-up using the last actual hover state. It
    //deliberately does not clear validation produced by the down phase.
    if (!mouse_down_) {
        hover(p, modifiers);
        mouse_down(p, modifiers);
    }
    if (!feedback_.cursor) {
        return false;
    }
    auto& c = park_.context();
    auto& editor = park_.construction();
    const bool placing = tool_ == ParkAuthoringTool::object;
    const bool alternate = c.construction.mode == 59;
    const bool accepted = !editor.state.validation_failed && !editor.state.status_latch;
    const auto serial = c.construction.grid_change_serial_counter;
    (void)editor.pointer_up(c, p, modifiers, true);
    track_changed_ |= c.construction.grid_change_serial_counter != serial && (tool_ == ParkAuthoringTool::track || tool_ == ParkAuthoringTool::erase_track);
    mouse_down_ = false;
    if (placing) {
        const auto created = editor.state.result.created_object;
        if (!created) {
            editor.resume_shortcut_tool(c, p);
            synchronize_tool();
            return false;
        }
        if (auto* header = c.entities.at(created).payload) {
            auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(header);
            remember_selection(object);
        }
        if (keep && !alternate && c.construction.mode == 0) {
            editor.begin_placement(c, placement_definition_, feedback_.rotation);
        }
        if (c.construction.mode == 11 || c.construction.mode == 16) {
            track_changed_ = true;
        }
    }
    editor.resume_shortcut_tool(c, p);
    synchronize_tool();
    feedback_.message = accepted ? u"Park updated" : u"Cannot build here";
    return accepted;
}

bool ParkAuthoringController::activate_selected() {
    if (replaying()) {
        return false;
    }
    if (!selected_object()) {
        return false;
    }
    const auto& object = *selected_object();
    if (get_park_object_balance(park_.context(), object).Info.IsSpecial != 1) {
        feedback_.message = u"This is not a special ride";
        return false;
    }
    if (object.activated) {
        feedback_.message = u"This ride is already activated";
        return false;
    }
    cancel();
    bind_selected();
    const auto result = activate_special_ride_for_construction(park_.context(), park_.construction(), selected_);
    if (!result) {
        feedback_.message = u"Activation needs the required tickets and assigned staff";
        return false;
    }
    feedback_.message = u"Ride activated";
    synchronize_tool();
    return true;
}

bool ParkAuthoringController::set_park_open(bool open) {
    if (replaying()) {
        return false;
    }
    cancel();
    auto& c = park_.context();
    handle_world_park_open_close(c, *c.simulation.current, open);
    feedback_.message = open ? u"Park opened" : u"Park closed";
    return true;
}

bool ParkAuthoringController::open_selected() {
    if (replaying()) {
        return false;
    }
    auto* object = selected_object();
    if (!object) {
        return false;
    }
    auto& c = park_.context();
    if (c.simulation.current->saved.park_closed) {
        feedback_.message = u"Open the park from the File menu before opening this ride";
        return false;
    }
    const auto& balance = get_park_object_balance(c, *object);
    if (!object->entry_cell || (!balance.Info.IsUsable && !balance.Info.IsChoosable)) {
        feedback_.message = u"This object has no opening control";
        return false;
    }
    if (balance.Info.IsSpecial && !object->activated) {
        feedback_.message = u"Activate this special ride before opening it";
        return false;
    }
    if ((balance.Bumper.WhichTrackType == 1 || balance.Bumper.WhichTrackType == 2) && !object->is_track_ride_valid) {
        feedback_.message = u"Complete the ride track before opening";
        return false;
    }
    if (!is_park_object_open_eligible(c, *object)) {
        feedback_.message = u"Connect the entrance and exit and complete the ride before opening";
        return false;
    }
    open_park_object(c, *object);
    feedback_.message = u"Ride opened";
    return true;
}

bool ParkAuthoringController::close_selected() {
    if (replaying()) {
        return false;
    }
    auto* object = selected_object();
    if (!object) {
        return false;
    }
    const auto& balance = get_park_object_balance(park_.context(), *object);
    if (!object->entry_cell || (!balance.Info.IsUsable && !balance.Info.IsChoosable)) {
        feedback_.message = u"This object has no closing control";
        return false;
    }
    close_park_object(park_.context(), *object);
    feedback_.message = u"Ride closed";
    return true;
}

bool ParkAuthoringController::select_coaster_node(WorldCellCoordinates tile) {
    if (replaying()) {
        return false;
    }
    auto& c = park_.context();
    if (!selected_object() || get_park_object_balance(c, *selected_object()).Bumper.WhichTrackType != 3) {
        return false;
    }
    if (!c.construction.editor) {
        throw std::logic_error("coaster editor is not bound");
    }
    if (!c.construction.editor->active_map()) {
        c.construction.editor->activate_object(c, selected_);
    }
    const bool found = c.construction.editor->select_at_tile(c, tile);
    if (found) {
        tool_ = ParkAuthoringTool::track;
        feedback_.message = u"Coaster node selected";
    }
    return found;
}

bool ParkAuthoringController::stage_coaster_node(bool special, bool select_pylon) {
    if (replaying()) {
        return false;
    }
    auto& c = park_.context();
    if (!selected_object() || get_park_object_balance(c, *selected_object()).Bumper.WhichTrackType != 3) {
        return false;
    }
    if (!c.construction.editor) {
        throw std::logic_error("coaster editor is not bound");
    }
    if (!c.construction.editor->active_map()) {
        c.construction.editor->activate_object(c, selected_);
    }
    park_.construction().set_mode(c, 21, true);
    c.construction.editor->stage(c, special, select_pylon);
    tool_ = ParkAuthoringTool::track;
    feedback_.message = u"Click to place the support; move the cursor to adjust it";
    return true;
}

bool ParkAuthoringController::move_coaster_node() {
    if (replaying()) {
        return false;
    }
    auto& c = park_.context();
    if (!c.construction.editor || !c.construction.editor->active_node()) {
        feedback_.message = u"Select a coaster node under the cursor first";
        return false;
    }
    c.construction.editor->begin_move(c, *c.construction.editor->active_node());
    tool_ = ParkAuthoringTool::track;
    feedback_.message = u"Click the new support position; Escape restores its original position";
    return true;
}

bool ParkAuthoringController::finalize_coaster() {
    if (replaying()) {
        return false;
    }
    auto& c = park_.context();
    if (!c.construction.editor || !c.construction.editor->active_map()) {
        return false;
    }
    c.construction.editor->finalize(c, park_.construction());
    synchronize_tool();
    feedback_.message = u"Coaster edit finished";
    return true;
}

void ParkAuthoringController::cancel(WorldInputModifierMask modifiers) {
    finish_interaction(modifiers, true);
}

void ParkAuthoringController::reset_for_ui() {
    auto& c = park_.context();
    finish_interaction(c.construction.modifiers, false);
    //50151F..501790 replaces the old handler before publishing Pointer.
    c.staff.interaction.pointer_mode_code = 1;
    c.staff.interaction.handler = { DefaultWorldInteraction {}, false };
    c.construction.handler = &c.staff.interaction.handler;
    c.staff.interaction.left_button_held = false;
    c.staff.interaction.cursor_presentation = 0;
    if (c.ui.persistent_root) {
        c.ui.persistent_root->cursor_id = 0;
    }
}

void ParkAuthoringController::finish_interaction(WorldInputModifierMask modifiers, bool record_action) {
    ++interaction_revision_;
    if (replaying()) {
        park_.cancel_layout_replay();
    }
    auto& c = park_.context();
    auto& editor = park_.construction();
    editor.finish_object_relocation(c, modifiers);
    if (c.construction.editor && c.construction.editor->active_map()) {
        c.construction.editor->cancel(c, editor);
        c.construction.editor->activate(c, 0, record_action);
    } else if (track_changed_ && (tool_ == ParkAuthoringTool::track || tool_ == ParkAuthoringTool::erase_track) && selected_object()) {
        bind_selected();
        selected_object()->is_track_ride_valid = editor.refresh_track_path(c);
    }
    if (reopen_after_cancel_ && selected_object() && is_park_object_open_eligible(c, *selected_object())) {
        open_park_object(c, *selected_object());
    }
    editor.set_mode(c, 0, record_action);
    editor.reset_after_restore();
    c.construction.line_endpoint_dispatch = 0;
    c.construction.track_endpoint_phase = 0;
    c.construction.park_object = nullptr;
    c.view.secondary_cursor_record_snapshot = nullptr;
    c.construction.origin_cell = 0;
    park_.coaster_catalog().clear(c);
    c.construction.selected_track_type = 0;
    c.construction.track_type = 0;
    c.construction.interaction_cursor_anchor = { -1, -1 };
    c.construction.interaction_secondary_anchor = { -1, -1 };
    tool_ = ParkAuthoringTool::inspect;
    feedback_.cursor.reset();
    feedback_.accepted = false;
    mouse_down_ = false;
    track_changed_ = reopen_after_cancel_ = false;
    feedback_.message = u"Tool finished";
}

void ParkAuthoringController::save(const std::filesystem::path& path, WorldInputModifierMask modifiers) {
    cancel(modifiers);
    park_.save(path);
    feedback_.message = u"Park saved";
}

bool ParkAuthoringController::relocate_object(ThingId id) {
    auto& c = park_.context();
    auto* header = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    if (replaying() || !header || header->retirement_pending || header->kind != ThingKind::park_object) {
        return false;
    }
    auto& source = *reinterpret_cast<ParkObjectRuntimeView*>(header);
    cancel();
    cancel_world_staff_interaction(c);
    if (!park_.construction().begin_object_relocation(c, source)) {
        return false;
    }
    placement_definition_ = c.construction.selected_object_definition_id;
    feedback_.rotation = park_.construction().state.rotation_degrees;
    tool_ = ParkAuthoringTool::object;
    mouse_down_ = false;
    //The original receiver has retired. Keep only the related live identity
    //when one exists; construction owns the source metadata independently.
    if (auto* related = c.construction.park_object) {
        remember_selection(*related);
    }
    return true;
}
} //namespace simcoaster
