#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_staff.hpp"
#include "world_ui_staff_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace simcoaster {
WorldStaffUiController::State::State(WorldRuntime& context_owner, UiRuntime& runtime, UiControl* parent, const std::filesystem::path& path, StaffUiPanelHost panel_host) : c(context_owner), game_root(path), ui(runtime), primary(parent), model(c, path), host(std::move(panel_host)) {
    if (!ui.owns(primary)) {
        throw std::invalid_argument("Staff UI requires its persistent primary root");
    }
    model.set_retirement_observer([this](ThingId id) {
        retire(id);
    });
}

WorldStaffUiController::State::~State() {
    close();
    model.set_retirement_observer({});
}

UiControl* WorldStaffUiController::State::find(std::uint32_t id) const {
    return ui.find_recursive(primary, id);
}

UiFontLibrary& WorldStaffUiController::State::fonts() {
    if (!local_fonts) {
        local_fonts = std::make_unique<UiFontLibrary>(game_root);
    }
    return *local_fonts;
}

void WorldStaffUiController::State::affixed_edit(UiControl& stepper, std::u16string_view affix, std::uint32_t font, std::size_t capacity, std::array<std::uint8_t, 4> color, bool append) {
    auto& edit = required(&stepper, 3);
    stepper.associated_edit = &edit;
    caption(edit, edit.text, font);
    edit.color = color;
    edit.text_affix = affix;
    edit.append_text_affix = append;
    edit.numeric_only = true;
    edit.text_limit = capacity ? capacity - 1 : 0;
    edit.edit_caret_asset_hash = ui_model_asset_hash("lolight");
    ui.set_value(stepper, stepper.value);
}

void WorldStaffUiController::State::button(UiControl& control, std::uint32_t row, std::uint32_t help) {
    caption(control, text(row), 6);
    control.help_index = help;
}

void WorldStaffUiController::State::meter(UiControl& control, std::string_view texture, std::int32_t inset) {
    control.meter_presentation = UiMeterPresentation { std::string(texture), { 0, inset } };
    ui.set_range(control, 0, 1024);
}

void WorldStaffUiController::State::preview(UiControl& control, ThingId id) {
    auto& provider = previews[&control];
    if (!provider) {
        provider = std::make_unique<UiStaffPreview>(c);
    }
    (void)provider->bind_staff(id);
}

void WorldStaffUiController::State::candidate_preview(UiControl& control, std::size_t slot) {
    auto& provider = previews[&control];
    if (!provider) {
        provider = std::make_unique<UiStaffPreview>(c);
    }
    (void)provider->bind_candidate(slot);
}

void WorldStaffUiController::State::object_preview(UiControl& control, ThingId id) {
    auto& provider = object_previews[&control];
    if (!provider) {
        provider = std::make_unique<UiObjectPreview>(c, game_root);
    }
    (void)provider->bind(id, 5);
}

void WorldStaffUiController::State::begin(Page next) {
    close();
    if (host.dismiss) {
        host.dismiss();
    }
    page = next;
    page_target = 0;
    next_refresh = ui.clock();
}

void WorldStaffUiController::State::stage_advisor(std::int32_t action) {
    //503AD0 stages a panel-owned delayed request. It does not immediately
    //score/queue a candidate, which is a separate Help-button operation.
    auto* world = c.simulation.current;
    if (!world) {
        return;
    }
    const auto id = world->saved.advisor;
    if (!id || id >= c.entities.size() || !c.entities[id].payload || c.entities[id].payload->kind != ThingKind::advisor) {
        throw std::logic_error("Staff panel Advisor owner is absent");
    }
    auto& advisor = *c.entities.payload<AdvisorRecord>(id);
    advisor.from_ui_panel = 1;
    advisor.delayed_message = action;
    advisor.delay_ticks = c.ports.clock.read_advisor_ui_delay_clock(c);
}

void WorldStaffUiController::State::close_auxiliary() {
    //Wage-result destruction can accept a demand or fire its employee. Keep
    //its model alive until event20 has finished, including nested retirement.
    if (message_wrapper && ui.owns(message_wrapper)) {
        ui.destroy(message_wrapper);
    }
    message_wrapper = confirmation = nullptr;
    message_accepted = {};
    for (auto** slot : { &context, &name_editor, &wage_dialog }) {
        if (*slot && ui.owns(*slot)) {
            ui.destroy(*slot);
        }
        *slot = nullptr;
    }
    auxiliary_staff = 0;
    auxiliary_identity = nullptr;
    current_context_family.reset();
}

void WorldStaffUiController::State::close() {
    if (closing) {
        return;
    }
    closing = true;
    const auto old_page = page;
    auto* old_root = root;
    close_auxiliary();
    close_selector();
    close_ability();
    model.close_all();
    const auto owners = page_roots;
    for (auto* control : owners) {
        if (ui.owns(control)) {
            ui.destroy(control);
        }
    }
    page_roots.clear();
    previews.clear();
    object_previews.clear();
    hire_candidate_identities.clear();
    root = selector = ability = nullptr;
    page = Page::none;
    page_target = 0;
    const auto action = [old_page] {
        match (old_page) {
            case Page::hire            => return 443;
            case Page::list            => return 234;
            case Page::details         => return 154;
            case Page::finance         => return 434;
            case Page::activity        => return 430;
            case Page::staff_room      => return 247;
            case Page::training        => return 158;
            case Page::staff_ability   => return 445;
            case Page::training_budget => return 151;
            case _                     => return -1;
        }
    }();
    if (action >= 0) {
        cancel_ui_advisor_action(c, action);
    }
    if (old_root && host.deactivate) {
        host.deactivate(old_root);
    }
    ui.set_input_root(nullptr);
    if (ui.owns(primary)) {
        ui.set_focus(primary);
    }
    closing = false;
}

void WorldStaffUiController::State::retire(ThingId id) {
    if (host.retiring) {
        host.retiring(id);
    }
    for (auto& [control, provider] : previews) {
        (void)control;
        provider->retire(id);
    }
    for (auto& [control, provider] : object_previews) {
        (void)control;
        provider->retire(id);
    }
    //The model reconciles before this observer. page_target deliberately
    //retains the old identity so a cleared model cannot conceal its owner.
    if (auxiliary_staff == id) {
        close_auxiliary();
    }
    if (page_target == id && page != Page::locator && !closing) {
        close();
        return;
    }
    if (confirmation && model.state().selector.selected == 0 && selector) {
        close_auxiliary();
    }
    if (!closing) {
        refresh_page();
    }
}

void WorldStaffUiController::State::refresh_page() {
    if (closing || refreshing) {
        return;
    }
    refreshing = true;
    try {
        match (page) {
            case Page::hire          => refresh_hire();
            case Page::list          => refresh_list();
            case Page::details       => refresh_details();
            case Page::locator       => refresh_locator();
            case Page::finance       => refresh_finance();
            case Page::activity      => refresh_activity();
            case Page::staff_room    => refresh_room();
            case Page::training      => refresh_training();
            case Page::patrol        => refresh_patrol();
            case Page::staff_ability => refresh_staff_ability();
            case _                   => {
            }
        } if (selector) {
            refresh_selector();
        }
        if (context) {
            refresh_context();
        }
        if (wage_dialog) {
            refresh_wage();
        }
        refreshing = false;
    } catch (...) {
        refreshing = false;
        throw;
    }
}

void WorldStaffUiController::State::refresh() {
    //These controllers consume frame/pool/pointer state. The remaining
    //pages own native2s/4s periodic model refreshes; their live cells must not
    //be rebuilt or sorted on each render frame.
    if (page == Page::hire || page == Page::locator || page == Page::patrol) {
        model.refresh();
    }
    const auto* area = std::get_if<StaffAreaSelectionInteraction>(&c.staff.interaction.handler.value);
    if (area && area->panel_open && page != Page::patrol) {
        open_patrol();
    }
    //Native hover/release callbacks update the caption/confirm control and
    //right-up destroys the modal directly. These are live interaction state,
    //not the slower staff-statistics refresh below.
    if (page == Page::patrol) {
        refresh_patrol();
    }
    if (ui.clock() >= next_refresh) {
        next_refresh = ui.clock() + std::chrono::milliseconds { 250 };
        refresh_page();
    }
    for (auto iterator = previews.begin(); iterator != previews.end();) {
        if (!ui.owns(iterator->first)) {
            iterator = previews.erase(iterator);
        } else {
            (void)iterator->second->refresh();
            ++iterator;
        }
    }
    for (auto iterator = object_previews.begin(); iterator != object_previews.end();) {
        if (!ui.owns(iterator->first)) {
            iterator = object_previews.erase(iterator);
        } else {
            iterator->second->advance();
            ++iterator;
        }
    }
}

std::intptr_t WorldStaffUiController::State::event(UiControl& sender, const UiEvent& event) {
    //Win32 supplies a virtual key; native panel callbacks receive its packed
    //command form. Editors normalize their own keys before notifications.
    if ((event.type == 65546 || event.type == 65547) && event.id > 0 && event.id <= 255) {
        auto packed = event;
        packed.id <<= 8;
        return this->event(sender, packed);
    }
    //507FA0 turns unmodified Escape KEYUP into the panel's close event5.
    //Child editors retain their own Enter/Escape handling before notifying us.
    if (&sender == root && event.type == 65547 && (event.id > 255 ? event.id >> 8 : event.id) == 27 && (static_cast<std::uint32_t>(event.value) & 0xFF0000u) == 0) {
        return ui.dispatch(root, 5);
    }
    if (event.type == 4 && &sender == root && !closing) {
        close();
        return 0;
    }
    if (event.type == 20 && &sender == root && !closing) {
        //An external owner may destroy a root directly. Join the same teardown
        //transaction; UiRuntime suppresses recursive notification of this root.
        close();
        return ui.secondary(sender, event);
    }
    match (page) {
        case Page::hire or Page::list or Page::locator => return roster_event(sender, event);
        case Page::details                             => return details_event(sender, event);
        case Page::finance                             => return finance_event(sender, event);
        case Page::activity                            => return activity_event(sender, event);
        case Page::staff_room                          => return room_event(sender, event);
        case Page::training                            => return training_event(sender, event);
        case Page::patrol                              => return patrol_event(sender, event);
        case Page::staff_ability                       => return staff_ability_event(sender, event);
        case _                                         => return ui.secondary(sender, event);
    }
}

void WorldStaffUiController::State::message(std::uint32_t row, std::function<void()> accepted) {
    message_text(text(row), std::move(accepted));
}

void WorldStaffUiController::State::message_text(std::u16string body, std::function<void()> accepted) {
    if (message_wrapper && ui.owns(message_wrapper)) {
        ui.destroy(message_wrapper);
    }
    message_previous_focus = ui.focus();
    message_scope.acquire(!c.simulation.current || c.simulation.current->saved.state != WorldState::transition);
    message_accepted = std::move(accepted);
    const bool question = static_cast<bool>(message_accepted);
    //4F4470: only generic messages own a full-canvas wrapper and lolight
    //backdrop. Staff context/name/wage panels parse directly under primary.
    message_wrapper = &create_ui_message_wrapper(ui);
    auto callback = [this](UiControl& sender, const UiEvent& event) -> std::intptr_t {
        if (event.type == 20 && &sender == confirmation) {
            confirmation = nullptr;
            message_wrapper = nullptr;
            message_accepted = {};
            message_scope.release();
            if (!closing) {
                ui.set_input_root(context ? context : name_editor ? name_editor : wage_dialog ? wage_dialog : nullptr);
                ui.set_focus(ui.owns(message_previous_focus) ? message_previous_focus : primary);
            }
            return ui.secondary(sender, event);
        }
        const bool choice = event.type == 256 && (event.id == 60001 || event.id == 60002 || event.id == 60003);
        if (choice || event.type == 4 || event.type == 5) {
            auto action = choice && event.id == 60002 ? std::move(message_accepted) : std::function<void()> {};
            auto* wrapper = message_wrapper;
            if (ui.owns(wrapper)) {
                ui.destroy(wrapper);
            }
            if (action) {
                action();
            }
            return 0;
        }
        return ui.secondary(sender, event);
    };
    const auto parsed = ui.parse_template(message_wrapper, std::as_bytes(ui_message_template()));
    if (!parsed) {
        throw std::runtime_error("Original message template: " + parsed.error().reason);
    }
    confirmation = ui.find(message_wrapper, 60000);
    if (!confirmation) {
        throw std::runtime_error("Original message template has no root60000");
    }
    bind(*confirmation, callback);
    ui.set_primary_callback(*message_wrapper, [this](auto& sender, const auto& event) {
        if (event.type == 4) {
            ui.destroy(&sender);
            return std::intptr_t {};
        }
        return ui.secondary(sender, event);
    });
    message_controls = bind_ui_message_controls(bindings, confirmation, "Original Staff template missing control ");
    present_message<false>(message_wrapper, confirmation, message_controls, std::nullopt, body, question, UiMessageButtonKind::cancel);
}

WorldStaffUiController::WorldStaffUiController(WorldRuntime& c, UiRuntime& ui, UiControl* primary, const std::filesystem::path& root, StaffUiPanelHost host) : state_(std::make_unique<State>(c, ui, primary, root, std::move(host))) {}

WorldStaffUiController::~WorldStaffUiController() = default;

bool WorldStaffUiController::command(WorldUiCommand command) {
    match (command) {
        case WorldUiCommand::staff_hire => {
            state_->open_hire();
            return true;
        }
        case WorldUiCommand::staff_list => {
            state_->open_list();
            return true;
        }
        case WorldUiCommand::staff_locator => {
            state_->open_locator();
            return true;
        }
        case WorldUiCommand::staff_finance => {
            state_->open_finance();
            return true;
        }
        case WorldUiCommand::training_room => {
            //54F4F0 chooses the first Training Room in the actual park-object chain.
            //The independent589280 budget dialog has no inbound native call here.
            if (auto* world = state_->c.referenced_world()) {
                for (auto id = world->saved.first_park_object; id;) {
                    const auto& object = *state_->c.entities.at_payload<const ParkObjectRuntimeView>(id);
                    if ((std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::training_room)) != 0) {
                        state_->open_training(id);
                        break;
                    }
                    id = object.next_park_object;
                }
            }
            return true;
        }
        case _ => return false;
    }
}

bool WorldStaffUiController::open() const {
    return state_->page != State::Page::none || state_->context || state_->name_editor || state_->wage_dialog || state_->confirmation;
}

void WorldStaffUiController::close() {
    state_->close();
}

void WorldStaffUiController::close_context() {
    auto& s = *state_;
    if (s.context && s.ui.owns(s.context)) {
        s.ui.dispatch(s.context, 4);
    }
}

void WorldStaffUiController::advance() {
    state_->refresh();
}

void WorldStaffUiController::open_details(std::uint16_t id) {
    state_->open_details(id);
}

void WorldStaffUiController::open_hire(StaffType role) {
    state_->open_hire(role);
}

void WorldStaffUiController::open_locator(ThingId id) {
    state_->open_locator(id);
}

void WorldStaffUiController::open_room(std::uint16_t id) {
    state_->open_room(id);
}

void WorldStaffUiController::open_training(std::uint16_t id) {
    state_->open_training(id);
}

void WorldStaffUiController::open_training_budget() {
    state_->open_training_budget();
}

void WorldStaffUiController::open_context(std::uint16_t id, UiPoint pointer) {
    state_->open_context(id, pointer);
}

void WorldStaffUiController::open_wage(std::uint16_t id, std::int32_t increment) {
    state_->model.begin_wage(id, increment);
    state_->open_wage(id);
}

void WorldStaffUiController::preferences(const UiPreferences& options) {
    state_->confirm_deletions = options.confirm_delete_on != 0;
    state_->animate_menus = options.animate_menus != 0;
}

ThingId WorldStaffUiController::locator_target() const {
    return state_->model.state().locator.open ? state_->model.state().locator.target : 0;
}

StaffAreaBounds WorldStaffUiController::locator_area() const {
    return state_->model.state().locator.area;
}

bool WorldStaffUiController::dismiss_outside(UiPoint point) {
    auto& s = *state_;
    if (!s.context || !s.ui.owns(s.context) || s.context->rect.contains(point)) {
        return false;
    }
    s.ui.dispatch(s.context, 4);
    return true;
}

bool WorldStaffUiController::simulation_paused() const {
    return state_->message_scope.advisor_paused();
}

void WorldStaffUiController::append(UiDrawList& draw, UiControl& control, UiRect clip, std::uint32_t width, std::uint32_t height) {
    auto& s = *state_;
    s.pixel_width = width;
    s.pixel_height = height;
    if (const auto found = s.previews.find(&control); found != s.previews.end()) {
        UiStaffPreviewPlacement placement;
        const auto r = control.rect;
        placement.bounds = { r.left, r.top, r.right, r.bottom };
        placement.clip = { static_cast<float>(clip.left), static_cast<float>(clip.top), static_cast<float>(clip.right), static_cast<float>(clip.bottom) };
        placement.depth = static_cast<std::int16_t>(control.primary_placement.depth + 1);
        (void)found->second->append(draw, placement);
    }
    if (const auto found = s.object_previews.find(&control); found != s.object_previews.end()) {
        found->second->append(draw, control, clip, width, height);
    }
}
} //namespace simcoaster
