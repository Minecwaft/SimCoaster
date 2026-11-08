#include "simcoaster/entity_identity.hpp"
#include "simcoaster/world_cursor_actions.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/object_balance.hpp"
#include "simcoaster/native_coaster_editor.hpp"
#include "simcoaster/park_authoring.hpp"
#include "simcoaster/park_object_context_geometry.hpp"
#include "simcoaster/park_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_bank.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_coaster_runtime.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_grid.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_object_ui.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_queue.hpp"
#include "simcoaster/world_special_rides.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_surface.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_ui_coaster_editor.hpp"
#include "simcoaster/world_ui_objects.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "simcoaster/world_ui_staff.hpp"
#include "simcoaster/world_ui_visitors.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace simcoaster {
namespace {
using Action = WorldCursorAction;

using Identity = EntityIdentity;

std::uint32_t mobile_body_script(const ThingHeader& record) {
    if (record.kind == ThingKind::person) {
        return reinterpret_cast<const PersonRuntimeView&>(record).mobile.animation.sprite_script;
    }
    if (is_staff_record_kind(record.kind)) {
        return reinterpret_cast<const StaffRuntimeRecord&>(record).mobile.animation.sprite_script;
    }
    return 0;
}

ParkObjectRuntimeView* object(WorldRuntime& c, ThingId id) {
    return find_live_entity<ParkObjectRuntimeView>(c.entities, id);
}

ParkObjectRuntimeView* owner(WorldRuntime& c, CellId parent) {
    return c.simulation.current && parent && parent <= map_cell_count ? object(c, find_first_park_object_id_in_map_cell(c, &c.simulation.current->map_cells.at(parent - 1u))) : nullptr;
}

void prepare_person(WorldRuntime& c, PersonRuntimeView& person) {
    const auto effective = person.state == PersonState::temporary_appearance ? person.saved_state : person.state;
    //60FF4A is JNZ: queued states14..17 are EXCLUDED, despite an old IDB
    //comment saying the opposite. The remaining three protected states also
    //retain their behavior. Repeated hover never overwrites the saved state23.
    if ((effective >= PersonState::wait_in_object_queue && effective <= PersonState::wait_for_object_script) || !record_passes_grid_predicates(c, &person.base) || person.state == PersonState::after_object_exit || person.state == PersonState::object_interaction || person.state == PersonState::first_aid_recovery) {
        return;
    }
    if (person.state != PersonState::return_to_roaming) {
        person.saved_state = person.state;
    }
    enter_world_person_state(c, person, PersonState::return_to_roaming);
    person.mobile.sprite_angle = 0;
}

bool in_same_map_chain(WorldRuntime& c, const MapObjectBase& candidate, const ThingHeader* retained) {
    for (const bool forward : { true, false }) {
        auto id = forward ? candidate.map_child : candidate.map_parent;
        std::size_t seen {};
        while (id) {
            if (++seen > c.entities.size()) {
                throw std::logic_error("Cyclic hover mobile chain");
            }
            const auto* p = c.entities.at(id).payload;
            if (!p) {
                throw std::logic_error("Hover mobile chain has an absent record");
            }
            if (p == retained) {
                return true;
            }
            const auto& base = *reinterpret_cast<const MapObjectBase*>(p);
            id = forward ? base.map_child : base.map_parent;
        }
    }
    return false;
}
} //namespace

struct WorldCursorActions::State {
    ParkRuntime& park;
    WorldRuntime& c;
    Action action {};
    StaffInteractionCursor cursor;
    Identity target, frozen_staff, context_press_target;
    std::uint32_t context_press_script {};
    std::uint64_t context_press_generation {};
    std::uint32_t mesh_slot {}, presentation {};
    std::u16string help;

    explicit State(ParkRuntime& p) : park(p), c(p.context()) {}

    ThingHeader* pressed_mobile() {
        auto* record = context_press_target.get<ThingHeader>(c.entities);
        const auto* source = find_resource_variant(c, context_press_script);
        return record && mobile_body_script(*record) == context_press_script && source && source->generation == context_press_generation ? record : nullptr;
    }

    void reset() {
        action = Action::none;
        target = {};
        cursor = {};
        mesh_slot = 0;
        presentation = 0;
        help.clear();
        park.meshes().hovered_slot = 0;
        park.meshes().hovered_generation = 0;
    }

    void publish() {
        c.staff.interaction.cursor_presentation = presentation;
        park.ui().set_world_cursor_help(help);
    }

    void select(Action next, const ThingHeader* p = nullptr) {
        action = next;
        target = EntityIdentity::capture(p);
    }

    void select(Action next, const ParkObjectRuntimeView* p) {
        select(next, p ? &p->base.header : nullptr);
    }

    void caption(std::uint32_t row, std::u16string_view name = {}, std::optional<std::int32_t> number = {}) {
        const std::array strings { BfstWideStringArgument { 0, name } };
        const std::array integers { BfstSignedIntegerArgument { 1, number.value_or(0) } };
        auto text = format_bfst_wide_string(c.messages.mail_language.ui_help, c.messages.mail_language.conversion, row, 16383, strings, number ? std::span<const BfstSignedIntegerArgument> { integers } : std::span<const BfstSignedIntegerArgument> {});
        if (!text) {
            throw std::runtime_error("World cursor help: " + text.error().reason);
        }
        help = std::move(*text);
    }

    void stars(double value) {
        if (!std::isfinite(value)) {
            throw std::domain_error("Nonfinite World cursor rating");
        }
        help.append(static_cast<std::size_t>(std::clamp(std::trunc(value), 1., 12.)), u'*');
    }

    void highlight() {
        if (const auto* mesh = park.meshes().instance(mesh_slot)) {
            park.meshes().hovered_slot = mesh_slot;
            park.meshes().hovered_generation = mesh->generation;
        }
    }

    void mobile(ThingHeader& h) {
        presentation = 2;
        if (h.kind == ThingKind::person) {
            auto& person = reinterpret_cast<PersonRuntimeView&>(h);
            select(Action::visitor, &h);
            if (person.arrival_index) {
                caption(156, {}, std::bit_cast<std::int32_t>(person.arrival_index));
            } else {
                caption(157);
            }
            stars(static_cast<std::uint8_t>(fixed::truncate_integer(person.happiness)) / 10u);
            prepare_person(c, person);
            return;
        }
        auto& staff = reinterpret_cast<StaffRuntimeRecord&>(h);
        select(
            match (h.kind) -> Action {
                case ThingKind::handyman    => Action::handyman;
                case ThingKind::mechanic    => Action::mechanic;
                case ThingKind::entertainer => Action::entertainer;
                case ThingKind::guard       => Action::guard;
                case ThingKind::gardener    => Action::gardener;
                case _                      => Action::researcher;
            },
            &h);
        if (is_world_staff_interaction_eligible(c, staff)) {
            auto* retained = frozen_staff.get<ThingHeader>(c.entities);
            if (!retained || (retained != &h && !in_same_map_chain(c, staff.base, retained))) {
                frozen_staff = EntityIdentity::capture(&h);
            }
            if (auto* record = frozen_staff.get<ThingHeader>(c.entities)) {
                enter_world_staff_state(c, *reinterpret_cast<StaffRuntimeRecord*>(record), StaffState::frozen);
            }
        }
        const std::u16string_view name { staff.name.data(), static_cast<std::size_t>(std::ranges::find(staff.name, char16_t {}) - staff.name.begin()) };
        caption(155, name);
        const auto max = c.staff.recruitment.balance.all.MaxExperience;
        stars(max ? static_cast<double>(staff.experience) / max * 5. : 1.);
    }

    bool gate(std::int32_t x, std::int32_t y) {
        if (c.mode.game == GameMode::code_3) {
            return false;
        }
        const auto& f = c.visitors.environment.FixedItemInfo;
        if ((y == f.EntranceAPosY && x >= f.EntranceAPosX - 2 && x <= f.EntranceAPosX + 1) || (y == f.EntranceBPosY && x >= f.EntranceBPosX - 1 && x <= f.EntranceBPosX + 2)) {
            select(Action::entrance, object(c, c.simulation.current->saved.park_gates));
            presentation = 2;
            caption(161);
            highlight();
            return true;
        }
        for (std::int32_t i = 0; i < f.NumTicketBooths; ++i) {
            const auto id = static_cast<std::uint32_t>(get_ticket_booth_cell(c.visitors.environment, static_cast<std::size_t>(i))) - 1u;
            const auto column = static_cast<std::int32_t>(id & 127u);
            if (y == static_cast<std::int32_t>(id >> 7) && x >= column - 1 && x <= column + 1) {
                select(Action::ticket_booth, object(c, c.simulation.current->saved.park_gates));
                presentation = 2;
                caption(161);
                return true;
            }
        }
        return false;
    }

    void hover_cell() {
        if (!cursor.cell || cursor.cell > map_cell_count || !c.simulation.current) {
            return;
        }
        const auto index = static_cast<std::uint32_t>(cursor.cell) - 1u;
        const auto x = static_cast<std::int32_t>(index & 127u);
        const auto y = static_cast<std::int32_t>(index >> 7);
        auto& cell = c.simulation.current->map_cells.at(index);
        auto* record = find_world_park_object_at_map_cell(c, x, y);
        const auto* d = record ? get_park_object_definition(c, record) : nullptr;
        if (gate(x, y)) {
            return;
        }
        if (cell.is_obstacle) {
            if (!record || !d || !d->balance) {
                return;
            }
            const auto destination = d->balance->Info.GateTo;
            if (destination >= 1 && destination <= 3) {
                auto& manager = get_or_create_theme_global_info_manager(c);
                const auto* info = get_or_create_theme_global_info_for_source(c, manager, destination == 2 ? "water" : "arabian");
                if (!info || !info->is_available) {
                    return;
                }
                select(Action::theme_gate, &record->base.header);
                caption(destination == 1 ? 164u : destination == 2 ? 166u : 165u);
            } else {
                select(Action::obstacle, &record->base.header);
                caption(record->base.header.id == c.simulation.current->saved.current_obstacle ? 144u : 360u);
            }
            presentation = 2;
            return;
        }
        if ((cell.base.common.flags & 64u) && !(cell.base.common.flags & 128u)) {
            presentation = 8;
            caption(358);
            return;
        }
        auto* track = &c.simulation.current->track_cells.at(index);
        if (track->base.common.type == 12) {
            const auto parent = track->base.common.parent_id;
            if (!parent || parent > map_cell_count) {
                return;
            }
            track = &c.simulation.current->track_cells.at(parent - 1u);
            if (track->base.common.type == 25) {
                return;
            }
        }
        const bool footprint = cell.base.common.type == 4 || cell.base.common.type == 9 || cell.base.common.type == 10;
        if (c.mode.game != GameMode::code_3 && (track->base.common.type == 11 || track->base.common.type == 16) && !(footprint && d && !static_cast<std::uint16_t>(d->add_on.upgrades_id))) {
            if (auto* ride = owner(c, track->base.common.parent_id)) {
                const auto endpoint = find_park_object_track_edit_endpoint(c, *ride);
                const auto slot = [&](CellId id) {
                    auto* part = &c.simulation.current->track_cells.at(id - 1u);
                    if (world_cell_is_track_child(&part->base)) {
                        part = &c.simulation.current->track_cells.at(part->base.common.parent_id - 1u);
                    }
                    return part->base.common.mesh_instance;
                };
                if (!endpoint || slot(cursor.cell) == slot(static_cast<CellId>(endpoint->x + endpoint->y * 128 + 1))) {
                    select(Action::track, &ride->base.header);
                    presentation = 6;
                    caption(151);
                    highlight();
                    return;
                }
            }
        }
        if (cell.litter_selector) {
            select(Action::litter);
            presentation = 2;
            caption(cell.litter_selector == 8 ? 158u : cell.litter_selector == 7 ? 159u : 160u);
            return;
        }
        (do {
            match (cell.base.common.type) {
                case 1 => {
                    if (c.mode.game != GameMode::code_3 && count_world_cell_neighbor_bits(c, &cell.base) == 1) {
                        const auto mask = get_world_cell_neighbor_mask(c, &cell.base);
                        const auto* neighbor = resolve_world_cell_from_selector_flag_byte(c, &cell.base, mask);
                        if (neighbor && neighbor->base.common.type == 10) {
                            if (auto* ride = owner(c, neighbor->base.common.parent_id)) {
                                select(Action::extend_exit_path, &ride->base.header);
                                c.view.secondary_cursor_record_snapshot = &ride->base.header;
                                presentation = 3;
                                caption(148);
                            }
                        }
                    }
                }
                case 3 => {
                    if (c.mode.game != GameMode::code_3) {
                        if (auto* ride = owner(c, cell.base.common.parent_id); ride && get_back_of_queue(c, ride) == cursor.cell) {
                            select(Action::extend_queue, &ride->base.header);
                            presentation = 4;
                            caption(149);
                        }
                    }
                }
                case 4 or 9 or 10 => {
                    if (!record || !d || (static_cast<std::uint16_t>(d->add_on.upgrades_id) && d->add_on.upgrade_type != 2)) {
                        do_return;
                    }
                    select(Action::object, &record->base.header);
                    presentation = 2;
                    if (d->which_ui_type == ObjectUiCategory::rides || d->which_ui_type == ObjectUiCategory::shops || d->which_ui_type == ObjectUiCategory::sideshows) {
                        if (!park_object_definition_is_marked_special(c, record) || is_special_ride_group_id_unregistered(c, record->base.header.id)) {
                            caption(143, park_object_ui_display_name(*record));
                            stars(std::bit_cast<std::uint32_t>(calculate_park_object_customer_satisfaction(*record)) / 10u);
                        } else {
                            caption(335, park_object_ui_display_name(*record));
                        }
                    } else {
                        const auto caps = std::to_underlying(record->capabilities);
                        caption(caps & 0x600u ? 235u : 234u, park_object_ui_display_name(*record));
                    }
                    highlight();
                }
                case 21 => {
                    auto* ride_object = owner(c, cell.base.common.parent_id);
                    if (!ride_object || (park_object_definition_is_marked_special(c, ride_object) && !is_special_ride_group_id_unregistered(c, ride_object->base.header.id))) {
                        do_return;
                    }
                    const auto* w = park.meshes().coasters();
                    const auto* ride = w ? w->find(ride_object->mesh_instance_id) : nullptr;
                    if (!ride || ride->geometry_state.empty()) {
                        do_return;
                    }
                    const auto flags = ride->train_state.placement_flags;
                    //503FD0 uses the registry CURRENT node, not the ray-hit support.
                    //Native473420 leaves the last created node current; gap insertion
                    //instead retains the node carrying both current-anchor flags.
                    std::optional<std::size_t> current;
                    if (c.construction.editor && c.construction.editor->active_map() == ride->map_id) {
                        current = c.construction.editor->active_node();
                    }
                    if (!current && (flags & 2u)) {
                        for (std::size_t i = 0; i < ride->geometry.size(); ++i) {
                            if ((ride->geometry[i].geometry_flags & 0x50000u) == 0x50000u) {
                                current = i;
                            }
                        }
                    }
                    if (!current) {
                        current = ride->geometry_state.size() - 1;
                    }
                    const auto& node = ride->geometry_state.at(*current);
                    const auto& placement = w->placements.at(node.placement);
                    if ((placement.flags & 16u) && !flags && (ride->geometry_state.size() <= 2 || node.placement != ride->geometry_state[2].placement)) {
                        do_return;
                    }
                    select(Action::coaster, &ride_object->base.header);
                    if ((placement.tile_x == x && placement.tile_y == y) || (flags & 1u)) {
                        presentation = 11;
                        caption(153);
                        highlight();
                    }
                }
                case _ => {
                }
            }
        });
    }
};

WorldCursorActions::WorldCursorActions(ParkRuntime& p) : state_(std::make_unique<State>(p)) {}

WorldCursorActions::~WorldCursorActions() = default;

void WorldCursorActions::clear() {
    end_context_press();
    state_->reset();
    state_->park.ui().set_world_cursor_help({});
}

bool WorldCursorActions::begin_context_press() {
    auto& s = *state_;
    end_context_press();
    if (s.action >= Action::visitor && s.action <= Action::researcher) {
        if (const auto* record = s.target.get<ThingHeader>(s.c.entities)) {
            if (const auto* source = find_resource_variant(s.c, mobile_body_script(*record))) {
                s.context_press_target = s.target;
                s.context_press_script = source->handle;
                s.context_press_generation = source->generation;
            }
        }
    }
    return s.context_press_target.id != 0;
}

void WorldCursorActions::end_context_press() {
    state_->context_press_target = {};
    state_->context_press_script = 0;
    state_->context_press_generation = 0;
}

void WorldCursorActions::hover(StaffInteractionCursor cursor, std::optional<WorldMobilePick> mobile, std::uint32_t mesh) {
    auto& s = *state_;
    s.reset();
    s.cursor = cursor;
    s.mesh_slot = mesh;
    if (s.c.view.flags & 0x16u) {
        s.publish();
        return;
    }
    if (s.context_press_target.id) {
        //Validate both record identity and the body's actual resource lease.
        //Reusing the same thing ID/address in the same simulation tick is safe.
        if (auto* pressed = s.pressed_mobile()) {
            s.mobile(*pressed);
        }
        s.publish();
        return;
    }
    if (mobile && s.c.mode.game != GameMode::code_3) {
        auto* h = mobile->thing < s.c.entities.size() ? s.c.entities[mobile->thing].payload : nullptr;
        const auto index = static_cast<std::uint32_t>(cursor.cell) - 1u;
        const bool mesh_owner = cursor.cell && mesh && find_world_park_object_at_map_cell(s.c, static_cast<std::int32_t>(index & 127u), static_cast<std::int32_t>(index >> 7));
        if (!mesh_owner && h && h == mobile->identity && !h->retirement_pending && (h->kind == ThingKind::person || is_staff_record_kind(h->kind))) {
            s.mobile(*h);
            s.publish();
            return;
        }
    }
    s.hover_cell();
    s.publish();
}

WorldCursorPressResult WorldCursorActions::press(WorldInputModifierMask modifiers) {
    auto& s = *state_;
    auto& c = s.c;
    auto& park = s.park;
    park.ui().close_context_menus();
    auto* h = s.target.get<ThingHeader>(c.entities);
    const auto id = h ? h->id : ThingId {};
    (do {
        match (s.action) {
            case Action::extend_exit_path or Action::extend_path => {
                park.authoring().begin_cursor_map_action(1, id);
                return WorldCursorPressResult::forward_map_action;
            }
            case Action::extend_queue => {
                if (id) {
                    park.authoring().begin_cursor_map_action(20, id);
                    return WorldCursorPressResult::forward_map_action;
                }
            }
            case Action::track => {
                if (id) {
                    auto* track = &c.simulation.current->track_cells.at(s.cursor.cell - 1u);
                    if (track->base.common.type == 12) {
                        track = &c.simulation.current->track_cells.at(track->base.common.parent_id - 1u);
                    }
                    const auto mode = track->base.common.type == 11 ? 14 : track->base.common.type == 16 ? 19 : track->base.common.type == 25 ? 26 : 0;
                    if (mode) {
                        park.authoring().begin_cursor_map_action(mode, id);
                        return WorldCursorPressResult::forward_map_action;
                    }
                }
            }
            case Action::coaster => {
                if (id) {
                    (void)park.ui().coaster_editor_controller().open_world_target(id, true);
                    world_operations::queue_scored_advisor_action(c, 242);
                    park.authoring().synchronize_from_world();
                }
                return WorldCursorPressResult::consumed;
            }
            case Action::object => {
                if (!h) {
                    do_return;
                }
                if ((std::to_underlying(modifiers) & 12u) != 0) {
                    auto& o = *reinterpret_cast<ParkObjectRuntimeView*>(h);
                    const auto* d = get_park_object_definition(c, &o);
                    const auto* control = world_operations::find_object_control(c, o.definition_id);
                    auto* bank_header = c.entities.at(c.simulation.current->saved.bank_account).payload;
                    if (!d || !control || !bank_header) {
                        throw std::logic_error("Control-copy requires a live quote and bank");
                    }
                    const auto caps = std::to_underlying(o.capabilities);
                    const auto balance = reinterpret_cast<BankRecord*>(bank_header)->balance;
                    if (park_object_definition_is_marked_special(c, &o) || (caps & 0x600u) || d->add_on.upgrade_type) {
                        (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 183, 0, 0, 0);
                    } else if (fixed::word(control->purchase_cost) > balance) {
                        (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 29, 0, 0, 0);
                    } else {
                        (void)park.authoring().buy_object(id);
                    }
                    //Native returns0: this press starts the copy tool, never places it.
                    return WorldCursorPressResult::consumed;
                }
                park.ui().object_controller().open_object(id);
            }
            case Action::visitor => {
                if (id) {
                    park.ui().visitor_controller().open_profile(id);
                }
            }
            case Action::handyman or Action::mechanic or Action::entertainer or Action::guard or Action::gardener or Action::researcher => {
                if (h && is_world_staff_interaction_eligible(c, *reinterpret_cast<StaffRuntimeRecord*>(h))) {
                    if (auto* frozen = s.frozen_staff.get<ThingHeader>(c.entities)) {
                        park.ui().staff_controller().open_details(frozen->id);
                    }
                }
            }
            case Action::entrance or Action::ticket_booth => park.ui().dispatch_command(WorldUiCommand::park_gates);
            case Action::obstacle                         => {
                (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 31, 0, 0, 0);
                park.ui().open_objectives();
            }
            case Action::theme_gate => {
                if (h) {
                    const auto& o = *reinterpret_cast<ParkObjectRuntimeView*>(h);
                    const auto* d = get_park_object_definition(c, &o);
                    auto& manager = get_or_create_theme_global_info_manager(c);
                    const auto gate = d->balance->Info.GateTo;
                    const auto* info = get_or_create_theme_global_info_for_source(c, manager, gate == 2 ? "water" : "arabian");
                    const auto mission = manager.mission_number;
                    if (info && info->is_available && ((gate == 1 && mission > 7) || (gate == 2 && mission > 2) || (gate == 3 && mission > 4))) {
                        c.ports.ui.request_resource_source_change_overlay(c, gate == 1 ? 1 : gate == 2 ? 2 : 0, 0);
                    } else if (o.definition_id == static_cast<std::uint16_t>(manager.mission_research.completion_obstacle_opened)) {
                        (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 31, 0, 0, 0);
                        park.ui().open_objectives();
                    }
                }
            }
            case _ => {
                //Litter33 and unknown/no-action codes have no left mutation.
            }
        }
    });
    return WorldCursorPressResult::forward_default;
}

void WorldCursorActions::context_menu(UiPoint point) {
    auto& s = *state_;
    if (s.c.view.flags & 0x16u) {
        return;
    }
    if (s.context_press_target.id && !s.pressed_mobile()) {
        return;
    }
    auto* h = s.target.get<ThingHeader>(s.c.entities);
    auto& objects = s.park.ui().object_controller();
    //505DF0's litter cell overrides every used-thing context, including a
    //mobile picked over that cell. Snapshot it before UI reset changes modes.
    if (s.cursor.cell && s.c.simulation.current->map_cells.at(s.cursor.cell - 1u).litter_selector) {
        objects.open_cell_context(s.cursor.cell, point);
        return;
    }
    match (s.action) {
        case Action::object => {
            if (h) {
                objects.open_world_context(h->id, point, 0, s.mesh_slot);
            }
        }
        case Action::coaster => {
            if (h) {
                objects.open_world_context(h->id, point, 6, s.park.meshes().hovered_slot);
            }
        }
        case Action::obstacle => {
            if (h) {
                objects.open_world_context(h->id, point, 16, s.mesh_slot);
            }
        }
        case Action::entrance or Action::ticket_booth => {
            if (h) {
                objects.open_world_context(h->id, point, s.action == Action::entrance ? 15 : 14);
            }
        }
        case Action::visitor => {
            if (h) {
                s.park.ui().visitor_controller().open_context(h->id, point);
            }
        }
        case Action::handyman or Action::mechanic or Action::entertainer or Action::guard or Action::gardener or Action::researcher => {
            if (h) {
                s.park.ui().staff_controller().open_context(h->id, point);
            }
        }
        case Action::track => {
            //Family5 passes its track predicate but has option_count0 in9BDC60.
            if (h) {
                mark_advisor_context_menu_used(s.c);
            }
            s.park.ui().close_context_menus();
        }
        case _ => s.park.ui().close_context_menus();
    }
}
} //namespace simcoaster
