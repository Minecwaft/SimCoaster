#include "simcoaster/entity_identity.hpp"
#include "simcoaster/application_ending_ui.hpp"
#include "simcoaster/rsse_state.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_park_admission.hpp"
#include <algorithm>
#include <bit>
#include <memory>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char notice_template[]{
#embed "ui_templates/application_ending_notice.bin"
};
constexpr unsigned char overlay_template[]{
#embed "ui_templates/application_ending_overlay.bin"
};
static_assert(sizeof(notice_template) == 650);
static_assert(sizeof(overlay_template) == 36);

template <std::size_t N>
std::u16string_view text_view(const std::array<char16_t, N>& value) {
    const auto end = std::ranges::find(value, char16_t{});
    if (end == value.end())
        throw std::runtime_error("Unterminated original ending mail text");
    return { value.data(), static_cast<std::size_t>(end - value.begin()) };
}

bool escape(const UiEvent& event) {
    return event.type == 65547
        && (event.id > 255 ? event.id >> 8 : event.id) == 27
        && !(static_cast<std::uint32_t>(event.value) & 0xFF0000u);
}

bool is_end_name(std::string_view value) {
    return value.size() == 3
        && (value[0] == 'E' || value[0] == 'e')
        && (value[1] == 'N' || value[1] == 'n')
        && (value[2] == 'D' || value[2] == 'd');
}
} //namespace

struct ApplicationEndingUiController::State {
    WorldRuntime& c;
    UiRuntime& ui;
    UiControl *primary{}, *notice{}, *scene{}, *overlay{}, *overlay_wrapper{};
    ApplicationEndingUiHost host;
    std::filesystem::path game_root;
    std::string language;
    std::unique_ptr<UiFontLibrary> fonts;
    std::optional<WorldMailIdentity> mail;
    MailMessage presentation{};
    std::optional<ApplicationEndingScene> scene_kind;
    ThingId ending_object{};
    const ThingHeader* ending_identity{};
    std::uint64_t ending_creation_time{};
    std::uint32_t cancel_started{};
    std::array<std::uint32_t, 2> overlay_viewport{};
    bool closing{};

    State(WorldRuntime& context,
          UiRuntime& runtime,
          UiControl* parent,
          const std::filesystem::path& path,
          ApplicationEndingUiHost callbacks,
          std::string locale)
        : c(context),
          ui(runtime),
          primary(parent),
          host(std::move(callbacks)),
          game_root(path),
          language(std::move(locale)) {
        if (!ui.owns(primary)
            || !host.viewport
            || !host.command_families_enabled
            || !host.prepare_scene
            || !host.persistent_visibility
            || !host.right_mouse_cancel_enabled
            || !host.failure_audio
            || !host.mail_action)
            throw std::invalid_argument("Ending UI requires its complete application host");
    }

    ~State() {
        close();
    }

    std::u16string text(std::uint32_t row) {
        auto result =
            format_bfst_wide_string(c.messages.mail_language.ui_text, c.messages.mail_language.conversion, row, 16383);
        if (!result)
            throw std::runtime_error("Ending UI text: " + result.error().reason);
        return std::move(*result);
    }

    UiControl& required(UiControl* root, std::uint32_t id) {
        if (auto* found = ui.find_recursive(root, id))
            return *found;
        throw std::runtime_error("Original ending template missing control " + std::to_string(id));
    }

    void destroy(UiControl*& root) {
        auto* old = std::exchange(root, nullptr);
        const auto was_closing = std::exchange(closing, true);
        if (ui.owns(old))
            ui.destroy(old);
        closing = was_closing;
    }

    void restore_focus() {
        if (host.panels.synchronize_focus)
            host.panels.synchronize_focus();
        else
            ui.set_focus(primary);
    }

    void close_notice() {
        auto* old = notice;
        destroy(notice);
        mail.reset();
        if (old && host.panels.deactivate)
            host.panels.deactivate(old);
        if (old)
            restore_focus();
    }

    void close_overlay() {
        overlay = nullptr;
        destroy(overlay_wrapper);
        overlay_viewport = {};
    }

    void retire_end() {
        if (auto* object = EntityIdentity { ending_object, ending_identity, ending_creation_time }.get<ParkObjectRuntimeView>(c.entities)) {
            retire_world_used_thing(c, &object->base.header);
        }
        if (c.messages.congratulations_popup == ending_object)
            c.messages.congratulations_popup = 0;
        ending_object = 0;
        ending_identity = nullptr;
    }

    void close() {
        if (closing)
            return;
        closing = true;
        close_notice();
        const bool had_scene = scene_kind.has_value();
        destroy(scene);
        close_overlay();
        if (ending_object)
            retire_end();
        if (had_scene) {
            c.ports.camera.restore_world_view_from_special_modes(c);
            host.command_families_enabled(true);
            host.persistent_visibility(true, false);
            ui.set_input_root(primary);
            restore_focus();
        }
        scene_kind.reset();
        closing = false;
    }

    void caption(UiControl& control, std::u16string_view value, std::uint32_t font = 5) {
        control.font = font;
        ui.set_text(control, value);
    }

    void bind(UiControl& control) {
        const auto widget = control.primary_callback;
        ui.set_primary_callback(control, [this, widget](UiControl& sender, const UiEvent& event) -> std::intptr_t {
            if (&sender == notice || (event.type >= 256 && event.type <= 2055))
                return notice_event(sender, event);
            return widget ? widget(sender, event) : ui.secondary(sender, event);
        });
        for (auto* child : control.children)
            bind(*child);
    }

    void open(WorldMailIdentity identity) {
        auto* record = find_ui_mail(c, identity);
        if (!record)
            throw std::invalid_argument("Ending notice mailbox identity is no longer live");
        //Copy before dismissing another UI owner; its callback can delete mail.
        const auto snapshot = *record;
        if (host.panels.dismiss)
            host.panels.dismiss();
        close_notice();
        presentation = snapshot;
        presentation.next = nullptr;
        mail = identity;
        auto parsed = ui.parse_template(primary, std::as_bytes(std::span{ notice_template }));
        if (!parsed || !*parsed || (*parsed)->id != 43000)
            throw std::runtime_error("Original category25 ending template failed to parse");
        notice = *parsed;
        ui.set_depth(*notice, notice->render_depth + 4);
        bind(*notice);
        auto& action = required(notice, 43016);
        ui.set_visible(action, presentation.message_code != 0);
        if (presentation.message_code)
            action.help_index = 175;
        auto& heading = required(notice, 43014);
        caption(heading, text_view(presentation.text_buffer_3));
        heading.primary_shadow_text = true; //501D50 dual text attachment.
        for (const auto [id, row] : std::array<std::pair<std::uint32_t, std::uint32_t>, 4>{
                 { { 43001, 855 }, { 43003, 856 }, { 43007, 857 }, { 43011, 858 } } }) {
            auto& label = required(notice, id);
            caption(label, text(row));
            label.color = { 0, 0, 150, 255 };
        }
        caption(required(notice, 43002), text_view(presentation.text_buffer_2));
        caption(required(notice, 43004), text_view(presentation.text_buffer_1));
        caption(required(notice, 43008), text_view(presentation.text_buffer_3));
        //Read the copied packed date even if dismissal deleted the live mail.
        const auto packed_date = presentation.packed_date;
        const std::array date_arguments{
            BfstSignedIntegerArgument{ 41, static_cast<std::int32_t>(packed_date & 255u) },
            BfstSignedIntegerArgument{ 42, static_cast<std::int32_t>((packed_date >> 8) & 255u) + 1 },
            BfstSignedIntegerArgument{ 43, std::bit_cast<std::int32_t>(packed_date) >> 16 }
        };
        auto date = format_bfst_wide_string(c.messages.mail_language.ui_text,
                                            c.messages.mail_language.conversion,
                                            956,
                                            63,
                                            {},
                                            date_arguments);
        if (!date)
            throw std::runtime_error("Ending notice calendar date: " + date.error().reason);
        caption(required(notice, 43010), *date);
        for (const auto id : { 43002u, 43004u, 43008u, 43010u })
            required(notice, id).color = { 0, 0, 0, 255 };
        auto& image = required(notice, 43006);
        if (presentation.auxiliary_field)
            ui.set_value(image, static_cast<std::int16_t>(presentation.auxiliary_field) - 1);
        else
            ui.set_visible(image, false);
        auto& body = required(notice, 43009);
        caption(body, text_view(presentation.rendered_message), 7);
        body.text_constraints.width = true;
        if (host.panels.activate)
            host.panels.activate(notice);
        ui.set_focus(notice);
    }

    void layout_overlay() {
        if (!ui.owns(overlay))
            return;
        auto resolution = host.viewport();
        if (!resolution[0] || !resolution[1])
            throw std::invalid_argument("Ending overlay requires a nonzero viewport");
        if (overlay_viewport == resolution)
            return;
        if (!fonts)
            fonts = std::make_unique<UiFontLibrary>(game_root, language);
        auto font = fonts->slot(0, resolution[0]);
        if (!font)
            throw std::runtime_error("Original ending overlay font: " + font.error().reason);
        UiTextStyle style;
        style.wrap = false;
        style.horizontal = UiTextHorizontal::center;
        style.vertical = UiTextVertical::middle;
        const auto measured = fonts->measure(**font, overlay->text, { 0, 0, 1000, 1000 }, style);
        if (!measured)
            throw std::runtime_error("Ending overlay measurement: " + measured.error().reason);
        const auto width = static_cast<std::int32_t>(measured->bounds[2] - measured->bounds[0]);
        const auto height = static_cast<std::int32_t>(measured->bounds[3] - measured->bounds[1]);
        const auto logical_width = static_cast<std::int32_t>(std::int64_t{ width } * 2048 / resolution[0]);
        const auto logical_height = static_cast<std::int32_t>(std::int64_t{ height } * 1536 / resolution[1]);
        const auto left = 1024 - logical_width / 2, top = 768 - logical_height / 2;
        ui.set_bounds(*overlay, { left, top, left + logical_width, top + logical_height });
        overlay_viewport = resolution;
    }

    void show_overlay(std::uint32_t row) {
        close_overlay();
        //4F4470(a3=0): genuine full-canvas intermediate root, no lolight model.
        overlay_wrapper = &ui.create(nullptr, UiControlKind::window, 1, 0, { 0, 0, 2047, 1535 });
        //50ACE0 retains failure text after restoring the World/HUD. This
        //caption-only global subtree is presentation, not a full-canvas input
        //owner when dialogs later restore unrestricted root hit traversal.
        ui.set_enabled(*overlay_wrapper, false);
        auto parsed = ui.parse_template(overlay_wrapper, std::as_bytes(std::span{ overlay_template }));
        if (!parsed || !*parsed || (*parsed)->id != 200000)
            throw std::runtime_error("Original ending overlay template failed to parse");
        overlay = *parsed;
        caption(*overlay, text(row), 0);
        overlay->secondary_shadow_text = true;
        overlay->horizontal_alignment = overlay->vertical_alignment = 1;
        layout_overlay();
    }

    void start_scene(ApplicationEndingScene kind, ParkObjectRuntimeView& object) {
        if (scene_kind)
            close();
        c.ports.camera.restore_world_view_from_special_modes(c);
        c.ports.meshes.prepare_park_object_animation(c, &object, 0, 0);
        host.command_families_enabled(false);
        host.prepare_scene();
        host.persistent_visibility(false, false);
        scene_kind = kind;
        scene = &ui.create(nullptr, UiControlKind::window, 1, 29810, { 0, 0, 2047, 1535 });
        ui.set_primary_callback(*scene, [this](auto& sender, const auto& event) {
            return scene_event(sender, event);
        });
        cancel_started = static_cast<std::uint32_t>(ui.clock().count());
        const auto row = kind == ApplicationEndingScene::congratulations ? 923u
                       : kind == ApplicationEndingScene::bankruptcy      ? 921u
                                                                         : 922u;
        show_overlay(row);
        ui.set_input_root(scene);
        ui.set_focus(scene);
    }

    void failure(ApplicationEndingScene kind) {
        auto* world = c.referenced_world();
        auto* gate = world && world->saved.park_gates ? c.entities.at(world->saved.park_gates).payload : nullptr;
        if (!gate || gate->kind != ThingKind::park_object || gate->retirement_pending)
            throw std::runtime_error("Native failure transition requires the real Park Gates");
        //649E60: state4 BEFORE the camera/UI callbacks; no generic pause or
        //retirement sweep substitutes for this transition state.
        world->saved.state = WorldState::transition;
        start_scene(kind, *reinterpret_cast<ParkObjectRuntimeView*>(gate));
        c.resources.sprite_resource_rendering_enabled = 0;
        for (auto* link = c.entities.active_head; link; link = link->active_next) {
            auto* record = link->payload;
            if (record && !record->retirement_pending && record->kind == ThingKind::park_object) {
                auto& object = *reinterpret_cast<ParkObjectRuntimeView*>(record);
                if ((std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::is_choosable))
                    != 0)
                    set_rsse_script_state_slot(c, find_rsse_script_state_by_id(c, object.script_id), 4, 1);
            }
        }
        drain_pending_world_used_thing_removals(c);
        host.failure_audio();
        handle_world_park_open_close(c, *world, false, true);
    }

    void congratulations() {
        if (scene_kind)
            close();
        const ResearchCatalogDefinition* entry{};
        for (auto* candidate : c.resources.object_catalog.entries)
            if (candidate && is_end_name(candidate->resource_catalog_name)) {
                entry = candidate;
                break;
            }
        if (!entry)
            throw std::runtime_error("Original victory catalog has no End resource");
        auto* storage = static_cast<ParkObjectRuntimeView*>(
            c.entities.allocate_payload(c.ports.memory, sizeof(ParkObjectRuntimeView)));
        if (!storage)
            throw std::bad_alloc{};
        std::construct_at(storage);
        ParkObjectRuntimeView* object{};
        try {
            object = world_operations::initialize_park_object_instance(c, storage, 1, entry->id, 0, 0, 0);
            if (object != storage)
                throw std::runtime_error("End constructor lost its original allocation");
        } catch (...) {
            const auto id = storage->base.header.id;
            if (!id || id >= c.entities.size() || c.entities[id].payload != &storage->base.header) {
                std::destroy_at(storage);
                c.entities.release_payload(c.ports.memory, storage);
            }
            throw;
        }
        ending_object = object->base.header.id;
        ending_identity = &object->base.header;
        ending_creation_time = object->creation_file_time;
        c.messages.congratulations_popup = ending_object;
        start_scene(ApplicationEndingScene::congratulations, *object);
    }

    void finish_notice(bool invoke_action) {
        const auto key = mail;
        const auto tag = presentation.tag_id;
        close_notice();
        if (tag == 93)
            congratulations();
        else if (tag == 94)
            failure(ApplicationEndingScene::bankruptcy);
        else if (tag == 95)
            failure(ApplicationEndingScene::failed_challenges);
        //Native43016 reloads and dispatches the mail action AFTER the scene
        //transition. Other close buttons and event5 perform no mail action.
        if (invoke_action && key)
            if (auto* message = find_ui_mail(c, *key))
                if (const auto action = resolve_world_mail_ui_action(c, *message))
                    host.mail_action(*action);
    }

    std::intptr_t notice_event(UiControl& control, const UiEvent& event) {
        if (closing)
            return ui.secondary(control, event);
        if (event.type == 20 && &control == notice) {
            notice = nullptr;
            mail.reset();
            if (host.panels.deactivate)
                host.panels.deactivate(&control);
            restore_focus();
        } else if (event.type == 5 || escape(event)) {
            finish_notice(false);
            return 1;
        } else if (event.type == 256) {
            if (event.id != 43015)
                finish_notice(event.id == 43016);
            return 0;
        }
        return ui.secondary(control, event);
    }

    std::intptr_t scene_event(UiControl& control, const UiEvent& event) {
        if (event.type == 20 && &control == scene) {
            scene = nullptr;
            return ui.secondary(control, event);
        }
        if (event.type == 21)
            cancel_started = static_cast<std::uint32_t>(ui.clock().count());
        const bool right_click =
            event.type == 65542
            && event.id == 1
            && host.right_mouse_cancel_enabled()
            && std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(ui.clock().count()) - cancel_started) > 2000;
        if (!closing && scene_kind && (right_click || escape(event))) {
            const bool victory = *scene_kind == ApplicationEndingScene::congratulations;
            destroy(scene);
            if (victory) {
                close_overlay();
                retire_end();
            }
            c.ports.camera.restore_world_view_from_special_modes(c);
            host.persistent_visibility(true, false);
            host.command_families_enabled(true);
            scene_kind.reset();
            ui.set_input_root(primary);
            restore_focus();
            //Native50ACE0 deliberately retains the BANKRUPT/Fired wide overlay;
            //50AE00 explicitly removes it only for congratulations.
            return 0;
        }
        return event.type == 65547 ? 0 : ui.secondary(control, event);
    }

    void advance() {
        auto notices = std::move(c.messages.modal_mail_notices);
        c.messages.modal_mail_notices.clear();
        for (const auto& pending : notices) {
            std::optional<WorldMailIdentity> identity;
            for (const auto& row : world_mail_ui_rows(c)) {
                auto* message = find_ui_mail(c, row.key);
                if (message
                    && text_view(message->text_buffer_3) == pending.heading
                    && text_view(message->rendered_message) == pending.body
                    && text_view(message->text_buffer_1) == pending.sender
                    && text_view(message->text_buffer_2) == pending.recipient
                    && (!identity || row.key.sequence > identity->sequence))
                    identity = row.key;
            }
            if (!identity)
                throw std::runtime_error("Original category25 notice has no " "corresponding live mailbox record");
            open(*identity);
        }
        layout_overlay();
    }
};

ApplicationEndingUiController::ApplicationEndingUiController(WorldRuntime& c,
                                                             UiRuntime& ui,
                                                             UiControl* primary,
                                                             const std::filesystem::path& root,
                                                             ApplicationEndingUiHost host,
                                                             std::string language)
    : state_(std::make_unique<State>(c, ui, primary, root, std::move(host), std::move(language))) {}

ApplicationEndingUiController::~ApplicationEndingUiController() = default;

void ApplicationEndingUiController::advance() {
    state_->advance();
}

void ApplicationEndingUiController::open(WorldMailIdentity identity) {
    state_->open(identity);
}

void ApplicationEndingUiController::close() {
    state_->close();
}

bool ApplicationEndingUiController::is_open() const {
    return state_->notice || state_->scene || state_->overlay;
}

bool ApplicationEndingUiController::scene_active() const {
    return state_->scene != nullptr;
}

UiControl* ApplicationEndingUiController::root() const {
    return state_->scene ? state_->scene : state_->notice;
}
} //namespace simcoaster
