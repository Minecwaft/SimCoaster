#include "simcoaster/schema_projection.hpp"
#include "application_frontend_internal.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace simcoaster {
ApplicationFrontendController::State::State(WorldRuntime& context,
                                            UiRuntime& runtime,
                                            UiControl* root,
                                            const std::filesystem::path& path,
                                            ApplicationFrontendHost callbacks,
                                            std::string locale)
    : c(context),
      ui(runtime),
      parent(root),
      game_root(path),
      host(std::move(callbacks)),
      language(std::move(locale)) {
    if (!ui.owns(parent)
        || !host.profiles
        || !host.activate_profile
        || !host.create_profile
        || !host.delete_profile
        || !host.selected_resource_source
        || !host.select_resource_source
        || !host.request_source
        || !host.open_load_game
        || !host.open_coaster_source_picker
        || !host.request_quit
        || !host.message
        || !host.command_families_enabled
        || !host.persistent_visible
        || !host.resume_ui_transition
        || !host.lobby_view
        || !host.lobby_sound
        || !host.cursor)
        throw std::invalid_argument("Original application frontend requires its complete session host");
}

ApplicationFrontendController::State::~State() {
    lifetime.reset();
    close();
}

UiControl& ApplicationFrontendController::State::required(UiControl* root, std::uint32_t id) {
    return bindings.required(root, id, "Original application template missing control ");
}

UiControl* ApplicationFrontendController::State::parse(std::span<const unsigned char> bytes, std::uint32_t id) {
    const auto previous = parent->children;
    const auto parsed = ui.parse_template(parent, std::as_bytes(bytes));
    if (!parsed)
        throw std::runtime_error(
            "Original application template word " + std::to_string(parsed.error().word) + ": " + parsed.error().reason);
    UiControl* result{};
    for (auto* child : parent->children)
        if (std::ranges::find(previous, child) == previous.end()) {
            owners.push_back(child);
            bind(*child, true);
            if (child->id == id)
                result = child;
        }
    if (!result)
        throw std::runtime_error("Original application template has no root " + std::to_string(id));
    return result;
}

void ApplicationFrontendController::State::bind(UiControl& control, bool is_root) {
    const auto widget = control.primary_callback;
    ui.set_primary_callback(control, [this, is_root, widget](UiControl& sender, const UiEvent& value) {
        if (is_root || (value.type >= 256 && value.type <= 2055))
            return event(sender, value);
        //542B90 wraps one button only.543100 restores Skip's stock
        //callback at the first speech and later wraps Play instead.
        if (phase == ApplicationFrontendPhase::intro && sender.id == intro_pulse_button) {
            if (value.type == 65537 && pulse_enabled) {
                pulse_enabled = false;
                pulse_phase = false;
                pulse_deadline = 0;
                ui.set_button_auxiliary_state(sender, false);
            } else if (value.type == 65538)
                pulse_enabled = true;
            else if (value.type == 30 && pulse_enabled) {
                const auto now = std::bit_cast<std::int32_t>(clock());
                if (now >= pulse_deadline) {
                    pulse_deadline = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(now) + 300u);
                    pulse_phase = !pulse_phase;
                    ui.set_button_auxiliary_state(sender, pulse_phase);
                }
            }
        }
        return widget ? widget(sender, value) : ui.secondary(sender, value);
    });
    for (auto* child : control.children)
        bind(*child);
}

void ApplicationFrontendController::State::remove(UiControl*& control) {
    auto* old = std::exchange(control, nullptr);
    const bool was_closing = std::exchange(closing, true);
    if (ui.owns(old))
        ui.destroy(old);
    std::erase(owners, old);
    closing = was_closing;
}

void ApplicationFrontendController::State::close() {
    if (closing)
        return;
    closing = true;
    const auto previous = std::move(owners);
    owners.clear();
    intro = profile = overview = nullptr;
    for (auto* control : previous)
        if (ui.owns(control))
            ui.destroy(control);
    ui.set_input_root(parent);
    phase = ApplicationFrontendPhase::none;
    leave_lobby();
    commands(true);
    closing = false;
}

std::intptr_t ApplicationFrontendController::State::event(UiControl& control, const UiEvent& value) {
    if (value.type == 20) {
        if (closing)
            return ui.secondary(control, value);
        if (&control == intro)
            intro = nullptr;
        if (&control == profile) {
            profile = nullptr;
            commands(true);
        }
        if (&control == overview) {
            overview = nullptr;
            map_view_active = false;
            leave_lobby();
            restore_park_ui();
        }
        std::erase(owners, &control);
        if (!intro && !profile && !overview)
            phase = ApplicationFrontendPhase::none;
        return ui.secondary(control, value);
    }
    if (value.type == 4 || value.type == 5) {
        if (&control == overview) {
            cancel_advisor(144);
            cancel_advisor(441);
            close();
            restore_park_ui();
            return 0;
        }
    }
    if (phase == ApplicationFrontendPhase::intro) {
        if (value.type == 30 && &control == intro)
            update_intro();
        else if (value.type == 256) {
            if (value.id == 111001)
                play_intro();
            else if (value.id == 111004)
                skip_intro();
        }
        return ui.secondary(control, value);
    }
    if (profile
        && (phase == ApplicationFrontendPhase::profiles || phase == ApplicationFrontendPhase::profile_transition)) {
        if (value.type == 257 && value.value == 1 && value.id >= 51005 && value.id <= 51008)
            select_profile(value.id - 51005);
        else if ((value.type == 256 && value.id == 51031) || value.type == 2050)
            create_profile();
        else if (value.type == 256)
            profile_action(value.id);
        else if (value.type == 2049) {
            const auto& edit = required(profile, 51032);
            auto folded = [](std::u16string text) {
                for (auto& ch : text)
                    if (ch >= u'A' && ch <= u'Z')
                        ch = static_cast<char16_t>(ch + u'a' - u'A');
                return text;
            };
            ui.set_enabled(required(profile, 51031), !edit.text.empty() && folded(edit.text) != folded(text(32)));
        }
        return ui.secondary(control, value);
    }
    if (overview) {
        if ((value.type == 256 || value.type == 65544 || value.type == 65545)) {
            if (value.id == 90002 && !hide_chooser_close) {
                close();
                restore_park_ui();
                return 0;
            }
            if (value.id == 90004)
                navigate(-1);
            else if (value.id == 90005)
                navigate(1);
            else if (value.id == 90011)
                select_mode(initial_mode);
        }
        if (value.type == 65542 && value.id == 0 && &control == overview)
            area_click = true;
    }
    return ui.secondary(control, value);
}

std::u16string ApplicationFrontendController::State::format(std::span<const std::byte> data,
                                                            std::uint32_t row,
                                                            std::span<const BfstWideStringArgument> strings,
                                                            std::span<const BfstSignedIntegerArgument> integers) {
    const auto result =
        format_bfst_wide_string(data, c.messages.mail_language.conversion, row, 16383, strings, integers);
    if (!result)
        throw std::runtime_error("Original application text " + std::to_string(row) + ": " + result.error().reason);
    return *result;
}

std::u16string ApplicationFrontendController::State::text(std::uint32_t row,
                                                          std::span<const BfstWideStringArgument> strings,
                                                          std::span<const BfstSignedIntegerArgument> integers) {
    return format(c.messages.mail_language.ui_text, row, strings, integers);
}

const std::vector<std::byte>& ApplicationFrontendController::State::table(std::string_view name) {
    if (const auto found = tables.find(name); found != tables.end())
        return found->second;
    const auto path = (game_root / "Data/Language" / language / name).native_encoded_string();
    const auto contents = c.ports.assets.read_balance_asset(c, path);
    if (!contents)
        throw std::runtime_error("Missing original application language table " + path);
    const auto bytes = std::as_bytes(std::span(contents->data(), contents->size()));
    return tables.emplace(std::string(name), std::vector<std::byte>(bytes.begin(), bytes.end())).first->second;
}

std::u16string ApplicationFrontendController::State::theme_name(int category) {
    constexpr std::array<std::uint32_t, 4> rows{ 3, 0, 1, 2 };
    if (category < 0 || category >= static_cast<int>(rows.size()))
        throw std::out_of_range("Original application theme category");
    return format(table("THEMENAMES.str"), rows[static_cast<std::size_t>(category)]);
}

void ApplicationFrontendController::State::caption(UiControl& control,
                                                   std::u16string_view value,
                                                   std::uint32_t font,
                                                   std::int32_t horizontal,
                                                   std::int32_t vertical) {
    control.secondary_asset_hash = 0;
    control.font = font;
    control.horizontal_alignment = horizontal;
    control.vertical_alignment = vertical;
    ui.set_text(control, value);
}

void ApplicationFrontendController::State::numeric(UiControl& control, std::int32_t value, UiScalarTextKind kind) {
    control.scalar_text = kind;
    control.value = value;
}

AdvisorRecord* ApplicationFrontendController::State::advisor() {
    if (!c.simulation.current || !c.simulation.current->saved.advisor)
        return nullptr;
    auto* header = c.entities.at(c.simulation.current->saved.advisor).payload;
    return header && header->kind == ThingKind::advisor ? reinterpret_cast<AdvisorRecord*>(header) : nullptr;
}

void ApplicationFrontendController::State::queue_advisor(std::int32_t action) {
    if (restoring)
        return;
    if (auto* record = advisor()) {
        auto candidate = world_operations::make_advisor_action_candidate(c, action);
        (void)world_operations::queue_advisor_action_candidate(c, record, candidate);
    }
}

void ApplicationFrontendController::State::cancel_advisor(std::int32_t action) {
    if (auto* record = advisor())
        world_operations::cancel_advisor_action_by_id(c, record, action);
}

void ApplicationFrontendController::State::commands(bool enabled) {
    commands_disabled = !enabled;
    host.command_families_enabled(enabled);
}

void ApplicationFrontendController::State::restore_park_ui() {
    //Native522280 restores the HUD before refreshing eligible Goal feedback.
    host.persistent_visible(true);
    update_objectives_panel_and_queue_mission_advisor_action(c);
}

std::uint32_t ApplicationFrontendController::State::clock() {
    return c.ports.clock.get_simulation_clock_time_ms(c);
}

ApplicationFrontendController::ApplicationFrontendController(WorldRuntime& c,
                                                             UiRuntime& ui,
                                                             UiControl* root,
                                                             const std::filesystem::path& path,
                                                             ApplicationFrontendHost host,
                                                             std::string language)
    : state_(std::make_unique<State>(c, ui, root, path, std::move(host), std::move(language))) {}

ApplicationFrontendController::~ApplicationFrontendController() = default;

void ApplicationFrontendController::open_intro(bool suppress) {
    state_->open_intro(suppress);
}

void ApplicationFrontendController::finish_intro(bool force) {
    state_->finish_intro(force);
}

void ApplicationFrontendController::open_profiles() {
    state_->open_profiles();
}

void ApplicationFrontendController::open_chooser(int source, bool hide_close) {
    state_->open_chooser(source, hide_close);
}

void ApplicationFrontendController::close() {
    state_->close();
}

ApplicationFrontendResumeState ApplicationFrontendController::capture_resume_state() const {
    auto& s = *state_;
    auto d = std::make_shared<ApplicationFrontendResumeState::Detail>();
    d->context = &s.c;
    //All matching values are captured; restoration below remains staged around
    //panel/lobby creation and intentionally excludes its resulting ownership.
    static_assert(schema::matching_fields<ApplicationFrontendResumeState::Detail, State>.size() == 28);
    schema::copy_matching(*d, s);
    if (s.intro) {
        constexpr std::array ids{ 111002u, 111001u, 111004u };
        for (std::size_t i = 0; i < ids.size(); ++i)
            d->intro_visible[i] = s.required(s.intro, ids[i]).visible();
    }
    if (s.profile)
        d->profile_edit = s.required(s.profile, 51032).text;
    return { s.phase, s.mode, s.hide_chooser_close, s.suppress_buttons, std::move(d) };
}

void ApplicationFrontendController::restore_resume_state(const ApplicationFrontendResumeState& saved) {
    auto& s = *state_;
    s.close();
    if (saved.phase == ApplicationFrontendPhase::none)
        return;
    if (!saved.detail || saved.detail->context != &s.c)
        throw std::invalid_argument("Frontend recovery belongs to another World");
    const auto& d = *saved.detail;
    s.restoring = true;

    struct RestoreFlag {
        bool& value;

        ~RestoreFlag() {
            value = false;
        }
    } restore{ s.restoring };

    s.mode = saved.selection_mode;
    s.selected_profile = d.selected_profile;
    s.suppress_template = d.suppress_template;
    s.angle = d.angle;
    match (saved.phase) {
        case ApplicationFrontendPhase::intro => {
            s.open_intro(saved.suppress_intro_buttons);
            if (d.lobby_bound)
                s.bind_lobby(d.lobby_intro);
        }
        case ApplicationFrontendPhase::profiles or ApplicationFrontendPhase::profile_transition => {
            s.open_profiles();
            s.ui.set_text(s.required(s.profile, 51032), d.profile_edit);
        }
        case ApplicationFrontendPhase::chooser or ApplicationFrontendPhase::area_transition => {
            s.open_chooser(4, saved.hide_close);
        }
        case ApplicationFrontendPhase::none => {}
        case _ => {}
    }
    s.skip_requested = d.skip_requested;
    s.final_speech = d.final_speech;
    s.pulse_enabled = d.pulse_enabled;
    s.pulse_phase = d.pulse_phase;
    s.map_view_active = d.map_view_active;
    s.suppress_template = d.suppress_template;
    s.speech_index = d.speech_index;
    s.speech_status = d.speech_status;
    s.pulse_deadline = d.pulse_deadline;
    s.intro_script_id = d.intro_script_id;
    s.intro_pulse_button = d.intro_pulse_button;
    s.initial_mode = d.initial_mode;
    s.selected_area = d.selected_area;
    s.previous_area = d.previous_area;
    s.metric_complete = d.metric_complete;
    s.fade_ready = d.fade_ready;
    s.angle_tick = d.angle_tick;
    s.fade_tick = d.fade_tick;
    s.angle = d.angle;
    s.spin_target = d.spin_target;
    s.spin_origin = d.spin_origin;
    s.dolly = d.dolly;
    s.height_offset = d.height_offset;
    s.directional = d.directional;
    s.ambient = d.ambient;
    s.select_mode(saved.selection_mode, false);
    if (s.intro) {
        constexpr std::array ids{ 111002u, 111001u, 111004u };
        for (std::size_t i = 0; i < ids.size(); ++i)
            s.ui.set_visible(s.required(s.intro, ids[i]), d.intro_visible[i]);
    }
    if (s.lobby_bound)
        s.publish_lobby_view();
}

void ApplicationFrontendController::advance() {
    auto& s = *state_;
    if (s.lobby_bound)
        s.update_lobby();
}

void ApplicationFrontendController::viewport(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > 16384 || height > 16384)
        throw std::invalid_argument("Application UI viewport");
    state_->pixel_width = width;
    state_->pixel_height = height;
}

bool ApplicationFrontendController::key(std::uint32_t key, bool down, std::uint32_t modifiers) {
    return state_->key(key, down, modifiers);
}

ApplicationFrontendPhase ApplicationFrontendController::phase() const {
    return state_->phase;
}

std::int32_t ApplicationFrontendController::selected_area() const {
    return state_->selected_area;
}

std::uint16_t ApplicationFrontendController::selection_mode() const {
    return state_->mode;
}

UiControl* ApplicationFrontendController::root() const {
    return state_->overview ? state_->overview : state_->profile ? state_->profile : state_->intro;
}

const std::vector<std::string>& ApplicationFrontendController::diagnostics() const {
    return state_->diagnostics;
}
} //namespace simcoaster
