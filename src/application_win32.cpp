#include "application_host_win32.hpp"
#include "simcoaster/require_bound.hpp"
#include "simcoaster/text_format.hpp"
#include <iterator>
#include <format>

namespace simcoaster::win32_host {
ApplicationUiError application_error(const ApplicationSessionError& error) {
    auto body = postcard_from_ansi(error.detail);
    if (!error.path.empty()) {
        body += u"\n" + error.path.u16string();
    }
    return { u"SimCoaster", std::move(body) };
}

ParkRuntime& ApplicationWindow::park() {
    return require_bound(window.park, "Application has no bound World view");
}

void ApplicationWindow::require_bound_views() const {
    if (!view_recovery_failure.empty()) {
        throw std::runtime_error(std::format("Cannot recover the previous application view: {}", view_recovery_failure));
    }
}

WorldRuntime& ApplicationWindow::context() {
    return park().context();
}

bool ApplicationWindow::paused() const {
    const auto* world = window.park ? window.park->context().simulation.current : nullptr;
    const bool ui_hold = (!world || world->saved.state != WorldState::transition) && std::ranges::any_of(pauses, [](unsigned value) {
                             return value != 0;
                         });
    return !active || window.paused || ui_hold;
}

void ApplicationWindow::pause(std::size_t owner, bool hold) {
    const auto held = [this] {
        return std::ranges::any_of(pauses, [](unsigned value) {
            return value != 0;
        });
    };
    const bool previously_held = held();
    auto& depth = pauses.at(owner);
    if (hold) {
        ++depth;
    } else if (depth) {
        --depth;
    }
    if (!session || !session->park()) {
        return;
    }
    if (hold && !previously_held) {
        cancel_world_pointer_input();
    }
    //42A170 never acquires a gameplay-clock pause in the failure state4.
    const bool now_held = held() && context().simulation.current && context().simulation.current->saved.state != WorldState::transition;
    park().set_application_ui_paused(now_held);
    if (!previously_held && now_held && !context().advisor.speech.speech_request_paused_for_ui) {
        pause_advisor_speech_for_ui(context());
    } else if (!held()) {
        resume_transition();
    }
}

bool ApplicationWindow::frontend_active() const {
    return frontend && frontend->phase() != ApplicationFrontendPhase::none;
}

void ApplicationWindow::persistent(bool visible) {
    if (!session || !session->park()) {
        return;
    }
    if (visible) {
        park().ui().restore_persistent();
    } else {
        park().ui().hide_persistent();
    }
}

void ApplicationWindow::sync_canvas() {
    if (!canvas || !session->park()) {
        return;
    }
    auto& ui = park().ui().controls();
    const bool visible = frontend_active() || (game_ui && game_ui->is_open()) || (coaster_sources && coaster_sources->is_open()) || (postcard_ui && postcard_ui->is_open()) || (tips && tips->is_open()) || (debug_selector && debug_selector->is_open()) || (ending && ending->root());
    if (ui.owns(canvas)) {
        ui.set_visible(*canvas, visible);
    }
    if (!visible && ui.input_root() == canvas) {
        ui.set_input_root(park().ui().secondary_root()->visible() ? park().ui().secondary_root() : park().ui().primary_root());
    }
    synchronize_world_input();
}

void ApplicationWindow::command_families(bool enabled) {
    auto& c = context();
    c.ports.ui.restore_command_families_state(c, !enabled);
    window.ui_commands_enabled = park().ui_commands_enabled();
    if (!enabled && shortcuts) {
        shortcuts->clear_active();
    }
    synchronize_world_input();
}

void ApplicationWindow::resume_transition() {
    if (!session || !session->park()) {
        return;
    }
    const bool held = std::ranges::any_of(pauses,
                                          [](unsigned value) {
                                              return value != 0;
                                          })
                   && context().simulation.current
                   && context().simulation.current->saved.state != WorldState::transition;
    park().set_application_ui_paused(held);
    //42A200 resumes Advisor/audio and BOTH gameplay clock domains. Each
    //owner releases its own pause; other UI owners and user pause survive.
    //Audio prepare_frame applies native subsound listener override0 when
    //the final pause is released. It never hides either persistent root.
    if (!held && !park().ui().simulation_paused()) {
        resume_advisor_speech_after_ui(context());
    }
}

WorldUiPanelHost ApplicationWindow::panels() {
    auto host = park().ui().panel_host();
    const auto ordinary_dismiss = host.dismiss;
    host.dismiss = [this, ordinary_dismiss] {
        if (ordinary_dismiss) {
            ordinary_dismiss();
        }
        if (park_map) {
            park_map->close();
        }
    };
    const auto ordinary_focus = host.synchronize_focus;
    host.synchronize_focus = [this, ordinary_focus] {
        auto& ui = park().ui().controls();
        auto* owner = frontend_active() ? frontend->root() : nullptr;
        if (ui.owns(owner) && owner->visible()) {
            ui.set_focus(owner);
        } else if (ordinary_focus) {
            ordinary_focus();
        }
    };
    return host;
}

void ApplicationWindow::report(const PostcardError& error) {
    if (game_ui) {
        game_ui->report_error({ u"SimCoaster", postcard_from_ansi(error.reason) + u"\n" + error.path.u16string() });
    }
}

bool ApplicationWindow::result(std::expected<void, ApplicationSessionError> value) {
    if (value) {
        return true;
    }
    if (game_ui) {
        game_ui->report_error(application_error(value.error()));
    }
    return false;
}

void ApplicationWindow::inspect(ThingId id) {
    auto& c = context();
    const auto* record = id && id < c.entities.size() ? c.entities[id].payload : nullptr;
    if (!record || record->retirement_pending) {
        return;
    }
    if (record->kind == ThingKind::person) {
        park().ui().visitor_controller().open_profile(id);
    } else if (is_staff_record_kind(record->kind)) {
        park().ui().staff_controller().open_details(id);
    } else if (record->kind == ThingKind::park_object) {
        park().ui().object_controller().open_object(id);
    }
}

void ApplicationWindow::close_auxiliary(bool include_frontend) {
    if (debug_selector) {
        debug_selector->close();
    }
    if (postcard_ui) {
        postcard_ui->close();
    }
    if (coaster_sources) {
        coaster_sources->close();
    }
    if (park_map) {
        park_map->close();
    }
    if (include_frontend && frontend) {
        frontend->close();
    }
}

void ApplicationWindow::open_coaster_source(bool from_profile) {
    close_auxiliary();
    park().ui().close_panel();
    coaster_sources->open(0, from_profile ? 0 : 1);
    sync_canvas();
}

void ApplicationWindow::bind_management() {
    WorldManagementUiHost host;
    host.panels = panels();
    host.language = "American";
    host.read_options = [this] {
        return park().preferences();
    };
    host.write_options = [this](const UiPreferences& options, ManagementOptionsChange change) {
        //The dialog owns its pause. Audio/control previews never reset the
        //simulation rate while it is paused; commit publishes the actual rate.
        park().apply_preferences(options, change == ManagementOptionsChange::commit);
        movie.set_volume(application_movie_volume(options.movie_volume));
        if (change == ManagementOptionsChange::commit) {
            configuration.movie_volume = options.movie_volume;
            configuration.sound_quality = options.sound_quality;
        }
        if (park_map) {
            park_map->preferences(options);
        }
    };
    host.read_graphics = [this] {
        return ManagementGraphicsPreferences { configuration.renderer_backend != 0, static_cast<std::int32_t>(configuration.hardware_profile), static_cast<std::int32_t>(configuration.resolution), static_cast<std::int32_t>(configuration.video_device) };
    };
    host.graphics_capabilities = [] {
        return ManagementGraphicsCapabilities { true, MeshRenderer::hardware_adapters() };
    };
    host.write_graphics = [this](const ManagementGraphicsPreferences& value, bool commit) {
        (void)application_resolution(static_cast<std::uint32_t>(value.resolution));
        const auto profile = load_application_hardware_profile(root, static_cast<std::uint32_t>(value.quality));
        if (!profile) {
            throw std::runtime_error(profile.error().reason);
        }
        if (value.hardware && (value.video_device < 0 || static_cast<std::size_t>(value.video_device) >= MeshRenderer::hardware_adapters().size())) {
            throw std::runtime_error("Selected graphics device is unavailable");
        }
        if (commit) {
            pending_graphics = value;
        }
    };
    host.pause_options = [this](bool hold) {
        pause(1, hold);
    };
    park().ui().bind_management(std::move(host));
}

void ApplicationWindow::bind_game_ui() {
    ApplicationGameUiHost host;
    host.panels = panels();
    host.language = "American";
    host.status = [this] {
        return ApplicationMenuStatus { false, false, !paused() && !park().ui().simulation_paused(), static_cast<std::uint32_t>(postcards.index().records.size()) };
    };
    host.viewport = [this] {
        return std::array { window.width, window.height };
    };
    host.list_files = [this](ApplicationFileKind kind) -> std::expected<std::vector<ApplicationFileEntry>, ApplicationUiError> {
        auto listed = session->list_files(kind);
        if (!listed) {
            return std::unexpected(application_error(listed.error()));
        }
        return std::move(*listed);
    };
    host.pause = [this](bool hold) {
        pause(0, hold);
    };
    host.prepare_menu = [this](bool designer) {
        close_auxiliary();
        if (!designer && context().construction.editor) {
            context().construction.editor->cancel(context(), park().construction());
            context().construction.editor->activate(context(), 0, false);
        }
    };
    host.open_options = [this] {
        park().ui().management_controller().open_options();
    };
    host.play_ui_sound = [this](std::uint32_t action) {
        auto& c = context();
        (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, action, 0, 500, 0);
    };
    game_ui = std::make_unique<ApplicationGameUiController>(context(), park().ui().controls(), canvas, root, std::move(host));
}

void ApplicationWindow::bind_frontend() {
    ApplicationFrontendHost host;
    host.profiles = [this] {
        return session->profiles().views();
    };
    host.activate_profile = [this](std::uint32_t slot) {
        return result(session->activate_profile(slot));
    };
    host.create_profile = [this](std::uint32_t slot, std::u16string_view name) {
        return result(session->create_profile(slot, name));
    };
    host.delete_profile = [this](std::uint32_t slot, std::uint64_t generation) {
        return result(session->erase_profile(slot, generation));
    };
    host.selected_resource_source = [this] {
        return session->selected_source();
    };
    host.select_resource_source = [this](int source) {
        session->select_source(source);
    };
    host.request_source = [this](int source, bool from_profile) {
        session->request_source(source, from_profile);
    };
    host.open_load_game = [this] {
        game_ui->open_load(true);
        sync_canvas();
    };
    host.open_coaster_source_picker = [this] {
        open_coaster_source(true);
    };
    //The Profile controller already obtained native UITEXT773 confirmation.
    host.request_quit = [this] {
        session->request_quit();
    };
    host.message = [this](std::u16string body, bool question, std::function<void()> accepted) {
        game_ui->show_message(std::move(body), question, std::move(accepted));
        sync_canvas();
    };
    host.command_families_enabled = [this](bool enabled) {
        command_families(enabled);
    };
    host.persistent_visible = [this](bool visible) {
        persistent(visible);
    };
    host.resume_ui_transition = [this] {
        resume_transition();
    };
    host.lobby_view = [this](std::optional<ApplicationLobbyView> view) {
        if (!view) {
            window.frontend_cursor.reset();
        }
        park().set_lobby_view(view ? &*view : nullptr);
        window.camera = park().render_camera();
        return window.camera;
    };
    host.lobby_sound = [this](bool entering, bool intro) {
        park().set_lobby_audio(entering, intro);
    };
    host.cursor = [this](std::int32_t value) {
        if (value < 0 || static_cast<std::uint32_t>(value) >= UiCursorWin32::size()) {
            throw std::out_of_range("Unknown original frontend cursor");
        }
        auto* owner = frontend->root();
        window.frontend_cursor = WindowCursorOverride { owner, park().ui().controls().lifetime_serial(owner), static_cast<UiCursorId>(value) };
        if (active && window.cursor_assets && client_owns_pointer(handle)) {
            install_ui_cursor(window);
        }
    };
    frontend = std::make_unique<ApplicationFrontendController>(context(), park().ui().controls(), canvas, root, std::move(host));
    frontend->viewport(window.width, window.height);
}

void ApplicationWindow::bind_map() {
    WorldParkMapUiHost host;
    host.panels = panels();
    host.map_mode = [this](bool enabled) {
        map_active = enabled;
        pause(2, enabled);
        persistent(!enabled);
    };
    host.focus_world = [this](float x, float z) {
        set_world_view_camera_focus(context(), { x, z });
        apply_ui_camera(park(), window);
    };
    host.inspect = [this](ThingId id) {
        inspect(id);
    };
    host.tooltip = [this](std::u16string_view text) {
        park().ui().set_tooltip_override(text.empty() ? std::nullopt : std::optional<std::u16string> { text });
    };
    host.tooltip_offsets = [this](std::optional<UiPoint> offsets) {
        park().ui().set_tooltip_offsets(offsets);
    };
    park_map = std::make_unique<WorldParkMapUiController>(context(), park().ui().controls(), root, std::move(host));
    park_map->preferences(park().preferences());
}

void ApplicationWindow::bind_postcards() {
    ApplicationPostcardUiHost host;
    host.panels = panels();
    host.viewport = [this] {
        return std::array { window.width, window.height };
    };
    host.capture_scene = [this]() -> std::expected<TextureImage, PostcardError> {
        try {
            if (!window.width || !window.height) {
                return std::unexpected(PostcardError { {}, "The park window is minimized" });
            }
            resize_renderer();
            //Capture the actual secondary World surface before the UI owner
            //restores special modes or constructs its editor/overlay controls.
            park().build_scene(scene, window.camera, float(window.width) / float(window.height));
            renderer->draw(scene, window.camera);
            return renderer->capture();
        } catch (const std::exception& error) {
            return std::unexpected(PostcardError { {}, error.what() });
        }
    };
    host.composition_transition = [this](bool pending) {
        auto& ui = park().ui().controls();
        if (pending) {
            postcard_previous_presentation = ui.presentation_visible();
        }
        ui.presentation_visible(pending ? false : postcard_previous_presentation);
        window.ui_transition_cursor_hidden = pending;
        if (window.cursor_assets && active && client_owns_pointer(handle)) {
            window.cursor_assets->set_visible(!pending && !window.camera_mouse_look);
        }
    };
    host.restore_after_capture = [this] {
        context().ports.camera.restore_world_view_from_special_modes(context());
        if (!park().ui().primary_root()->visible()) {
            park().ui().restore_persistent();
        }
        apply_ui_camera(park(), window);
    };
    host.online_status = [] {
        return PostcardOnlineStatus {};
    };
    host.go_online = [this] {
        game_ui->report_error({ u"SimCoaster", u"The original online service is unavailable." });
    };
    host.send = [this](std::uint64_t serial, std::span<const PostcardRecord>) {
        //No compatible connected transport exists. Failure retains every
        //outbox entry and routes the original sender's error, never delivery.
        pending_send_failures.push_back(serial);
    };
    host.cancel_send = [this](std::uint64_t serial) {
        std::erase(pending_send_failures, serial);
    };
    host.pause = [this](bool hold) {
        pause(3, hold);
    };
    host.report_error = [this](const PostcardError& error) {
        report(error);
    };
    host.status_message = [this](std::uint32_t row) {
        auto bytes = load_application_error_text(row);
        game_ui->show_message(std::move(bytes), false);
    };
    postcard_ui = std::make_unique<ApplicationPostcardUiController>(context(), park().ui().controls(), canvas, root, postcards, std::move(host));
    park().ui().bind_frame_poll([this] {
        if (postcard_ui) {
            postcard_ui->poll_composition();
        }
        if (game_ui) {
            if (auto failure = take_mission_transition_failure(context())) {
                game_ui->report_error({ u"SimCoaster", postcard_from_ansi(failure->reason) + u"\n" + postcard_from_ansi(failure->resource) });
            }
        }
    });
}

std::u16string ApplicationWindow::load_application_error_text(std::uint32_t row) {
    const auto path = root / "Data/Language/American/ERRORMSG.str";
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Cannot open original ERRORMSG.str");
    }
    const auto size = stream.tellg();
    if (size < 0) {
        throw std::runtime_error("Cannot size original ERRORMSG.str");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("Cannot read original ERRORMSG.str");
    }
    const auto text = format_bfst_wide_string(bytes, context().messages.mail_language.conversion, row, 16383, {}, {});
    if (!text) {
        throw std::runtime_error(text.error().reason);
    }
    return *text;
}

void ApplicationWindow::bind_coaster_source() {
    ApplicationCoasterSourceHost host;
    host.panels = panels();
    host.selected_source = [this] {
        return session->selected_source();
    };
    host.request_source = [this](ApplicationCoasterSourceSelection value) {
        session->request_coaster_source(std::move(value));
    };
    host.begin_same_source = [this](const ApplicationCoasterSourceSelection& value) {
        auto result = session->begin_same_source(value);
        if (!result) {
            game_ui->report_error(application_error(result.error()));
            return false;
        }
        return *result;
    };
    host.pause = [this](bool hold) {
        pause(4, hold);
    };
    host.gameplay_running = [this] {
        return context().simulation.current && context().simulation.current->saved.state != WorldState::transition && !paused() && !park().ui().simulation_paused();
    };
    host.resume_ui_transition = [this] {
        resume_transition();
    };
    host.map_source_recreation_flag = [this] {
        return context().mode.source_recreation;
    };
    host.cancel_profile_route = [this] {
        if (result(session->cancel_coaster_source_profile_route())) {
            frontend->open_profiles();
        }
        sync_canvas();
    };
    coaster_sources = std::make_unique<ApplicationCoasterSourceController>(context(), park().ui().controls(), canvas, root, std::move(host));
    if (const auto* choice = session->current_coaster_source()) {
        coaster_sources->restore_return_policy(choice->remember_current_source, choice->previous_source_index);
    }
}

void ApplicationWindow::bind_ending() {
    ApplicationEndingUiHost host;
    host.panels = panels();
    host.viewport = [this] {
        return std::array { window.width, window.height };
    };
    host.command_families_enabled = [this](bool enabled) {
        command_families(enabled);
    };
    host.prepare_scene = [this] {
        close_auxiliary(true);
        game_ui->close();
        park().ui().close_panel();
        (void)authoring->select_tool(ParkAuthoringTool::inspect);
        //Destruction of the previous Profile/name/message owners restores
        //their commands. The ending scene owns the final disabled state.
        command_families(false);
    };
    host.persistent_visibility = [this](bool primary, bool secondary) {
        auto& ui = park().ui().controls();
        ui.set_visible(*park().ui().primary_root(), primary);
        ui.set_visible(*park().ui().secondary_root(), secondary);
    };
    host.right_mouse_cancel_enabled = [this] {
        return park().preferences().rmb_cancel_on != 0;
    };
    host.failure_audio = [this] {
        park().begin_failure_audio_transition();
    };
    host.mail_action = [this](const WorldMailUiAction& action) {
        park().ui().dispatch_mail_action(action);
    };
    ending = std::make_unique<ApplicationEndingUiController>(context(), park().ui().controls(), canvas, root, std::move(host));
}

void ApplicationWindow::bind_tips() {
    auto* owner = session.get();
    ApplicationTipUiHost host;
    host.panels = panels();
    host.history = [owner]() -> CampaignTipHistory& {
        return owner->tip_history();
    };
    host.enabled = [this] {
        return park().preferences().totd != 0;
    };
    host.set_enabled = [this](bool enabled) {
        auto options = park().preferences();
        options.totd = enabled ? 1 : 0;
        park().apply_preferences(options, false);
    };
    host.pause = [this](bool hold) {
        pause(5, hold);
    };
    host.command_families_enabled = [this](bool enabled) {
        command_families(enabled);
    };
    host.resume_ui_transition = [this] {
        resume_transition();
    };
    tips = std::make_unique<ApplicationTipUiController>(context(), park().ui().controls(), canvas, root, std::move(host));
    ApplicationDebugSelectorHost developer;
    developer.panels = panels();
    developer.persistent = &debug_state;
    developer.inspection_parent = park().ui().primary_root();
    developer.execute = [this](ApplicationDebugCommand command) -> std::expected<void, std::string> {
        using C = ApplicationDebugCommand;
        try {
            if (!execute_application_debug_command(context(), debug_state, command)) {
                match (command) {
                    case C::raise_light or C::lower_light => park().set_debug_lighting(command == C::raise_light);
                    case C::music_up or C::music_down     => {
                        debug_state.music_level = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(debug_state.music_level) + (command == C::music_up ? 1u : 4294967295u));
                        park().set_debug_music_level(debug_state.music_level);
                    }
                    case C::open_research   => park().ui().object_controller().open_research();
                    case C::assign_research => park().ui().object_controller().open_research_assignment();
                    case C::outbox          => postcard_ui->open_outbox();
                    case C::dialup          => {
                        game_ui->report_error({ u"SimCoaster", u"The original online service is unavailable." });
                    }
                    case C::tip       => (void)tips->show(true);
                    case C::focus_atm => {
                        //Native5384B6 ignores the ATM lookup and focuses constant
                        //ThingId104.
                        auto& c = context();
                        auto* value = c.entities.size() > 104 ? c.entities[104].payload : nullptr;
                        if (!value || value->retirement_pending) {
                            throw std::runtime_error("Developer camera target104 is missing");
                        }
                        auto& target = *reinterpret_cast<MapObjectBase*>(value);
                        set_world_view_camera_focus(c, { float(10u * (target.x >> 8)), float(10u * (target.y >> 8)) });
                        apply_ui_camera(park(), window);
                    }
                    case C::source_water or C::source_arabian or C::source_science => {
                        session->request_source(command == C::source_water ? 2 : command == C::source_arabian ? 0 : 1, false);
                    }
                    case C::build      => park().ui().build_controller().open_build(-1, 0);
                    case C::statistics => park().ui().dispatch_command(WorldUiCommand::park_statistics);
                    case C::finance    => park().ui().dispatch_command(WorldUiCommand::financial_statistics);
                    case _             => throw std::logic_error("Developer command has no application route");
                }
            }
            sync_canvas();
            return {};
        } catch (const std::exception& e) {
            return std::unexpected(e.what());
        }
    };
    developer.key = [this](const UiEvent& event) {
        return shortcuts && shortcuts->key(event, WorldUiShortcutRoute::panel);
    };
    developer.report_error = [this](std::string_view error) {
        game_ui->report_error({ u"SimCoaster", postcard_from_ansi(error) });
        sync_canvas();
    };
    debug_selector = std::make_unique<ApplicationDebugSelectorController>(context(), park().ui().controls(), canvas, std::move(developer));
}

bool ApplicationWindow::shortcut_action(WorldUiShortcutAction selected, std::int32_t argument) {
    using A = WorldUiShortcutAction;
    match (selected) {
        case A::exit_special_view => {
            park().exit_special_view_from_escape();
            apply_ui_camera(park(), window);
            return true;
        }
        case A::open_objectives => {
            park().ui().open_objectives();
            return true;
        }
        case A::open_training_room => {
            park().ui().staff_controller().open_training(static_cast<ThingId>(argument));
            return true;
        }
        case A::show_tip_of_the_day => {
            (void)tips->show(true);
            sync_canvas();
            return true;
        }
        case A::open_debug_panel_selector => {
            (void)debug_selector->open();
            sync_canvas();
            return true;
        }
        case A::toggle_hud => {
            persistent(!park().ui().primary_root()->visible());
            sync_canvas();
            return true;
        }
        case A::open_load_dialog => {
            game_ui->open_load(false);
            sync_canvas();
            return true;
        }
        case A::open_save_dialog => {
            game_ui->open_save(false);
            sync_canvas();
            return true;
        }
        case A::open_coaster_name_dialog => {
            game_ui->open_coaster_save();
            sync_canvas();
            return true;
        }
        case _ => return apply_world_ui_shortcut_action(park(), selected, argument);
    }
}

void ApplicationWindow::bind_shortcuts() {
    WorldUiShortcutHost host;
    host.close_context_menu = [this] {
        park().ui().close_context_menus();
    };
    host.command = [this](WorldUiCommand requested) {
        park().ui().dispatch_command(requested);
        sync_canvas();
    };
    host.action = [this](WorldUiShortcutAction selected, std::int32_t argument) {
        return shortcut_action(selected, argument);
    };
    host.native_key_code = [](const UiEvent& event) -> std::uint16_t {
        const auto packed = event.id;
        if (packed <= 255) {
            return static_cast<std::uint16_t>(packed);
        }
        const auto virtual_key = packed >> 8;
        return normalize_world_ui_platform_key(virtual_key, MapVirtualKeyW(virtual_key, 2), (static_cast<std::uintptr_t>(event.value) & world_ui_extended_key_flag) != 0);
    };
    shortcuts = std::make_unique<WorldUiShortcuts>(context(), root, std::move(host));
    park().ui().set_shortcut_handler([this](const UiEvent& event, WorldUiShortcutRoute route) {
        return shortcuts && current_world_input_owner() != ApplicationWorldInputOwner::blocked && shortcuts->key(event, route);
    });
    auto& ui = park().ui().controls();
    ui.set_primary_callback(*canvas, [this](UiControl& control, const UiEvent& event) -> std::intptr_t {
        if (shortcuts && current_world_input_owner() != ApplicationWorldInputOwner::blocked && (event.type == 65546 || event.type == 65547)) {
            const bool hidden = !park().ui().primary_root()->visible() && !park().ui().secondary_root()->visible();
            if (shortcuts->key(event, hidden ? WorldUiShortcutRoute::hidden_hud : WorldUiShortcutRoute::panel)) {
                return 1;
            }
        }
        return park().ui().controls().secondary(control, event);
    });
}

void ApplicationWindow::bind_world(ParkRuntime* value, ApplicationSessionPage page) {
    if (!value) {
        return;
    }
    auto& ui = value->ui();
    auto& c = value->context();
    window.park = value;
    window.ui = &ui;
    window.ui_context = &c;
    window.preferences = &value->preferences();
    window.ui_commands_enabled = value->ui_commands_enabled();
    ui.bind_gameplay_pause([this] {
        return paused() || park().ui().simulation_paused();
    });
    world_input_owner = std::make_unique<ApplicationWorldInputRuntime>();
    bool preserve_balance = false;
    for (const auto& path : { root / "Data/levels/Standard.sam", root / "Data/levels" / c.source.selected_resource_source_name / "Standard.sam" }) {
        if (auto balance = c.ports.assets.read_balance_asset(c, path.native_encoded_string())) {
            world_input_owner->configure_balance(*balance, preserve_balance);
            preserve_balance = true;
        }
    }
    apply_ui_camera(*value, window);
    canvas = &ui.controls().create(nullptr, UiControlKind::window, 0, 400000, { 0, 0, 2048, 1536 });
    bind_management();
    bind_game_ui();
    bind_frontend();
    bind_map();
    bind_postcards();
    bind_coaster_source();
    staff = std::make_unique<StaffWorldInput>(*value, window, value->world_scene().terrain()->terrain->view());
    authoring = std::make_unique<ParkAuthoringFrontend>(
        *value,
        window.camera,
        value->world_scene().terrain()->terrain->view(),
        std::filesystem::path {},
        handle,
        false,
        [this] {
            release_unused_pointer_capture();
        },
        window.cursor_assets);
    window.authoring = authoring.get();
    window.drain_world_input = [this](HWND target) {
        staff->update(target);
    };
    window.prepare_default_world_press = [this](int x, int y) {
        return staff->prepare_press(x, y);
    };
    bind_ending();
    bind_tips();
    bind_shortcuts();
    previous_event = c.ui.panels.park_object_event;
    c.ui.panels.park_object_event = [this](WorldRuntime& source, const WorldEvent& event) {
        if (previous_event) {
            previous_event(source, event);
        }
        if (park_map) {
            park_map->handle_event(event);
        }
        if (game_ui) {
            game_ui->handle_event(event);
        }
    };
    ui.set_draw_underlay([this](UiDrawList& output, unsigned width, unsigned height) {
        if (park_map && park_map->is_open()) {
            park_map->append(output, width, height);
        }
    });
    ui.set_control_draw_hook([this](UiDrawList& output, const UiControl& control, UiRect clip, unsigned width, unsigned height) {
        if (postcard_ui) {
            postcard_ui->append(output, control, clip, width, height);
        }
    });
    if (session->binding_rollback()) {
        if (outgoing_views) {
            const auto& saved = *outgoing_views;
            if (saved.primary_visible) {
                ui.restore_persistent();
            } else {
                ui.hide_persistent();
            }
            ui.controls().set_visible(*ui.secondary_root(), saved.secondary_visible);
            auto* input = saved.secondary_visible ? ui.secondary_root() : saved.primary_visible ? ui.primary_root() : nullptr;
            ui.controls().set_input_root(input);
            ui.controls().set_focus(input);
            command_families(saved.commands_enabled);
            if (saved.frontend && saved.frontend->phase != ApplicationFrontendPhase::none) {
                frontend->restore_resume_state(*saved.frontend);
            }
        } else {
            persistent(true);
            command_families(true);
            if (page == ApplicationSessionPage::intro || page == ApplicationSessionPage::profiles) {
                frontend->open_profiles();
            }
        }
    } else if (page == ApplicationSessionPage::intro) {
        c.mode.source_recreation = 1;
        const auto id = find_first_park_object_record_id_by_definition_id(c, 4798);
        auto* manager = c.simulation.current->saved.animation_manager ? c.entities.at(c.simulation.current->saved.animation_manager).payload : nullptr;
        if (!id || !manager || manager->kind != ThingKind::animation_manager || !start_world_animation_playback(c, reinterpret_cast<AnimationManagerRecord*>(manager), id, 1, AnimationPlaybackProfile::code_1)) {
            throw std::runtime_error("Cannot start the original Intro camera sequence");
        }
        frontend->open_intro();
    } else if (page == ApplicationSessionPage::profiles) {
        c.mode.source_recreation = 1;
        frontend->open_profiles();
    } else {
        persistent(true);
        command_families(true);
        if (page == ApplicationSessionPage::park && !graphics_recreating) {
            const auto activation = apply_application_world_entry(*value, session->world_entry(), session->take_post_load_chooser_pending());
            if (activation.presentation == ApplicationWorldEntryPresentation::load_chooser) {
                frontend->open_chooser(4, true);
            } else if (activation.presentation == ApplicationWorldEntryPresentation::tip_of_the_day) {
                (void)tips->show(false);
            }
        }
    }
    sync_canvas();
}

void ApplicationWindow::release_world(ParkRuntime* old) noexcept {
    if (!old) {
        return;
    }
    const auto attempt = [](auto&& operation) noexcept {
        try {
            operation();
        } catch (const std::exception& error) {
            print_text(stderr, "Application view teardown: {}\n", error.what());
        }
    };
    auto& ui = old->ui();
    if (!session || !session->binding_rollback()) {
        outgoing_views.reset();
        attempt([&] {
            ViewResumeState saved;
            saved.primary_visible = ui.primary_root()->visible();
            saved.secondary_visible = ui.secondary_root()->visible();
            saved.commands_enabled = old->ui_commands_enabled();
            if (frontend) {
                saved.frontend = frontend->capture_resume_state();
            }
            outgoing_views = std::move(saved);
        });
    }
    attempt([&] {
        ui.controls().cancel_pointer_input();
    });
    attempt([&] {
        ui.close_panel();
    });
    old->context().ui.panels.park_object_event = std::move(previous_event);
    ui.set_control_draw_hook({});
    ui.set_draw_underlay({});
    ui.set_shortcut_handler({});
    ui.bind_frame_poll({});
    ui.bind_gameplay_pause({});
    window.authoring = nullptr;
    window.drain_world_input = {};
    window.prepare_default_world_press = {};
    window.path_hover_inspection = false;
    if (world_input_owner) {
        world_input_owner->cancel(world_input_capture());
    }
    world_input_owner.reset();
    window.application_pointer_capture = false;
    //A failed input callback must not skip teardown of the other owners.
    //Explicit close precedes destruction while every borrowed World lives.
    if (ending) {
        attempt([&] {
            ending->close();
        });
    }
    if (tips) {
        attempt([&] {
            tips->close();
        });
    }
    if (debug_selector) {
        attempt([&] {
            debug_selector->close();
        });
    }
    if (postcard_ui) {
        attempt([&] {
            postcard_ui->close();
        });
    }
    if (coaster_sources) {
        attempt([&] {
            coaster_sources->close();
        });
    }
    if (park_map) {
        attempt([&] {
            park_map->close();
        });
    }
    if (frontend) {
        attempt([&] {
            frontend->close();
        });
    }
    if (game_ui) {
        attempt([&] {
            game_ui->close();
        });
    }
    ending.reset();
    tips.reset();
    debug_selector.reset();
    shortcuts.reset();
    postcard_ui.reset();
    coaster_sources.reset();
    park_map.reset();
    frontend.reset();
    game_ui.reset();
    authoring.reset();
    staff.reset();
    attempt([&] {
        if (ui.controls().owns(canvas)) {
            ui.controls().destroy(canvas);
        }
    });
    window.frontend_cursor.reset();
    canvas = nullptr;
    screen_particles = {};
    pauses = {};
    pending_send_failures.clear();
    map_active = false;
    window.ui = nullptr;
    window.ui_context = nullptr;
    window.park = nullptr;
    window.preferences = nullptr;
    window.staff_input.clear();
    window.staff_camera_keys = window.previous_staff_camera_keys = {};
    window.ui_buttons = {};
    window.world_button_gestures = window.world_buttons_down = {};
    window.coaster_geometry_capture = window.coaster_camera_gesture = false;
    window.orbit = window.pan = false;
    window.camera_mouse_look = false;
    if (window.cursor_assets) {
        window.cursor_assets->reset();
    }
    if (GetCapture() == handle) {
        ReleaseCapture();
    }
}

bool ApplicationWindow::action(const WorldUiAction& action) {
    match (action) {
        case { const WorldFinishIntroAction& payload }  => frontend->finish_intro(payload.force != 0);
        case { const WorldSourceChangeAction& payload } => {
            close_auxiliary();
            park().ui().close_panel();
            frontend->open_chooser(payload.source, payload.force != 0);
        }
        case { const WorldCommandAction& payload } => {
            match (payload.command) {
                case WorldUiCommand::game_menu => game_ui->open_menu();
                case WorldUiCommand::park_map  => {
                    close_auxiliary();
                    park_map->open();
                }
                case WorldUiCommand::overview_map => {
                    close_auxiliary();
                    park().ui().close_panel();
                    frontend->open_chooser();
                }
                case WorldUiCommand::postcard => {
                    close_auxiliary();
                    postcard_ui->open_composer();
                }
                case WorldUiCommand::coaster_source => open_coaster_source(false);
                case _                              => return false;
            }
        }
        case _ => return false;
    } sync_canvas();
    return true;
}

std::optional<ApplicationWorldPointerEvent> ApplicationWindow::pointer_event(UINT message) {
    match (message) {
        case WM_MOUSEMOVE   => return ApplicationWorldPointerEvent::move;
        case WM_LBUTTONDOWN => return ApplicationWorldPointerEvent::left_down;
        case WM_LBUTTONUP   => return ApplicationWorldPointerEvent::left_up;
        case WM_RBUTTONDOWN => return ApplicationWorldPointerEvent::right_down;
        case WM_RBUTTONUP   => return ApplicationWorldPointerEvent::right_up;
        case WM_MBUTTONDOWN => return ApplicationWorldPointerEvent::middle_down;
        case WM_MBUTTONUP   => return ApplicationWorldPointerEvent::middle_up;
        case WM_MOUSEWHEEL  => return ApplicationWorldPointerEvent::wheel;
        case _              => return std::nullopt;
    }
}

ApplicationWorldInputCapture ApplicationWindow::world_input_capture() const {
    ApplicationWorldInputCapture capture;
    for (std::size_t i = 0; i < capture.world_buttons.size(); ++i) {
        capture.world_buttons[i] = window.world_buttons_down[i] || window.world_button_gestures[i];
    }
    capture.coaster_geometry = window.coaster_geometry_capture;
    capture.coaster_geometry_active = coaster_geometry_active(window);
    capture.world_buttons[0] = capture.world_buttons[0] || (authoring && authoring->pointer_active());
    return capture;
}

ApplicationWorldInputOwner ApplicationWindow::current_world_input_owner() const {
    if (!window.park || !booted || loading || !active || GetActiveWindow() != handle || !window.width || !window.height || !window.park->ui_commands_enabled() || !world_input_available() || std::ranges::any_of(pauses, [](unsigned value) {
            return value != 0;
        })) {
        return ApplicationWorldInputOwner::blocked;
    }
    return window.ui_context->view.flags & 0x16u ? ApplicationWorldInputOwner::special_camera : ApplicationWorldInputOwner::normal_camera;
}

void ApplicationWindow::release_unused_pointer_capture() {
    release_host_pointer_capture(window, handle);
}

void ApplicationWindow::apply_world_input_cleanup(const ApplicationWorldInputCleanup& cleanup) {
    if (cleaning_world_input) {
        return;
    }
    cleaning_world_input = true;

    struct Finish {
        bool& cleaning;

        ~Finish() {
            cleaning = false;
        }
    } finish { cleaning_world_input };

    for (std::size_t i = 0; i < cleanup.clear_world_buttons.size(); ++i) {
        if (cleanup.clear_world_buttons[i]) {
            window.world_buttons_down[i] = window.world_button_gestures[i] = false;
            if (window.ui_context) {
                if (i == 0) {
                    window.ui_context->staff.interaction.left_button_held = false;
                } else {
                    window.ui_context->staff.interaction.right_button_held = false;
                }
            }
        }
    }
    if (cleanup.cancel_authoring_pointer) {
        if (staff) {
            staff->clear_cursor();
        }
        window.world_buttons_down = window.world_button_gestures = {};
        window.staff_input.clear();
        window.orbit = window.pan = window.coaster_camera_gesture = false;
        if (window.ui_context) {
            window.ui_context->staff.interaction.left_button_held = false;
            window.ui_context->staff.interaction.right_button_held = false;
        }
    }
    if (cleanup.clear_camera_keys) {
        window.staff_camera_keys = window.previous_staff_camera_keys = {};
    }
    if (cleanup.cancel_authoring_pointer && shortcuts) {
        shortcuts->clear_active();
    }
    if (cleanup.cancel_authoring_pointer && window.park) {
        window.park->set_camera_input({});
        window.camera_mouse_look = false;
        if (window.cursor_assets) {
            window.cursor_assets->set_visible(true);
        }
    }
    window.application_pointer_capture = world_input_owner && world_input_owner->normal_pointer_capture();
    if (cleanup.cancel_coaster_geometry) {
        window.coaster_geometry_capture = false;
        if (window.park) {
            (void)window.park->coaster_geometry_pointer(CoasterGeometryPointerPhase::cancel, { window.mouse_x, window.mouse_y }, window.width, window.height);
        }
    }
    if (cleanup.cancel_authoring_pointer && authoring) {
        authoring->cancel_pointer_input();
    }
    if (cleanup.release_pointer) {
        release_unused_pointer_capture();
    }
}

void ApplicationWindow::synchronize_world_input() {
    if (!world_input_owner || cleaning_world_input) {
        return;
    }
    apply_world_input_cleanup(world_input_owner->synchronize(current_world_input_owner(), world_input_capture()));
}

void ApplicationWindow::cancel_world_pointer_input() {
    if (world_input_owner && !cleaning_world_input) {
        apply_world_input_cleanup(world_input_owner->cancel(world_input_capture()));
    }
}

bool ApplicationWindow::input(UINT message, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (message == WM_ACTIVATEAPP) {
        active = wp != 0;
        if (!active && shortcuts) {
            shortcuts->clear_active();
        }
        if (!active && movie.status().playing) {
            movie.stop(ApplicationMovieStop::deactivated);
        }
    }
    //This runs before the shared early coaster-capture/UI routes. An
    //automatically opened Ending notice needs no mouse event to revoke the
    //previous World owner; its next platform boundary also observes it here.
    synchronize_world_input();
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE || (message == WM_ACTIVATEAPP && !wp)) {
        cancel_world_pointer_input();
    }
    //Press owners receive their releases before a newer UI root. Fresh UI
    //presses clear only that early claim and keep the usual UI-first route.
    if (world_input_owner) {
        if (const auto event = pointer_event(message)) {
            world_input_owner->observe_platform_press(*event);
            if (world_input_owner->owns_pointer_release(*event)) {
                return world_input(message, wp, lp);
            }
        }
    }
    if (loading) {
        if (message == WM_CLOSE) {
            close_after_loading = true;
            return true;
        }
        return platform_input_boundary(message);
    }
    if (!booted) {
        if (message == WM_CLOSE) {
            close_after_loading = true;
            movie.stop(ApplicationMovieStop::replaced);
            return true;
        }
        if ((message == WM_KEYDOWN && wp == VK_ESCAPE) || message == WM_LBUTTONUP || message == WM_RBUTTONUP) {
            movie.stop();
            return true;
        }
        return platform_input_boundary(message);
    }
    if (message == WM_CLOSE) {
        game_ui->request_quit_confirmation();
        sync_canvas();
        return true;
    }
    if ((message == WM_KEYDOWN || message == WM_KEYUP) && frontend_active()) {
        const auto modifiers = static_cast<std::uint32_t>(context().construction.modifiers);
        if (frontend->key(static_cast<std::uint32_t>(wp), message == WM_KEYDOWN, modifiers)) {
            return true;
        }
    }
    if (shortcuts && current_world_input_owner() != ApplicationWorldInputOwner::blocked && (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && !park().ui().primary_root()->visible() && !park().ui().secondary_root()->visible()) {
        auto* focus = park().ui().controls().focus();
        bool visible_focus = focus != nullptr;
        for (auto* ancestor = focus; ancestor; ancestor = ancestor->parent) {
            visible_focus = visible_focus && ancestor->visible();
        }
        if (!visible_focus || focus == canvas) {
            const UiEvent event { message == WM_KEYDOWN || message == WM_SYSKEYDOWN ? 65546 : 65547, static_cast<std::uint32_t>(wp) << 8, world_ui_platform_key_flags(static_cast<std::uintptr_t>(lp), static_cast<std::uint32_t>(context().construction.modifiers)) };
            if (shortcuts->key(event, WorldUiShortcutRoute::hidden_hud)) {
                sync_canvas();
                return true;
            }
        }
    }
    //Let native UI key routing handle F12: shortcut action17 opens the
    //developer selector only after the park-name cheat has been enabled.
    //The standalone diagnostic viewer owns its separate screenshot shortcut.
    return false;
}

bool ApplicationWindow::world_input(UINT message, WPARAM wp, LPARAM lp) {
    if (!world_input_owner || !window.park) {
        return false;
    }
    //UI/control callbacks may have changed admission since input() ran.
    synchronize_world_input();
    const auto event = pointer_event(message);
    if (!event) {
        //UI has already had first refusal. Exclusive application owners also
        //exclude unconsumed authoring commands/keys, not only mouse gestures.
        return world_input_owner->owner() == ApplicationWorldInputOwner::blocked && (message == WM_COMMAND || message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_CHAR || message == WM_SYSCHAR);
    }
    if (*event != ApplicationWorldPointerEvent::wheel) {
        window.mouse_x = static_cast<short>(LOWORD(lp));
        window.mouse_y = static_cast<short>(HIWORD(lp));
    }
    //The old shared path activated a staged source before checking the
    //application's exclusive owner or a cancelled inherited release. Keep
    //activation before native pointer delivery, using the same coordinator.
    if (world_input_owner->admits_pointer(*event) && world_pointer_message(message) && !ui_owns_pointer(window) && park().activate_pending_coaster_source()) {
        park().authoring().synchronize_from_world();
        if (authoring) {
            authoring->note_external_change();
        }
        synchronize_world_input();
    }
    WorldNormalCameraPointerSample sample;
    if (window.width && window.height) {
        sample.logical_position = { static_cast<std::int32_t>(std::int64_t { window.mouse_x } * 2048 / window.width), static_cast<std::int32_t>(std::int64_t { window.mouse_y } * 1536 / window.height) };
    }
    sample.ui_time = park().ui().controls().clock();
    sample.platform_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
    sample.staff_area_selection = world_staff_interaction_kind(context()) == WorldInteractionHandlerKind::staff_area;
    const auto feedback = world_input_owner->pointer(*event, sample, park().preferences());
    window.application_pointer_capture = world_input_owner->normal_pointer_capture();
    if (feedback.delivery == ApplicationWorldInputDelivery::right_down) {
        window.world_buttons_down[1] = true;
    }
    if (feedback.capture_pointer) {
        acquire_window_pointer_capture(handle);
    }
    if (feedback.wheel) {
        apply_world_normal_camera_wheel(context(), static_cast<short>(HIWORD(wp)));
    }
    if (feedback.cancel_interaction) {
        //Old handler.slot2C completes relocation/queue work before BASE
        //replaces it. Keep that existing teardown before handler release.
        park().authoring().cancel_interaction(context().construction.modifiers);
        if (authoring) {
            authoring->synchronize_from_world();
            authoring->note_external_change();
        }
        synchronize_world_input();
    }
    if (world_input_owner->owner() != ApplicationWorldInputOwner::blocked) {
        if (feedback.delivery == ApplicationWorldInputDelivery::right_down) {
            queue_world_input(window, handle, { StaffInputKind::right_down, window.mouse_x, window.mouse_y });
        }
        if (feedback.context_menu_release) {
            //5070A0 attempts a BASE menu before4D2260 filters handler-up.
            //Cancellation above must finish the old handler before either route.
            const auto kind = world_staff_interaction_kind(context());
            const bool map_action = (park().authoring().tool() != ParkAuthoringTool::inspect && !window.path_hover_inspection) || coaster_geometry_active(window) || (park().coaster_editor().active_map() && park().coaster_editor().input_route() == CoasterEditorInputRoute::map_action);
            //5070A0's active-UI branch dismisses a locator/context menu before
            //the ordinary map-handler gate. The shared bridge owns that route.
            if (context().ui.active_root || (!map_action && (kind == WorldInteractionHandlerKind::default_ || kind == WorldInteractionHandlerKind::alternate))) {
                queue_world_input(window, handle, { StaffInputKind::menu, window.mouse_x, window.mouse_y });
            }
        }
        if (feedback.delivery == ApplicationWorldInputDelivery::right_release) {
            queue_world_input(window, handle, { StaffInputKind::right_up, window.mouse_x, window.mouse_y });
        }
    }
    for (std::size_t i = 0; i < feedback.clear_world_buttons.size(); ++i) {
        if (feedback.clear_world_buttons[i]) {
            window.world_buttons_down[i] = window.world_button_gestures[i] = false;
            if (i == 0) {
                context().staff.interaction.left_button_held = false;
            } else {
                context().staff.interaction.right_button_held = false;
            }
        }
    }
    //A picker DOWN can synchronously enter an admitted special camera.
    //An exclusive UI action can synchronously acquire a different owner.
    synchronize_world_input();
    if (feedback.release_pointer) {
        release_unused_pointer_capture();
    }
    return feedback.consumed;
}

bool ApplicationWindow::world_input_available() const {
    return !frontend_active()
        && !map_active
        && game_ui
        && !game_ui->is_open()
        && postcard_ui
        && !postcard_ui->is_open()
        && coaster_sources
        && !coaster_sources->is_open()
        && ending
        &&
           //50ACE0 keeps BANKRUPT/Fired text after restoring commands and
           //persistent roots. Only the notice/scene remains exclusive.
           !ending->root() && tips && !tips->is_open() && debug_selector && !debug_selector->is_open();
}

void ApplicationWindow::advance_normal_camera(std::chrono::milliseconds elapsed) {
    auto& current = park();
    auto& c = context();
    auto* focus = current.ui().controls().focus();
    const bool keyboard_owner = focus && (focus == current.ui().primary_root() || focus == current.ui().secondary_root() || focus->id == 32000 || focus->id == 97);
    if (!keyboard_owner) {
        window.staff_camera_keys = {};
    }
    const auto& keys = window.staff_camera_keys;
    WorldNormalCameraInput input;
    input.active = GetActiveWindow() == handle && current.ui_commands_enabled();
    input.pointer_inside_world = client_owns_pointer(handle) && (!ui_owns_pointer(window) || window.application_pointer_capture);
    input.pointer = { window.mouse_x, window.mouse_y };
    input.viewport = { window.width, window.height };
    input.ui_time = current.ui().controls().clock();
    input.rotate_left = keys[0];
    input.rotate_right = keys[1];
    input.zoom_in = keys[2] || keys[8];
    input.zoom_out = keys[3] || keys[9];
    input.pan_forward = keys[4];
    input.pan_backward = keys[5];
    input.pan_left = keys[6];
    input.pan_right = keys[7];
    const auto result = world_input_owner->advance_normal_camera(c, current.preferences(), input, elapsed);
    const auto& feedback = result.camera;
    apply_world_input_cleanup(result.cleanup);
    WorldCameraInput special;
    special.normal_heading_external = true;
    current.set_camera_input(special);
    if (feedback.close_visitor_locator && c.ui.panels.visitor_locator.open) {
        current.ui().visitor_controller().close();
    }
    //Wheel/UI events precede the kernel's before/after snapshots. Publish
    //its resulting state even when this update changed nothing further;
    //otherwise set_render_camera below overwrites their zoom with the old
    //WindowState value. The kernel has now also applied the native limits.
    apply_ui_camera(current, window);
}

void ApplicationWindow::resize_renderer() {
    if (window.width && window.height && (render_width != window.width || render_height != window.height)) {
        renderer->resize(window.width, window.height);
        render_width = window.width;
        render_height = window.height;
    }
}

void ApplicationWindow::pump_messages() {
    MSG message {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            close_after_loading = true;
            system_quit = true;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (window.ui_error) {
        std::rethrow_exception(window.ui_error);
    }
}

void ApplicationWindow::loading_pump() {
    if (!loading) {
        return;
    }
    synchronize_world_input();
    if (!loading_presentation) {
        //Native loading art is explicitly started by source initialization,
        //not by metadata/profile cleanup, saves, or closing a file dialog.
        pump_messages();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!startup) {
        startup.emplace(root, "American", window.width ? window.width : 640, window.height ? window.height : 480);
    }
    if (!loading_painted) {
        if (booted && session && session->park()) {
            park().ui().controls().cancel_pointer_input();
            window.ui_buttons = {};
            window.world_button_gestures = window.world_buttons_down = {};
            window.orbit = window.pan = false;
            window.staff_camera_keys = window.previous_staff_camera_keys = {};
            if (GetCapture() == handle) {
                ReleaseCapture();
            }
        }
        startup->request(booted ? ApplicationStartupStage::loading : ApplicationStartupStage::splash);
        loading_painted = true;
        loading_tick = now;
    }
    startup->advance(std::chrono::duration_cast<std::chrono::milliseconds>(now - loading_tick));
    loading_tick = now;
    pump_messages();
    if (!window.width || !window.height) {
        return;
    }
    resize_renderer();
    draw.clear();
    startup->append(draw);
    renderer->draw(MeshScene {}, window.camera);
    renderer->draw_ui(draw);
    renderer->present();
}

void ApplicationWindow::begin_loading(bool presentation) {
    loading = window.suspend_world_input = true;
    loading_presentation = presentation;
    loading_painted = false;
}

void ApplicationWindow::end_loading() {
    //6F92E0 holds ONLY an already-loaded legal composite, never a splash
    //that finishes loading before its three-second transition. Pump real
    //input/presentation while honoring that minimum instead of busy-waiting.
    if (startup && loading_presentation) {
        startup->request(ApplicationStartupStage::clear);
        while (startup->transition_pending() && !close_after_loading) {
            loading_pump();
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }
    loading = window.suspend_world_input = false;
    loading_presentation = false;
    loading_painted = false;
}

std::expected<void, ApplicationSessionError> ApplicationWindow::save_configuration() {
    try {
        if (session && session->park()) {
            configuration.movie_volume = park().preferences().movie_volume;
            configuration.sound_quality = park().preferences().sound_quality;
        }
        const auto bytes = encode_application_configuration(configuration);
        const auto directory = root / "save";
        std::filesystem::create_directories(directory);
        const auto path = directory / "Config.tcf";
        const auto temporary = directory / "Config.tcf.rebuild.tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            output.flush();
            if (!output) {
                throw std::runtime_error("Cannot write native Config.tcf");
            }
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error(std::format("Cannot publish native Config.tcf (Win32 {})", GetLastError()));
        }
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(ApplicationSessionError { ApplicationSessionFailure::save, root / "save/Config.tcf", error.what() });
    }
}

void ApplicationWindow::apply_graphics() {
    if (!pending_graphics) {
        return;
    }
    const auto requested = *std::exchange(pending_graphics, {});
    const auto old = configuration;
    const auto old_profile = park().hardware_profile();
    bool recreated {};
    begin_loading();
    try {
        const auto dimensions = application_resolution(requested.resolution);
        const auto profile = load_application_hardware_profile(root, requested.quality);
        if (!profile) {
            throw std::runtime_error(profile.error().reason);
        }
        renderer.reset(); //DXGI permits only one flip swapchain for this HWND.
        renderer = std::make_unique<MeshRenderer>(window.width, window.height, handle, !requested.hardware, requested.hardware ? std::optional { static_cast<std::uint32_t>(requested.video_device) } : std::nullopt, profile->graphics);
        RECT rectangle { 0, 0, static_cast<LONG>(dimensions[0]), static_cast<LONG>(dimensions[1]) };
        AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
        if (!SetWindowPos(handle, nullptr, 0, 0, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top, SWP_NOMOVE | SWP_NOZORDER)) {
            throw std::runtime_error("Cannot apply selected window resolution");
        }
        configuration.renderer_backend = requested.hardware ? 1 : 0;
        configuration.video_device = requested.video_device;
        configuration.resolution = requested.resolution;
        configuration.hardware_profile = requested.quality;
        if ((old_profile.graphics.texture_quality < 2) != (profile->graphics.texture_quality < 2)) {
            graphics_recreating = true;
            const auto replacement = session->recreate_graphics(*profile);
            graphics_recreating = false;
            if (!replacement) {
                throw std::runtime_error(replacement.error().detail);
            }
            recreated = true;
        } else {
            park().apply_hardware_profile(*profile);
        }
        render_width = render_height = 0;
        const auto saved = save_configuration();
        if (!saved) {
            throw std::runtime_error(saved.error().detail);
        }
    } catch (const std::exception& error) {
        if (!view_recovery_failure.empty()) {
            end_loading();
            require_bound_views();
        }
        configuration = old;
        renderer.reset();
        renderer = std::make_unique<MeshRenderer>(window.width, window.height, handle, old.renderer_backend == 0, old.renderer_backend ? std::optional { old.video_device } : std::nullopt, old_profile.graphics);
        const auto dimensions = application_resolution(old.resolution);
        RECT rectangle { 0, 0, static_cast<LONG>(dimensions[0]), static_cast<LONG>(dimensions[1]) };
        AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(handle, nullptr, 0, 0, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top, SWP_NOMOVE | SWP_NOZORDER);
        if (recreated) {
            graphics_recreating = true;
            const auto rollback = session->recreate_graphics(old_profile);
            graphics_recreating = false;
            if (!rollback) {
                game_ui->report_error(application_error(rollback.error()));
            }
        } else {
            park().apply_hardware_profile(old_profile);
        }
        game_ui->report_error({ u"SimCoaster", postcard_from_ansi(error.what()) });
    }
    end_loading();
}

ApplicationWindow::ApplicationWindow(std::filesystem::path directory, WindowState& state, HWND target, std::unique_ptr<MeshRenderer>& graphics, ApplicationConfiguration settings, UiPreferences defaults) : root(std::move(directory)), window(state), handle(target), renderer(graphics), configuration(settings), sam_configuration(load_root_sam_configuration(root)), postcards(root) {
    window.application_input = [this](UINT m, WPARAM w, LPARAM l) {
        return input(m, w, l);
    };
    window.application_action = [this](const WorldUiAction& value) {
        return action(value);
    };
    window.application_world_input = [this](UINT message, WPARAM wp, LPARAM lp) {
        return world_input(message, wp, lp);
    };
    ApplicationSessionHost host;
    host.resource_pump = [this] {
        loading_pump();
    };
    host.hardware_profile = [this] {
        auto profile = load_application_hardware_profile(root, configuration.hardware_profile);
        if (!profile) {
            throw std::runtime_error(profile.error().reason);
        }
        return *profile;
    };
    host.release_world_views = [this](ParkRuntime* old) {
        release_world(old);
    };
    host.bind_world = [this](ParkRuntime* value, ApplicationSessionPage page) {
        try {
            bind_world(value, page);
            view_recovery_failure.clear();
        } catch (const std::exception& error) {
            if (session && session->binding_rollback()) {
                view_recovery_failure = error.what();
            }
            throw;
        } catch (...) {
            if (session && session->binding_rollback()) {
                view_recovery_failure = "Unknown view binding failure";
            }
            throw;
        }
    };
    host.complete_request = [this](std::uint64_t serial, std::optional<ApplicationSessionError> error) {
        if (!game_ui) {
            return;
        }
        if (serial) {
            game_ui->complete(serial, error ? std::optional { application_error(*error) } : std::nullopt);
        } else if (error) {
            game_ui->report_error(application_error(*error));
        }
    };
    host.profile_services = [this](const std::filesystem::path& path, bool read) -> std::expected<void, ApplicationSessionError> {
        if (read && path.empty()) {
            postcards.clear_profile();
            return {};
        }
        const auto result = read ? postcards.select_profile(path) : postcards.save_profile();
        if (!result) {
            return std::unexpected(ApplicationSessionError { ApplicationSessionFailure::profile, result.error().path, result.error().reason });
        }
        return {};
    };
    host.save_configuration = [this] {
        return save_configuration();
    };
    host.external_request = [this](const ApplicationGameRequest& request) -> std::expected<void, ApplicationSessionError> {
        if (std::holds_alternative<ApplicationChooseCoasterSourceRequest>(request.payload)) {
            open_coaster_source(false);
            return {};
        }
        if (std::holds_alternative<ApplicationCancelLoadRequest>(request.payload)) {
            if (std::get<ApplicationCancelLoadRequest>(request.payload).from_profile) {
                auto& controls = park().ui().controls();
                auto* profile = frontend->root();
                //547750 closes its overlay and restores the existing Profile UI.
                //Reopening it resets the lobby/selection transition unnecessarily.
                if (frontend->phase() == ApplicationFrontendPhase::profiles && controls.owns(profile)) {
                    controls.set_input_root(profile);
                    controls.set_focus(profile);
                } else {
                    frontend->open_profiles();
                }
            }
            return {};
        }
        if (std::holds_alternative<ApplicationGoOfflineRequest>(request.payload)) {
            return {}; //No online session exists to disconnect.
        }
        return std::unexpected(ApplicationSessionError { ApplicationSessionFailure::external, {}, "The original online service is unavailable." });
    };
    constexpr std::array<std::string_view, 4> sources { "arabian", "science", "water", "wIntro" };
    session = std::make_unique<ApplicationSession>(root, sources, defaults, std::move(host));
}

ApplicationWindow::~ApplicationWindow() {
    session.reset(); //Host callbacks and all their captured owners still live.
    window.application_input = {};
    window.application_action = {};
    window.application_world_input = {};
}

void ApplicationWindow::boot() {
    startup.emplace(root, "American", window.width, window.height);
    //6A1400 initially consumes presentation stage0 (clear). Movie states
    //4..8 precede source startup9; that source loader starts the splash.
    auto previous = std::chrono::steady_clock::now();
    SYSTEMTIME date {};
    GetLocalTime(&date);
    //Native6A1400 checks dialog graphics bit512, independently of the
    //Options Movie attenuation. Both movies run only on that startup route.
    if (application_startup_movies_enabled(root)) {
        for (std::size_t index = 0; const auto& path : application_startup_movie_paths(root, "American", date.wDay)) {
            const auto movie_index = index++;
            if (!path) {
                continue;
            }
            try {
                movie.open(*path, application_movie_volume(configuration.movie_volume));
            } catch (const std::exception& error) {
                //6A1780/6A1930 ignore a failed native playback start. Report the
                //actual decoder/I/O error and continue to the application frontend.
                print_text(stderr, "Original startup movie {}: {}\n", path->native_encoded_string(), error.what());
                movie.stop(ApplicationMovieStop::failed);
                continue;
            }
            movie.set_viewport_offset({ 0, movie_index ? 64.f / 480.f : 0.f });
            previous = std::chrono::steady_clock::now();
            while (movie.status().playing && !close_after_loading) {
                pump_messages();
                const auto now = std::chrono::steady_clock::now();
                try {
                    movie.advance(std::chrono::duration_cast<std::chrono::milliseconds>(now - previous));
                } catch (const std::exception& error) {
                    print_text(stderr, "Original startup movie {}: {}\n", path->native_encoded_string(), error.what());
                    movie.stop(ApplicationMovieStop::failed);
                    break;
                }
                previous = now;
                if (!window.width || !window.height) {
                    MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    continue;
                }
                resize_renderer();
                draw.clear();
                movie.append(draw);
                renderer->draw(MeshScene {}, window.camera);
                renderer->draw_ui(draw);
                renderer->present();
            }
        }
    }
    if (close_after_loading) {
        return;
    }
    begin_loading();
    auto initialized = session->boot_intro();
    end_loading();
    if (!initialized) {
        throw std::runtime_error(initialized.error().detail);
    }
    booted = true;
}

void ApplicationWindow::report_runtime_failure(std::exception_ptr failure) const noexcept {
    //Report before ApplicationWindow unwinds: the live World still provides
    //the camera and physical-button state that led to an intermittent error.
    try {
        std::string reason;
        try {
            std::rethrow_exception(failure);
        } catch (const MeshError& error) {
            reason = describe_mesh_error(error);
        } catch (const TextureError& error) {
            reason = describe_texture_error(error);
        } catch (const SoundError& error) {
            reason = describe_sound_error(error);
        } catch (const UiTemplateError& error) {
            reason = std::format("UI template word {}: {}", error.word, error.reason);
        } catch (const std::exception& error) {
            reason = error.what();
        } catch (...) {
            reason = "Unknown non-standard exception";
        }
        SYSTEMTIME time {};
        GetSystemTime(&time);
        auto record = std::format("SimCoaster runtime failure, UTC {}-{}-{} {}:{}:{}\nStage: {}\nError: {}\nPointer: {},{}; buttons left/right: {:d}/{:d}\n", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, frame_stage, reason, window.mouse_x, window.mouse_y, window.world_buttons_down[0], window.world_buttons_down[1]);
        if (window.park) {
            const auto& c = window.park->context();
            const auto& camera = c.view.camera;
            std::format_to(std::back_inserter(record), "Camera flags: {}; focus: {:.6g},{:.6g},{:.6g}; heading/pitch: {:.6g}/{:.6g}; pan: {:.6g},{:.6g}; zoom: {:.6g}; RMB pan preference: {:d}\n", c.view.flags, camera.focus_position[0], camera.focus_position[1], camera.focus_position[2], camera.heading_radians, camera.pitch_radians, camera.damped_pan_x, camera.damped_pan_z, c.view.mobile.camera_zoom, window.park->preferences().rmb_scroll_on != 0);
        }
        record += '\n';
        print_text(stderr, "{}", record);
        const auto path = root / "simcoaster-error.log";
        std::ofstream log(path, std::ios::app);
        log.write(record.data(), static_cast<std::streamsize>(record.size()));
        log.flush();
        const auto body = reason + "\n\nStage: " + frame_stage + (log ? "\n\nDetails saved to: " + path.native_encoded_string() : "\n\nThe error log could not be written.");
        MessageBoxA(handle, body.c_str(), "SimCoaster runtime error", MB_OK | MB_ICONERROR);
    } catch (...) {
        //Reporting must never replace or propagate the original failure.
        MessageBoxW(handle, L"The game stopped because of an error. Its error " L"report could not be written.", L"SimCoaster runtime error", MB_OK | MB_ICONERROR);
    }
}

int ApplicationWindow::run(const std::optional<std::filesystem::path>& initial, std::string theme) {
    boot();
    if (!booted) {
        return 0;
    }
    if (initial) {
        session->request_saved_park(*initial, std::move(theme));
    }
    auto previous = std::chrono::steady_clock::now();
    while (session->page() != ApplicationSessionPage::quit) {
        frame_stage = "platform input";
        sync_canvas();
        pump_messages();
        if (system_quit) {
            system_quit = close_after_loading = false;
            session->request_quit();
        } else if (close_after_loading) {
            close_after_loading = false;
            game_ui->request_quit_confirmation();
        }
        for (auto& request : game_ui->take_requests()) {
            session->request(std::move(request));
        }
        const bool session_work = session->has_pending_requests();
        frame_stage = "park/session transition";
        begin_loading(session->has_pending_world_transition());
        session->process_requests();
        require_bound_views();
        const bool loaded = loading_painted;
        end_loading();
        if (!session->park()) {
            break;
        }
        const auto graphics_begin = std::chrono::steady_clock::now();
        frame_stage = "graphics settings";
        const bool graphics_changed = pending_graphics.has_value();
        apply_graphics();
        require_bound_views();
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = (loaded || session_work) ? std::chrono::milliseconds {} : std::chrono::duration_cast<std::chrono::milliseconds>((graphics_changed ? graphics_begin : now) - previous);
        //Loading elapsed belongs to neither the outgoing nor incoming World.
        //Retain sub-millisecond frame time except at an explicit loading or
        //graphics boundary; dropping it every frame makes the clocks run slow.
        if (loaded || session_work || graphics_changed) {
            previous = now;
        } else {
            previous += elapsed;
        }
        if (!window.width || !window.height) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        auto& current = park();
        frame_stage = "World input and camera preparation";
        const auto& limits = current.preferences();
        window.ui_commands_enabled = current.ui_commands_enabled();
        process_ui_actions(current, window, handle, limits);
        synchronize_world_input();
        bool world_input = world_input_owner->owner() != ApplicationWorldInputOwner::blocked;
        if (world_input && client_owns_pointer(handle) && !ui_owns_pointer(window) && current.activate_pending_coaster_source()) {
            current.authoring().synchronize_from_world();
            authoring->note_external_change();
            synchronize_world_input();
        }
        world_input = world_input_owner->owner() != ApplicationWorldInputOwner::blocked;
        if (world_input_owner->owner() == ApplicationWorldInputOwner::normal_camera) {
            advance_normal_camera(elapsed);
        } else if (world_input) {
            if (context().view.mobile.camera_zoom <= 0) {
                context().view.mobile.camera_zoom = context().view.camera.alternate_zoom_limits ? 40.f : current.preferences().minimum_zoom;
            }
            advance_staff_camera(current, window, handle, elapsed);
        } else {
            current.set_camera_input({});
        }
        current.set_render_camera(window.camera);
        window.camera = current.render_camera();
        if (window.ui_camera_changed) {
            current.commit_ui_camera();
            window.ui_camera_changed = false;
        }
        if (world_input) {
            staff->update(handle);
        } else {
            staff->clear_cursor();
        }
        process_ui_actions(current, window, handle, limits);
        synchronize_world_input();
        world_input = world_input_owner->owner() != ApplicationWorldInputOwner::blocked;
        const bool geometry = coaster_geometry_active(window);
        if (geometry && world_input && current.ui_commands_enabled() && !window.orbit && !window.pan && (window.coaster_geometry_capture || !ui_owns_pointer(window)) && client_owns_pointer(handle)) {
            (void)current.coaster_geometry_pointer(CoasterGeometryPointerPhase::hover, { window.mouse_x, window.mouse_y }, window.width, window.height);
        }
        authoring->frame(handle, window.width, window.height, !world_input || !current.ui_commands_enabled() || window.staff_controls || window.staff_editing || geometry || ui_owns_pointer(window), paused(), window.path_hover_inspection);
        current.set_render_camera(window.camera);
        frame_stage = "World simulation and camera update";
        current.advance_frame(elapsed, paused());
        frame_stage = "application UI update";
        synchronize_world_input();
        window.camera = current.render_camera();
        frontend->viewport(window.width, window.height);
        frontend->advance();
        park_map->advance();
        postcard_ui->advance();
        ending->advance();
        synchronize_world_input();
        for (const auto serial : std::exchange(pending_send_failures, {})) {
            postcard_ui->complete_send(serial, false, 1);
        }
        process_ui_actions(current, window, handle, limits);
        synchronize_world_input();
        world_input = world_input_owner->owner() != ApplicationWorldInputOwner::blocked;
        if (world_input) {
            follow_staff_locator(current, window);
        }
        window.camera = current.render_camera();
        frame_stage = "camera pointer feedback";
        apply_camera_input_feedback(current, window, handle);
        release_unused_pointer_capture();
        sync_canvas();
        if (client_owns_pointer(handle)) {
            install_ui_cursor(window);
        }
        resize_renderer();
        frame_stage = "World scene submission";
        if (map_active) {
            scene = {};
            current.prepare_scene_atmosphere(scene, window.camera, ParkScenePass::park_map);
            renderer->draw(scene, window.camera);
        } else {
            current.build_scene(scene, window.camera, float(window.width) / float(window.height));
            append_locator_outline(current, scene);
            renderer->draw(scene, window.camera);
        }
        frame_stage = "UI scene submission";
        current.ui().build_draw(draw, window.width, window.height);
        renderer->draw_ui(draw);
        current.build_screen_particles(screen_particles, static_cast<float>(window.width) / static_cast<float>(window.height));
        renderer->draw_effects(screen_particles);
        if (window.capture) {
            write_texture_png(renderer->capture(), root / "SimCoaster-application.png");
            window.capture = false;
        }
        frame_stage = "presentation";
        renderer->present();
    }
    return 0;
}
} //namespace simcoaster::win32_host

namespace simcoaster {
using namespace win32_host;

int run_application(const std::filesystem::path& directory, const std::optional<std::filesystem::path>& initial, std::string theme) {
    auto root = directory.empty() ? std::filesystem::current_path() : std::filesystem::absolute(directory);
    if (directory.empty() && !std::filesystem::is_regular_file(root / "Data/levels/Standard.sam")) {
        std::vector<wchar_t> executable(32768);
        const auto count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!count || count == executable.size()) {
            throw std::runtime_error("Cannot resolve the application directory");
        }
        auto parent = std::filesystem::path(std::wstring_view(executable.data(), count)).parent_path();
        while (!parent.empty()) {
            if (std::filesystem::is_regular_file(parent / "Data/levels/Standard.sam")) {
                root = parent;
                break;
            }
            if (parent == parent.parent_path()) {
                break;
            }
            parent = parent.parent_path();
        }
    }
    root = root.lexically_normal();
    auto defaults = load_native_ui_preferences(root);
    if (!defaults) {
        throw std::runtime_error(defaults.error().reason);
    }
    ApplicationConfiguration configuration;
    configuration.movie_volume = defaults->movie_volume;
    configuration.sound_quality = defaults->sound_quality;
    const auto path = root / "save/Config.tcf";
    if (std::filesystem::exists(path)) {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            throw std::runtime_error("Cannot open native Config.tcf");
        }
        const auto size = stream.tellg();
        if (size != 32) {
            throw std::runtime_error("Config.tcf must contain its complete native32-byte record");
        }
        std::array<std::byte, 32> bytes;
        stream.seekg(0);
        if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
            throw std::runtime_error("Cannot read native Config.tcf");
        }
        const auto decoded = decode_application_configuration(bytes);
        if (!decoded) {
            throw std::runtime_error(decoded.error().reason);
        }
        configuration = *decoded;
    }
    defaults->movie_volume = configuration.movie_volume;
    defaults->sound_quality = configuration.sound_quality;
    const auto dimensions = application_resolution(configuration.resolution);
    const auto profile = load_application_hardware_profile(root, configuration.hardware_profile);
    if (!profile) {
        throw std::runtime_error(profile.error().reason);
    }
    const auto devices = MeshRenderer::hardware_adapters();
    if (configuration.renderer_backend && devices.empty()) {
        configuration.renderer_backend = 0;
        configuration.video_device = 0;
    } else if (configuration.renderer_backend && configuration.video_device >= devices.size()) {
        configuration.video_device = 0;
    }
    WindowState state;
    state.width = dimensions[0];
    state.height = dimensions[1];
    UiCursorWin32 cursors(root);
    state.cursor_assets = &cursors;
    Window window(state, L"SimCoaster");
    auto renderer = std::make_unique<MeshRenderer>(state.width, state.height, window.handle, configuration.renderer_backend == 0, configuration.renderer_backend ? std::optional { configuration.video_device } : std::nullopt, profile->graphics);
    ApplicationWindow application(root, state, window.handle, renderer, configuration, *defaults);
    ShowWindow(window.handle, SW_SHOW);
    try {
        return application.run(initial, std::move(theme));
    } catch (...) {
        application.report_runtime_failure(std::current_exception());
        return 1;
    }
}
} //namespace simcoaster
