#include "simcoaster/require_bound.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "simcoaster/advisor_speech.hpp"
#include "simcoaster/advisor_ui_scene.hpp"
#include "simcoaster/park_construction.hpp"
#include "simcoaster/standard_balance.hpp"
#include "simcoaster/ui_assets.hpp"
#include "simcoaster/ui_control_presenter.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/ui_preferences.hpp"
#include "simcoaster/ui_tooltip.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_advisor.hpp"
#include "simcoaster/world_analyser.hpp"
#include "simcoaster/world_animation.hpp"
#include "simcoaster/world_camera_picker.hpp"
#include "simcoaster/world_challenge.hpp"
#include "simcoaster/world_events.hpp"
#include "simcoaster/world_mail.hpp"
#include "simcoaster/world_mesh_runtime.hpp"
#include "simcoaster/world_mission_controller.hpp"
#include "simcoaster/world_park_admission.hpp"
#include "simcoaster/world_person.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_staff.hpp"
#include "simcoaster/world_theme.hpp"
#include "simcoaster/world_ui_build.hpp"
#include "simcoaster/world_ui_coaster_editor.hpp"
#include "simcoaster/world_ui_mail.hpp"
#include "simcoaster/world_ui_management.hpp"
#include "simcoaster/world_ui_objects.hpp"
#include "simcoaster/world_ui_shortcuts.hpp"
#include "simcoaster/world_ui_staff.hpp"
#include "simcoaster/world_ui_visitors.hpp"
#include "simcoaster/text_format.hpp"
#include <format>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>

namespace simcoaster {
namespace {
constexpr unsigned char primary_template[] {
#embed "ui_templates/persistent_primary.bin"
};
constexpr unsigned char secondary_template[] {
#embed "ui_templates/persistent_secondary.bin"
};
constexpr unsigned char objectives_template[] {
#embed "ui_templates/objectives.bin"
};

template <class T>
T* record(WorldRuntime& c, ThingId id) {
    return id && id < c.entities.size() ? c.entities.payload<T>(id) : nullptr;
}

std::array<float, 4> bounds(UiRect r) {
    return { static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right), static_cast<float>(r.bottom) };
}

UiRect clipped(UiRect a, UiRect b) {
    return { std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom) };
}

constexpr std::array<std::uint32_t, 7> action_ids { 54, 55, 56, 57, 58, 59, 61 };

struct MenuRow {
    std::uint32_t text;
    WorldUiCommand command;
};

//54FFF0/54F4F0: fixed seven physical action slots; zero entries are hidden.
constexpr std::array<std::array<MenuRow, 7>, 6> menu_rows { { { { { 0, {} }, { 0, {} }, { 0x33B, WorldUiCommand::staff_hire }, { 0x33D, WorldUiCommand::staff_list }, { 0x33C, WorldUiCommand::training_room }, { 0x33E, WorldUiCommand::staff_locator }, { 0x33F, WorldUiCommand::staff_finance } } },
                                                              { { { 0, {} }, { 0, {} }, { 0, {} }, { 0x341, WorldUiCommand::attraction_build }, { 0x343, WorldUiCommand::attraction_list }, { 0x342, WorldUiCommand::research }, { 0x344, WorldUiCommand::attraction_finance } } },
                                                              { { { 0, {} }, { 0, {} }, { 0, {} }, { 0, {} }, { 0x348, WorldUiCommand::visitor_statistics }, { 0x346, WorldUiCommand::visitor_list }, { 0x347, WorldUiCommand::visitor_locator } } },
                                                              { { { 0, {} }, { 0, {} }, { 0, {} }, { 0, {} }, { 0x34A, WorldUiCommand::financial_statistics }, { 0x34B, WorldUiCommand::loans }, { 0x34C, WorldUiCommand::shares } } },
                                                              { { { 0, {} }, { 0x34F, WorldUiCommand::park_gates }, { 0x352, WorldUiCommand::park_awards }, { 0x34E, WorldUiCommand::park_statistics }, { 0x350, {} }, { 0x351, WorldUiCommand::challenges }, { 0x257, {} } } },
                                                              { { { 0, {} }, { 0, {} }, { 0, {} }, { 0, {} }, { 0, {} }, { 0x354, WorldUiCommand::park_map }, { 0x355, WorldUiCommand::overview_map } } } } };
//Native9BB558 six RGBA rows, independently read from the executable.
constexpr std::array<RgbaPixel, 6> cash_colors { { { 0xFF, 0, 0, 0xFF }, { 0xFA, 0x5A, 0, 0xFF }, { 0xFF, 0xAF, 0, 0xFF }, { 0xFF, 0xC8, 0, 0xFF }, { 0xFF, 0xFF, 0x96, 0xFF }, { 0xFF, 0xFF, 0xFF, 0xFF } } };
} //namespace

bool world_camera_key_matches(std::uint32_t packed_key, std::uint32_t modifiers) {
    const auto key = packed_key > 255 ? (packed_key >> 8) & 255u : packed_key;
    //Wildcard64 permits any modifiers for Up/Down and the four numpad pan
    //rows. Home/rotation/numpad zoom use zero; Space uses exact63.
    return key == 38 || key == 40 || key == 98 || key == 100 || key == 102 || key == 104 || (!modifiers && (key == 36 || key == 37 || key == 39 || key == 107 || key == 109)) || (key == 32 && modifiers == 63);
}

struct WorldUiRuntime::State {
    WorldRuntime& c;
    std::filesystem::path root;
    std::function<void()> reset_world_interaction;
    UiRuntime ui;
    UiControl *primary {}, *secondary {}, *panel {}, *playback {}, *highlight {};
    std::uint64_t playback_serial {};
    std::unique_ptr<UiAssetLibrary> assets;
    std::unique_ptr<UiFontLibrary> fonts;
    std::unique_ptr<UiControlPresenter> presenter;
    std::unique_ptr<UiTooltipController> tooltip;
    std::unique_ptr<AdvisorUiScene> advisor_scene;
    MeshLighting advisor_lighting;
    std::string advisor_lighting_source;
    bool advisor_lighting_bound {};
    std::function<void(UiDrawList&, std::uint32_t, std::uint32_t)> draw_underlay;
    std::function<void(UiDrawList&, const UiControl&, UiRect, std::uint32_t, std::uint32_t)> control_draw_hook;
    std::function<bool(const UiEvent&, WorldUiShortcutRoute)> shortcut_handler;
    std::optional<std::u16string> tooltip_override;
    std::u16string world_cursor_help;
    std::optional<UiPoint> tooltip_offsets;
    ParkConstructionRuntime* construction_runtime {};
    bool tooltips_enabled = true;
    std::unique_ptr<WorldStaffUiController> staff;
    std::unique_ptr<WorldParkObjectUiController> objects;
    std::unique_ptr<WorldBuildUiController> build;
    std::unique_ptr<WorldCoasterEditorUiController> coaster;
    std::unique_ptr<WorldVisitorsUiController> visitors;
    std::unique_ptr<WorldMailUiController> mail;
    std::unique_ptr<WorldManagementUiController> management;
    WorldUiPanelHost panels;
    std::function<bool(std::uint16_t)> begin_object_build;
    std::function<bool(const std::filesystem::path&, std::int32_t)> begin_coaster_design;
    std::function<void(ThingId)> retire_authoring;
    std::function<bool(ThingId)> edit_object_queue, edit_object_track;
    std::function<bool(ThingId, CoasterEditorEntryTool)> edit_coaster_tool;
    std::function<bool(ThingId)> relocate_object;
    std::function<void(ThingId)> ride_object;
    std::function<bool(std::uint32_t, bool)> select_toolbar_tool;
    TextureImagePtr gradient;
    std::map<std::string, std::vector<std::byte>, std::less<>> language;
    std::vector<std::string> diagnostics;
    std::function<void(const WorldUiAction&)> action_handler;
    std::vector<WorldUiAction> pending_actions;
    std::array<std::uint32_t, 2> viewport_pixels { 1024, 768 };
    std::chrono::milliseconds menu_started {}, next_indicator {};
    UiRect menu_bounds {};
    std::int32_t menu_target_top {};
    int category {}, menu_transition {}, research_countdown {}, challenge_step = 1;
    bool research_animating {}, hidden {}, details {}, closing {};
    std::function<bool()> gameplay_paused;
    std::function<void()> frame_poll;
    bool frame_polled {};
    UiHelpQuery happiness_help;
    std::u16string happiness_help_text;

    struct MailAnimation {
        bool active {};
        int kind {};
        std::size_t step {};
        std::chrono::milliseconds interval {}, last {};
    } mail_animation;

    std::uint32_t cash_events {};
    std::int32_t interaction_cost = -999;
    bool objectives_text_refresh_suppressed {};
    bool objectives_staff_initialized {};
    std::u16string objectives_text;

    State(WorldRuntime& context, const std::filesystem::path& path, std::function<void()> reset_interaction, ParkConstructionRuntime* construction) : c(context), root(path), reset_world_interaction(std::move(reset_interaction)), construction_runtime(construction) {
        if (!reset_world_interaction) {
            throw std::invalid_argument("World UI requires its interaction owner");
        }
        //509E7A installs500CA0 for the shared runtime, including transient pages.
        ui.set_observer([this](UiControl* control, const UiEvent& event) {
            if (event.type == 256 && ui.owns(control)) {
                (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, (control->flags & 16u) ? 189u : 31u, 0, 0, 0);
            }
        });
        primary = &ui.create(nullptr, UiControlKind::window, 1, 0, { 0, 0, 2047, 1535 });
        secondary = &ui.create(nullptr, UiControlKind::window, 0, 1, { 0, 0, 2047, 1535 });
        secondary->cursor_id = 21;
        const auto parsed = ui.parse_template(primary, std::as_bytes(std::span { primary_template }));
        if (!parsed) {
            throw std::runtime_error(std::format("Persistent primary UI template word {}: {}", parsed.error().word, parsed.error().reason));
        }
        //50A210 parses this separate persistent tree under the initially hidden
        //secondary canvas;39700 owns the two special-view camera commands.
        const auto secondary_parsed = ui.parse_template(secondary, std::as_bytes(std::span { secondary_template }), [this](UiControl& control, const UiEvent& event) {
            return camera_panel_event(control, event);
        });
        if (!secondary_parsed) {
            throw std::runtime_error(std::format("Persistent secondary UI template word {}: {}", secondary_parsed.error().word, secondary_parsed.error().reason));
        }
        //The existing World language owner loads American;50A210 suppresses
        //this language-specific secondary image for that same edition.
        ui.set_visible(required(&required(secondary, 39700), 39703), false);
        c.ui.persistent_root = primary;
        c.ui.interaction_toolbar = &required(primary, 62);
        c.ui.mail_counter_root = &required(primary, 69);
        menu_bounds = required(primary, 52).rect;
        initialize();
        c.advisor.presentation.attachment_controls_present = true;
        //54D770 ends with54D4D0(-3): template117/118/119 are all visible,
        //but neither portrait nor replay button should remain visible at boot.
        (void)set_advisor_attachment_part(c, -3);
        refresh_advisor_attachment();
        StaffUiPanelHost staff_host;
        staff_host.dismiss = [this] {
            dismiss_panels();
        };
        staff_host.request = [this](const WorldUiAction& action) {
            request(action);
        };
        staff_host.focus_staff = [this](ThingId id, bool follow) {
            request(WorldStaffFocusAction { .id = id, .follow = follow ? 1 : 0 });
        };
        staff_host.focus_object = [this](ThingId id) {
            request(WorldObjectFocusAction { .id = id });
        };
        staff_host.retiring = [this](ThingId id) {
            if (retire_authoring) {
                retire_authoring(id);
            }
            //Invalidate queued receiver actions synchronously before ID reuse,
            //including reuse within the same simulation calendar timestamp.
            std::erase_if(pending_actions, [id](const auto& action) {
                return world_ui_action_targets(action, id);
            });
        };
        const auto camera_key = [this](const UiEvent& event) {
            const auto modifiers = (static_cast<std::uint32_t>(event.value) >> 16) & 255u;
            const bool recognized = world_camera_key_matches(event.id, modifiers);
            if (recognized) {
                request(WorldStaffCameraAction { static_cast<std::int32_t>(event.id), event.type });
            }
            return recognized;
        };
        staff_host.camera_key = [this, camera_key](const UiEvent& event) {
            return shortcut_handler ? shortcut_handler(event, WorldUiShortcutRoute::panel) : camera_key(event);
        };
        const UiCallback camera_root = [this, camera_key](UiControl& control, const UiEvent& event) {
            if (event.type == 65546 || event.type == 65547) {
                const bool has_panel = &control != secondary && c.ui.active_root && ui.owns(c.ui.active_root) && c.ui.active_root->visible();
                const bool custom_camera_panel = has_panel && (c.ui.active_root->id == 97 || c.ui.active_root->id == 32000);
                if ((!has_panel || custom_camera_panel || !shortcut_handler) && camera_key(event)) {
                    return std::intptr_t { 1 };
                }
                if (shortcut_handler && shortcut_handler(event, has_panel ? WorldUiShortcutRoute::panel : &control == secondary ? WorldUiShortcutRoute::secondary : WorldUiShortcutRoute::primary)) {
                    return std::intptr_t { 1 };
                }
            }
            return ui.secondary(control, event);
        };
        ui.set_primary_callback(*primary, camera_root);
        ui.set_primary_callback(*secondary, camera_root);
        staff_host.activate = [this](UiControl* control) {
            activate_staff_panel(control);
        };
        staff_host.deactivate = [this](UiControl* control) {
            if (!control) {
                ui.set_focus(c.ui.active_root && ui.owns(c.ui.active_root) ? c.ui.active_root : secondary->visible() ? secondary : primary);
                return;
            }
            if (c.ui.active_root == control) {
                c.ui.active_root = nullptr;
                if (c.simulation.current) {
                    objectives_advisor(c.simulation.current).panel_up = 0;
                }
            }
        };
        panels = staff_host;
        panels.activate = [this](UiControl* control) {
            activate_panel(control);
        };
        panels.focus_person = [this](ThingId id, bool follow) {
            request(WorldPersonFocusAction { .id = id, .follow = follow ? 1 : 0 });
        };
        staff = std::make_unique<WorldStaffUiController>(c, ui, primary, root, std::move(staff_host));
        if (construction) {
            WorldUiPanelHost object_host = panels;
            object_host.construction = construction;
            object_host.native_coaster_editor = c.construction.editor;
            object_host.camera_key = camera_key;
            object_host.request = [this](const WorldUiAction& action) {
                request(action);
            };
            object_host.activate = [this](UiControl* control) {
                activate_panel(control);
            };
            object_host.prepare_context_menu = [this] {
                //554DC5 disables the retained active root before501490 resets World
                //input. The auxiliary popup never becomes a registered main panel.
                if (coaster) {
                    coaster->close();
                }
                reset_world_interaction();
                select_toolbar_presentation(67);
                primary->cursor_id = 0;
                request(WorldResetInteractionAction {});
            };
            object_host.open_objectives = [this] {
                open_objectives();
            };
            object_host.synchronize_focus = [this] {
                ui.set_focus(c.ui.active_root && ui.owns(c.ui.active_root) ? c.ui.active_root : secondary->visible() ? secondary : primary);
            };
            object_host.deactivate = [this](UiControl* control) {
                if (control && c.ui.active_root == control) {
                    c.ui.active_root = nullptr;
                    if (c.simulation.current) {
                        objectives_advisor(c.simulation.current).panel_up = 0;
                    }
                }
                //Native507FA0 event20 synchronizes focus as soon as the active
                //panel retires. Build closes with its old root, without a second
                //nullptr notification; waiting for one left placement keys unrouted.
                ui.set_focus(c.ui.active_root && ui.owns(c.ui.active_root) ? c.ui.active_root : secondary->visible() ? secondary : primary);
            };
            object_host.focus_object = [this](ThingId id) {
                request(WorldObjectFocusAction { .id = id });
            };
            object_host.retiring = [this](ThingId id) {
                if (retire_authoring) {
                    retire_authoring(id);
                }
                std::erase_if(pending_actions, [id](const auto& action) {
                    return world_ui_action_targets(action, id);
                });
            };
            object_host.begin_object_build = [this](std::uint16_t definition) {
                if (!begin_object_build) {
                    throw std::logic_error("Original Build UI has no construction owner");
                }
                std::erase_if(pending_actions, [](const auto& action) {
                    return std::holds_alternative<WorldResetInteractionAction>(action);
                });
                return begin_object_build(definition);
            };
            object_host.begin_coaster_design = [this](const auto& design_path, std::int32_t cost) {
                if (!begin_coaster_design) {
                    throw std::logic_error("Original coaster picker has no construction owner");
                }
                std::erase_if(pending_actions, [](const auto& action) {
                    return std::holds_alternative<WorldResetInteractionAction>(action);
                });
                return begin_coaster_design(design_path, cost);
            };
            object_host.open_object = [this](ThingId id) {
                objects->open_object(id);
            };
            object_host.open_staff_room = [this](ThingId id) {
                staff->open_room(id);
            };
            object_host.open_training_room = [this](ThingId id) {
                staff->open_training(id);
            };
            object_host.edit_object_queue = [this](ThingId id) {
                if (!edit_object_queue) {
                    throw std::logic_error("Park-object queue editor is not bound");
                }
                supersede_authoring_reset();
                return edit_object_queue(id);
            };
            object_host.edit_object_track = [this](ThingId id) {
                if (!edit_object_track) {
                    throw std::logic_error("Park-object track editor is not bound");
                }
                supersede_authoring_reset();
                return edit_object_track(id);
            };
            object_host.open_coaster_tool = [this](ThingId id, CoasterEditorEntryTool tool) {
                if (!edit_coaster_tool) {
                    throw std::logic_error("Coaster context tool has no construction owner");
                }
                supersede_authoring_reset();
                (void)edit_coaster_tool(id, tool);
            };
            object_host.delete_coaster_geometry = [this](ThingId id, std::uint32_t mesh) {
                supersede_authoring_reset();
                if (coaster) {
                    coaster->delete_context_geometry(id, mesh);
                }
            };
            object_host.ride_object = [this](ThingId id) {
                if (!ride_object) {
                    throw std::logic_error("Park-object inspection is not bound");
                }
                supersede_authoring_reset();
                ride_object(id);
            };
            object_host.relocate_object = [this](ThingId id) {
                if (!relocate_object) {
                    throw std::logic_error("Park-object relocation is not bound");
                }
                supersede_authoring_reset();
                return relocate_object(id);
            };
            auto build_host = object_host;
            auto coaster_host = object_host;
            panels = object_host;
            object_host.dismiss = [this] {
                dismiss_panels();
            };
            objects = std::make_unique<WorldParkObjectUiController>(c, ui, primary, root, std::move(object_host));
            build_host.dismiss = [this] {
                dismiss_panels();
            };
            build = std::make_unique<WorldBuildUiController>(c, ui, primary, root, std::move(build_host));
            if (c.construction.editor) {
                coaster_host.set_editor_toolbar_active = [this](bool active) {
                    ui.set_visible(required(primary, c.mode.game == GameMode::code_3 ? 111u : 62u), !active);
                };
                coaster_host.synchronize_focus = [this] {
                    ui.set_focus(coaster && coaster->open() ? coaster->root() : c.ui.active_root && ui.owns(c.ui.active_root) ? c.ui.active_root : secondary->visible() ? secondary : primary);
                };
                coaster = std::make_unique<WorldCoasterEditorUiController>(c, ui, primary, root, std::move(coaster_host));
            }
        }
        panels.camera_key = [this, camera_key](const UiEvent& event) {
            return shortcut_handler ? shortcut_handler(event, WorldUiShortcutRoute::panel) : camera_key(event);
        };
        visitors = std::make_unique<WorldVisitorsUiController>(c, ui, primary, root, panels);
        WorldMailUiHost mail_host;
        mail_host.panels = panels;
        mail_host.command_menu_visible = [this] {
            return category != 0;
        };
        mail_host.dispatch = [this](const WorldMailUiAction& action) {
            dispatch_mail(action);
        };
        mail = std::make_unique<WorldMailUiController>(c, ui, primary, root, std::move(mail_host));
        c.ui.panels.park_object_event = [this](WorldRuntime&, const WorldEvent& event) {
            if (objects) {
                objects->handle_event(event);
            }
            if (coaster) {
                coaster->handle_event(event);
            }
            visitors->handle_event(event);
            mail->handle_event(event);
            if (management) {
                management->handle_event(event);
            }
        };
        ui.drain();
        refresh_hud(true);
        //50A210 ends by synchronizing focus: the visible primary root owns
        //keyboard routing whenever there is no active panel or secondary view.
        ui.set_focus(primary);
    }

    ~State() {
        closing = true;
        advisor_scene.reset();
        tooltip.reset();
        c.ui.panels.park_object_event = {};
        management.reset();
        mail.reset();
        visitors.reset();
        coaster.reset();
        build.reset();
        objects.reset();
        staff.reset();
        c.advisor.presentation.attachment_controls_present = false;
        c.advisor.presentation.attachment_controls_visible = false;
        c.advisor.presentation.replay_enabled = false;
        if (c.ui.active_root && ui.owns(c.ui.active_root)) {
            c.ui.active_root = nullptr;
        }
        if (c.ui.persistent_root == primary) {
            c.ui.persistent_root = nullptr;
        }
        if (c.ui.interaction_toolbar && ui.owns(c.ui.interaction_toolbar)) {
            c.ui.interaction_toolbar = nullptr;
        }
        if (c.ui.mail_counter_root && ui.owns(c.ui.mail_counter_root)) {
            c.ui.mail_counter_root = nullptr;
        }
        c.ui.panels.visitor_profile.close_panel = {};
        c.ui.panels.ride_settings.refresh_statistics_and_status = {};
        c.ui.panels.visitor_profile.open = false;
        c.ui.panels.ride_settings.open = false;
        c.ui.panels.staff_room.open = false;
        c.ui.panels.training_room.open = false;
        c.ui.panels.visitor_list.open = false;
        c.ui.panels.visitor_locator.open = false;
        ui.clear();
    }

    UiControl& required(UiControl* parent, std::uint32_t id) {
        auto* control = ui.find(parent, id);
        if (!control) {
            throw std::runtime_error(std::format("Original UI template missing direct control {}", id));
        }
        return *control;
    }

    UiControl* any(std::uint32_t id) {
        return ui.find_recursive(primary, id);
    }

    std::u16string localized(std::span<const std::byte> table, std::uint32_t row, std::span<const BfstSignedIntegerArgument> integers = {}, std::span<const BfstWideStringArgument> strings = {}) {
        auto text = format_bfst_wide_string(table, c.messages.mail_language.conversion, row, 16383, strings, integers);
        if (!text) {
            throw std::runtime_error(std::format("World UI localized row {}: {}", row, text.error().reason));
        }
        return std::move(*text);
    }

    std::u16string text(std::uint32_t row) {
        return localized(c.messages.mail_language.ui_text, row);
    }

    std::u16string signed_cash_text(std::int32_t value) {
        //5133C0's signed-currency attachment uses the installed BFST templates.
        //NEG is native32-bit two's-complement, including its INT_MIN result.
        const bool negative = value < 0;
        const auto magnitude = negative ? std::bit_cast<std::int32_t>(0u - static_cast<std::uint32_t>(value)) : value;
        const std::array argument { BfstSignedIntegerArgument { negative ? 44u : 39u, magnitude } };
        return localized(c.messages.mail_language.ui_text, negative ? 958u : 957u, argument);
    }

    const std::vector<std::byte>& table(std::string name) {
        if (auto found = language.find(name); found != language.end()) {
            return found->second;
        }
        const auto path = (root / "Data/Language/American" / name).native_encoded_string();
        const auto bytes = c.ports.assets.read_balance_asset(c, path);
        if (!bytes) {
            throw std::runtime_error(std::format("Missing World UI language resource: {}", path));
        }
        const auto raw = std::as_bytes(std::span { bytes->data(), bytes->size() });
        return language.emplace(std::move(name), std::vector<std::byte> { raw.begin(), raw.end() }).first->second;
    }

    void request(WorldUiAction action) {
        match (action) {
            case { auto& request } => {
                using Request = std::remove_cvref_t<decltype(request)>;
                if constexpr (std::derived_from<Request, WorldEntityTarget>) {
                    if (!request.identity && request.id > 0 && static_cast<std::size_t>(request.id) < c.entities.size()) {
                        using Payload = std::conditional_t<std::same_as<Request, WorldPersonFocusAction>, PersonRuntimeView, std::conditional_t<std::same_as<Request, WorldStaffFocusAction>, StaffRuntimeRecord, ParkObjectRuntimeView>>;
                        auto* target = find_live_entity<Payload>(c.entities, request.id);
                        if (!target) {
                            return;
                        }
                        if constexpr (std::same_as<Request, WorldPersonFocusAction>) {
                            request.identity_generation = (std::uint64_t { target->arrival_date } << 32) | target->arrival_index;
                        } else {
                            request.identity_generation = entity_identity_generation(target->base.header);
                        }
                        request.identity = &target->base.header;
                    }
                }
            }
        } if (action_handler) {
            action_handler(action);
        } else {
            pending_actions.push_back(action);
        }
    }

    void command(WorldUiCommand command) {
        if (staff && staff->command(command)) {
            return;
        }
        if (objects && objects->command(command)) {
            return;
        }
        if (build && build->command(command)) {
            return;
        }
        if (visitors && visitors->command(command)) {
            return;
        }
        if (mail && mail->command(command)) {
            return;
        }
        if (management && management->command(command)) {
            return;
        }
        request(WorldCommandAction { command, 0 });
    }

    void dismiss_panels() {
        if (coaster) {
            coaster->close();
        }
        if (staff) {
            staff->close();
        }
        if (objects) {
            objects->close();
        }
        if (build) {
            build->close();
        }
        if (visitors) {
            visitors->close();
        }
        if (mail) {
            mail->close();
        }
        if (management) {
            management->close();
        }
        close_panel();
    }

    void dispatch_mail(const WorldMailUiAction&);

    void supersede_authoring_reset() {
        //The live construction owner has taken a newer explicit operation.
        //A prior page-open reset must not cancel it when the host drains actions.
        std::erase_if(pending_actions, [](const auto& action) {
            return std::holds_alternative<WorldResetInteractionAction>(action);
        });
    }

    void select_toolbar_presentation(std::uint32_t id) {
        ui.set_selected(required(&required(primary, 62), 64), static_cast<std::int32_t>(id), false);
        if (c.mode.game == GameMode::code_3) {
            auto& button = required(&required(primary, 111), 113);
            //Native501490 resets only selected bit1, preserving hover/press bits.
            const auto flags = (button.visual_flags & ~1u) | (id == 65 ? 1u : 0u);
            ui.set_value(button, static_cast<std::int32_t>(flags), false);
        }
    }

    void toolbar_selection(std::uint32_t id, bool selected) {
        if (id < 65 || id > 68) {
            return;
        }
        if (selected) {
            //Native54B780 dismisses the current page before replacing its World
            //handler. Do this in the portable owner, including headless clients.
            dismiss_panels();
        }
        if (select_toolbar_tool) {
            supersede_authoring_reset();
            if (select_toolbar_tool(id, selected)) {
                return;
            }
        }
        if (selected) {
            request(WorldToolbarAction { static_cast<std::int32_t>(id) });
        }
    }

    void activate_staff_panel(UiControl* control) {
        if (!ui.owns(control)) {
            return;
        }
        constexpr std::array main_ids { 4000u, 30000u, 7000u, 32000u, 31000u, 16000u, 26000u, 9000u, 631628u, 729u };
        if (std::ranges::find(main_ids, control->id) == main_ids.end()) {
            return; //Auxiliary roots retain the active page and its World handler.
        }
        activate_panel(control);
    }

    void activate_panel(UiControl* control) {
        if (!ui.owns(control)) {
            return;
        }
        if (c.ui.active_root != control) {
            close_panel();
            reset_world_interaction();
            select_toolbar_presentation(67);
            primary->cursor_id = 0;
            //The portable reset completed synchronously. The host only drops its
            //old gesture bookkeeping; it must not choose/cancel the tool again.
            request(WorldResetInteractionAction {});
            c.ui.active_root = control;
            if (c.simulation.current) {
                objectives_advisor(c.simulation.current).panel_up = 1;
            }
        }
        ui.set_focus(control);
    }

    std::intptr_t camera_panel_event(UiControl& control, const UiEvent& event) {
        //507ED0:39701 enters postcard mode;39702 exits the active special view.
        //The backend preserves native POI bit2 priority over inspection bit4.
        //Bit16 alone still performs the cursor sequence without a mode exit.
        if (event.type == 256) {
            if (event.id == 39701) {
                command(WorldUiCommand::postcard);
            } else if (event.id == 39702 && (c.view.flags & 0x16u)) {
                control.cursor_id = 1;
                c.ports.camera.restore_world_view_from_special_modes(c);
                control.cursor_id = 21;
            }
        }
        return ui.secondary(control, event);
    }

    void caption(UiControl& control, std::u16string_view value, std::uint32_t font = 6, int horizontal = 1, int vertical = 1) {
        control.font = font;
        control.horizontal_alignment = horizontal;
        control.vertical_alignment = vertical;
        ui.set_text(control, value);
    }

    void initialize() {
        required(primary, 119).help_index = 279;
        auto& cash = required(primary, 93);
        cash.has_primary_text_attachment = true;
        cash.font = 1;
        cash.horizontal_alignment = 0;
        cash.vertical_alignment = 1;
        ui.set_depth(cash, 0);
        ui.set_enabled(cash, false);
        auto& cost = required(&cash, 94);
        cost.has_primary_text_attachment = true;
        cost.font = 2;
        cost.horizontal_alignment = 2;
        cost.vertical_alignment = 1;
        cost.color = { 255, 250, 0, 255 };
        ui.set_depth(cost, 0);
        cost.secondary_placement.depth = 2;
        cash.secondary_placement.depth = 2;
        ui.set_visible(cost, false);
        ui.set_enabled(cost, false);
        auto& status = required(primary, 91);
        status.font = 5;
        status.color = { 230, 230, 255, 250 };
        status.horizontal_alignment = 1;
        ui.set_enabled(status, false);
        auto& research = required(primary, 95);
        research.font = 6;
        research.color = { 0, 0, 0, 255 };
        research.horizontal_alignment = research.vertical_alignment = 1;
        research.help_index = 243;
        ui.set_visible(research, false);
        ui.set_depth(research, 0);
        auto& challenge = required(primary, 96);
        challenge.help_index = 241;
        ui.set_visible(challenge, false);
        auto& actions = required(primary, 52);
        ui.set_visible(actions, false);
        for (auto id : action_ids) {
            auto& button = required(&actions, id);
            button.font = 6;
            ui.set_visible(button, false);
        }
        auto& title = required(&actions, 53);
        title.font = 5;
        title.color = { 235, 235, 255, 255 };
        ui.set_visible(title, false);
        auto& categories = required(primary, 69);
        ui.set_visible(required(&categories, 79), false);
        auto& toolbar = required(primary, 62);
        ui.set_selected(required(&toolbar, 64), 67);
        auto& date = required(primary, 120);
        ui.set_depth(date, 1); //Native54E14C/54E317 also moves the options child.
        date.font = 6;
        date.text_constraints = { true, true };
        date.horizontal_alignment = date.vertical_alignment = 1;
        date.color = { 230, 230, 255, 255 };
        required(&date, 121).help_index = 6;
        required(&required(&required(primary, 111), 114), 115).text_constraints = { true, true };
        auto& mail_button = required(&categories, 70);
        mail_button.font = 6;
        mail_button.horizontal_alignment = mail_button.vertical_alignment = 1;
        caption(mail_button, u"0");
        ui.set_primary_callback(mail_button, [this](UiControl& control, const UiEvent& e) {
            if (e.type == 256 || e.type == 65542) {
                command(WorldUiCommand::mail);
                reset_menu();
            }
            return ui.secondary(control, e);
        });
        auto& happy = required(primary, 80);
        ui.set_depth(happy, 0);
        happy.minimum = 0;
        happy.maximum = 1024;
        ui.set_primary_callback(required(&happy, 81), [this](UiControl& control, const UiEvent& e) {
            if (e.type == 256 || e.type == 65542) {
                (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 31, 0, 0, 0);
                command(c.simulation.current && get_world_park_gates_rsse_slot1(c, *c.simulation.current) == 1 ? WorldUiCommand::visitor_statistics : WorldUiCommand::park_gates);
            }
            return ui.secondary(control, e);
        });
        ui.set_primary_callback(required(primary, 119), [this](UiControl& control, const UiEvent& e) {
            //Native54D480 is replay-last-response on pointer
            //release, rather than a general help command.
            if (e.type == 65540 && c.advisor.presentation.replay_enabled) {
                (void)world_operations::play_advisor_speech_response(c, c.advisor.presentation.response_id);
            }
            return ui.secondary(control, e);
        });
        auto& tickets = required(primary, 49);
        ui.set_depth(tickets, 0);
        auto& ticket_text = required(&tickets, 50);
        ticket_text.has_primary_text_attachment = true;
        ticket_text.font = 2;
        ticket_text.horizontal_alignment = 0;
        ticket_text.vertical_alignment = 1;
        ui.set_depth(ticket_text, 0);
        auto& camera = required(primary, 83);
        ui.set_depth(camera, 1);
        for (auto id : { 87u, 88u, 89u, 90u }) {
            auto& button = required(&camera, id);
            button.flags |= 32u;
            button.repeat_delay = std::chrono::milliseconds { 200 };
            button.repeat_interval = std::chrono::milliseconds { 10 };
        }
        //Parent chains use the widget implementation as their secondary handler;
        //original primary callbacks do their work and explicitly forward
        //afterward.
        for (auto id : { 52u, 62u, 69u, 83u, 120u, 80u, 95u, 96u, 49u, 111u }) {
            auto& owner = required(primary, id);
            ui.set_primary_callback(owner, [this](UiControl& control, const UiEvent& event) {
                return event_control(control, event);
            });
        }
        ui.set_primary_callback(cash, [this](UiControl& control, const UiEvent& e) {
            if (e.type == 30 && (cash_events++ & 3u) == 3u) {
                refresh_cash();
            }
            return ui.secondary(control, e);
        });
        ui.set_primary_callback(required(&mail_button, 71), [this](UiControl& control, const UiEvent& e) {
            if (e.type == 30 && mail_animation.active && ui.clock() - mail_animation.last > mail_animation.interval) {
                //Native9BB5E8/9BB6B8/9BB788:25 SET_PART records then STOP.
                //84CABD advances only one due record, using a strict time
                //comparison.
                constexpr std::array<std::uint16_t, 25> ordinary { 0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1, 0 };
                constexpr std::array<std::uint16_t, 25> priority { 0, 1, 2, 2, 2, 1, 0, 1, 2, 2, 2, 1, 0, 1, 2, 2, 2, 1, 0, 1, 2, 2, 2, 1, 0 };
                mail_animation.last = ui.clock();
                if (mail_animation.step == ordinary.size()) {
                    mail_animation.active = false;
                    ui.set_visible(control, false);
                } else {
                    const auto base = mail_animation.kind == 0 ? 3u : mail_animation.kind == 1 ? 6u : 0u;
                    const auto offset = mail_animation.kind == 2 ? ordinary[mail_animation.step] : priority[mail_animation.step];
                    ui.set_attachment_part(control, static_cast<std::uint16_t>(base + offset));
                    ++mail_animation.step;
                }
            }
            return ui.secondary(control, e);
        });
        if (c.mode.game == GameMode::code_3) {
            for (auto id : { 93u, 82u, 91u, 95u, 96u, 52u, 120u, 62u, 80u, 49u, 69u }) {
                ui.set_visible(required(primary, id), false);
            }
            auto& alternate = required(primary, 111);
            ui.set_visible(alternate, true);
            const auto normal = toolbar.rect;
            ui.translate(alternate, normal.left - alternate.rect.left, normal.top - alternate.rect.top);
            auto& group = required(&alternate, 114);
            ui.set_primary_callback(group, [this](UiControl& control, const UiEvent& e) {
                if (e.type == 256 && e.id == 115) {
                    command(WorldUiCommand::coaster_source);
                } else if (e.type == 256 && e.id == 116) {
                    command(WorldUiCommand::game_menu);
                }
                return ui.secondary(control, e);
            });
        } else {
            ui.set_visible(required(primary, 111), false);
        }
        next_indicator = std::chrono::milliseconds { 2000 };
    }

    void reset_menu() {
        category = 0;
        c.ui.persistent_menu_category = 0;
        menu_transition = 0;
        ui.set_visible(required(primary, 52), false);
        ui.set_selected(required(&required(primary, 69), 72), -1);
    }

    bool has_kind(ThingKind kind) const {
        for (auto* row = c.entities.active_head; row; row = row->active_next) {
            if (row->payload && !row->payload->retirement_pending && row->payload->kind == kind) {
                return true;
            }
        }
        return false;
    }

    bool has_capability(ParkObjectCapabilities capability) const {
        for (auto* row = c.entities.active_head; row; row = row->active_next) {
            if (row->payload && row->payload->kind == ThingKind::park_object) {
                const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(row->payload);
                if ((std::to_underlying(object.capabilities) & std::to_underlying(capability)) != 0) {
                    return true;
                }
            }
        }
        return false;
    }

    void select_category(int next) {
        if (next < 1 || next > 6) {
            return;
        }
        if (category == next && menu_transition) {
            reset_menu();
            return;
        }
        category = next;
        c.ui.persistent_menu_category = next;
        menu_transition = 1;
        menu_started = ui.clock();
        auto& menu = required(primary, 52);
        constexpr std::array<std::uint32_t, 6> tabs { 77, 73, 78, 74, 76, 75 };
        constexpr std::array<std::uint32_t, 6> titles { 0x33A, 0x340, 0x345, 0x349, 0x34D, 0x353 };
        constexpr std::array<std::int32_t, 6> counts { 5, 4, 3, 3, 6, 2 };
        const auto index = static_cast<std::size_t>(next - 1);
        auto& group = required(&required(primary, 69), 72);
        const auto& tab = required(&group, tabs[index]);
        ui.set_selected(group, static_cast<std::int32_t>(tabs[index]));
        menu_target_top = menu_bounds.top + 58 * (7 - counts[index]);
        const auto left = tab.rect.left + tab.rect.width() / 2 - menu_bounds.width() / 2;
        ui.set_bounds(menu, { left, menu_bounds.bottom - 155, left + menu_bounds.width(), menu_bounds.bottom });
        ui.set_depth(menu, 25);
        ui.set_visible(menu, true);
        for (auto id : action_ids) {
            ui.set_visible(required(&menu, id), false);
        }
        auto& title = required(&menu, 53);
        caption(title, text(titles[index]), 5);
        apply_standard_menu_text_colors(title, ensure_world_standard_ui_balance(c, root).user_interface, true);
        ui.set_visible(title, true);
    }

    void configure_menu() {
        if (category < 1 || category > 6) {
            return;
        }
        auto& menu = required(primary, 52);
        const auto& rows = menu_rows[static_cast<std::size_t>(category - 1)];
        for (std::size_t i = 0; i < rows.size(); ++i) {
            auto& button = required(&menu, action_ids[i]);
            const auto& row = rows[i];
            ui.set_visible(button, row.text != 0);
            if (!row.text) {
                continue;
            }
            caption(button, text(category == 5 && i == 6 && c.simulation.current && !c.simulation.current->saved.park_closed ? 0x258 : row.text));
            bool enabled = true;
            if (category == 1 && (i == 3 || i == 5)) {
                enabled = has_kind(ThingKind::mechanic) || has_kind(ThingKind::handyman) || has_kind(ThingKind::entertainer) || has_kind(ThingKind::guard) || has_kind(ThingKind::researcher) || has_kind(ThingKind::gardener);
            }
            if (category == 1 && i == 4) {
                enabled = has_capability(ParkObjectCapabilities::training_room);
            }
            if (category == 2 && i == 4) {
                enabled = c.attractions.park_object_record_count != 0;
            }
            if (category == 2 && i == 5) {
                enabled = has_capability(ParkObjectCapabilities::thinking_space);
            }
            if (category == 3 && i >= 5) {
                enabled = has_kind(ThingKind::person);
            }
            ui.set_enabled(button, enabled);
            apply_standard_menu_text_colors(button, ensure_world_standard_ui_balance(c, root).user_interface, false, row.text == 848 && can_proceed_with_current_objective(c));
        }
        menu_transition = 2;
    }

    void menu_action(std::size_t index) {
        if (category < 1 || category > 6 || index >= 7) {
            return;
        }
        if (category == 5 && index == 4) {
            open_objectives();
            reset_menu();
            return;
        }
        if (category == 5 && index == 6) {
            if (!c.simulation.current) {
                throw std::logic_error("Park lifecycle UI without World");
            }
            handle_world_park_open_close(c, *c.simulation.current, c.simulation.current->saved.park_closed != 0);
            configure_menu();
            reset_menu();
            return;
        }
        const auto row = menu_rows[static_cast<std::size_t>(category - 1)][index];
        if (row.text) {
            command(row.command);
        }
        reset_menu();
    }

    std::intptr_t event_control(UiControl& control, const UiEvent& e) {
        const bool action = e.type == 256 || e.type == 65542;
        if (control.id == 52) {
            if (e.type == 30 && menu_transition == 1) {
                const auto elapsed = std::clamp<std::int64_t>((ui.clock() - menu_started).count(), 0, 300);
                const auto sample = static_cast<std::int32_t>(std::sin(static_cast<double>(elapsed) * 0.0052359877) * 300.0);
                const auto top = menu_bounds.bottom - 155 + sample * (menu_target_top - menu_bounds.bottom + 175) / 300;
                auto rectangle = control.rect;
                rectangle.top = top;
                ui.set_bounds(control, rectangle);
                if (elapsed == 300) {
                    configure_menu();
                }
            } else if (action && menu_transition >= 1) {
                const auto found = std::ranges::find(action_ids, e.id);
                if (found != action_ids.end()) {
                    menu_action(static_cast<std::size_t>(found - action_ids.begin()));
                }
            }
        } else if (control.id == 69) {
            if ((e.type == 256 || e.type == 257 || e.type == 65542) && e.value == 1 && e.id >= 73 && e.id <= 78) {
                constexpr std::array categories { 2, 4, 6, 5, 1, 3 };
                select_category(categories[e.id - 73]);
            } else if (e.type == 258 && e.id >= 73 && e.id <= 78) {
                if (e.id == 76) {
                    open_objectives();
                } else {
                    constexpr std::array defaults { WorldUiCommand::attraction_build, WorldUiCommand::financial_statistics, WorldUiCommand::park_map, WorldUiCommand::help, WorldUiCommand::staff_hire, WorldUiCommand::visitor_list };
                    command(defaults[e.id - 73]);
                }
                reset_menu();
            } else if (action && e.id == 70) {
                command(WorldUiCommand::mail);
                reset_menu();
            }
        } else if (control.id == 62) {
            if (e.type == 257) {
                reset_menu();
                toolbar_selection(e.id, e.value == 1);
            } else if (e.type == 256 && e.id == 63) {
                command(WorldUiCommand::postcard);
            }
        } else if (control.id == 83 || control.id == 120) {
            if ((e.type == 256 || e.type == 258 || e.type == 65544 || e.type == 65545) && (e.id == 84 || (e.id >= 87 && e.id <= 90))) {
                request(WorldCameraAction { static_cast<std::int32_t>(e.id), e.type });
            } else if (e.type == 256 && e.id == 121) {
                command(WorldUiCommand::game_menu);
            }
        } else if (control.id == 111 && e.type == 256) {
            if (e.id == 112) {
                command(WorldUiCommand::postcard);
            } else if (e.id == 113) {
                toolbar_selection(e.value == 1 ? 65 : 67, true);
            }
        } else if (control.id == 95 && e.type == 65542) {
            command(WorldUiCommand::research);
        } else if (control.id == 96 && e.type == 65542) {
            command(WorldUiCommand::challenges);
        } else if (control.id == 80) {
            if (e.type == 12) {
                const auto percent = std::min(100, static_cast<std::int32_t>(static_cast<double>(control.value) * .0009765625 * 100.));
                const std::array argument { BfstSignedIntegerArgument { 2, percent } };
                happiness_help_text = localized(c.messages.mail_language.ui_help, 336, argument);
                happiness_help.text = happiness_help_text.c_str();
                return reinterpret_cast<std::intptr_t>(&happiness_help);
            }
            if (action) {
                (void)submit_positioned_template_request(c, 0, c.resources.ui_catalog_slot, 31, 0, 0, 0);
                command(c.simulation.current && get_world_park_gates_rsse_slot1(c, *c.simulation.current) == 1 ? WorldUiCommand::visitor_statistics : WorldUiCommand::park_gates);
            }
        }
        return ui.secondary(control, e);
    }

    void refresh_cash() {
        if (!c.simulation.current) {
            return;
        }
        auto* account = record<BankRecord>(c, c.simulation.current->saved.bank_account);
        if (!account) {
            return;
        }
        auto& cash = required(primary, 93);
        const auto value = account->balance;
        const auto magnitude = value < 0 ? -static_cast<std::int64_t>(value) : value;
        ui.set_text(cash, (value < 0 ? u"-$ " : u"$ ") + number_text(magnitude));
        const auto wrapped = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) + 999u);
        cash.color = cash_colors[static_cast<std::size_t>(std::clamp(wrapped / 1000, 0, 5))];
        auto& cost = required(&cash, 94);
        ui.set_visible(cost, interaction_cost != -999 && interaction_cost != -9999);
        if (cost.visible()) {
            ui.set_text(cost, interaction_cost_text(interaction_cost));
        }
    }

    std::u16string interaction_cost_text(std::int32_t value) {
        //513920's scalar attachment distinguishes Golden Tickets from cash.
        //Native construction previews encode cash by this exact bias.
        constexpr std::int32_t bias = 250539758;
        const auto absolute = [](std::int32_t v) {
            return v < 0 ? std::bit_cast<std::int32_t>(0u - static_cast<std::uint32_t>(v)) : v;
        };
        std::uint32_t row = 957, key = 39;
        if (absolute(value) <= bias) {
            if (value < 0) {
                row = 963;
                key = 47;
                value = absolute(value);
            }
        } else if (value < 0) {
            row = 958;
            key = 44;
            value = absolute(value + bias);
        } else {
            value -= bias;
        }
        const std::array argument { BfstSignedIntegerArgument { key, value } };
        return localized(c.messages.mail_language.ui_text, row, argument);
    }

    void refresh_indicators() {
        if (!c.simulation.current || c.mode.game == GameMode::code_3) {
            return;
        }
        if (auto* lab = record<ResearchLabRecord>(c, c.simulation.current->saved.research_lab)) {
            const auto p = lab->primary_current_project, s = lab->secondary_current_project;
            int progress = p >= 0 ? get_world_research_project_progress_percent(lab, p) : 0;
            if (s >= 0) {
                bool choose = p < 0;
                if (p >= 0) {
                    auto pd = estimate_research_project_days_remaining(c, lab, ResearchWorkSlot::primary);
                    auto sd = estimate_research_project_days_remaining(c, lab, ResearchWorkSlot::secondary);
                    if (!pd) {
                        pd = 10000;
                    }
                    if (!sd) {
                        sd = 10000;
                    }
                    choose = pd > sd;
                }
                if (choose) {
                    progress = get_world_research_project_progress_percent(lab, s);
                }
            }
            auto& research = required(primary, 95);
            const std::array args { BfstSignedIntegerArgument { 6, progress } };
            ui.set_text(research, localized(c.messages.mail_language.ui_text, 959, args));
            research_animating = progress >= 100;
            if (progress >= 100) {
                --research_countdown;
            } else if (progress > 0) {
                research_countdown = 30;
            }
            ui.set_visible(research, (progress > 0 && progress < 100) || research_countdown > 0);
        }
        auto& challenge = required(primary, 96);
        bool present = false;
        if (const auto* manager = record<ChallengeManagerRecord>(c, c.simulation.current->saved.challenge_manager)) {
            for (const auto& row : manager->records) {
                present |= row.status == ChallengeStatus::preparing || row.status == ChallengeStatus::active;
            }
        }
        ui.set_visible(challenge, present);
        auto& happy = required(primary, 80);
        auto& face = required(&happy, 81);
        if (c.simulation.current->saved.park_closed || !count_eligible_person_records(c, 4)) {
            ui.set_value(happy, 0);
            ui.set_attachment_part(face, 5);
        } else {
            const auto average = average_eligible_person_happiness(c, 4);
            ui.set_value(happy, static_cast<std::int32_t>(average * 1024.0 * .0099999998));
            ui.set_attachment_part(face, static_cast<std::uint16_t>((static_cast<std::int64_t>(average) - 1) / 20));
        }
    }

    void refresh_advisor_attachment() {
        const auto& view = c.advisor.presentation;
        auto& frame = required(primary, 117);
        auto& portrait = required(&frame, 118);
        ui.set_visible(frame, view.attachment_controls_visible);
        ui.set_visible(portrait, view.attachment_controls_visible);
        ui.set_visible(required(primary, 119), view.replay_enabled);
        if (view.attachment_controls_visible && view.attachment_part >= 0) {
            ui.set_attachment_part(portrait, static_cast<std::uint16_t>(view.attachment_part));
        }
    }

    void refresh_hud(bool full = false, bool animate = false) {
        refresh_advisor_attachment();
        if (menu_transition == 2) {
            configure_menu();
        }
        if (!c.simulation.current) {
            return;
        }
        if (full) {
            refresh_cash();
        }
        auto& date = required(primary, 120);
        ui.set_text(date, c.ui.panels.calendar_header.text);
        date.color = c.ui.panels.calendar_header.color;
        auto& weather = required(primary, 82);
        ui.set_visible(weather, c.weather.weather_hud.visible && c.mode.game != GameMode::code_3);
        weather.help_index = static_cast<std::uint32_t>(c.weather.weather_hud.help_query);
        ui.set_attachment_part(weather, static_cast<std::uint16_t>(c.weather.weather_hud.attachment_part));
        const auto& manager = get_or_create_theme_global_info_manager(c);
        auto& tickets = required(primary, 49);
        const bool shown = manager.golden_ticket_balance != 0;
        ui.set_visible(required(&tickets, 50), shown);
        ui.set_visible(required(&tickets, 51), shown);
        ui.set_text(required(&tickets, 50), u"x " + number_text(manager.golden_ticket_balance));
        if (auto* tags = record<TagSystemRecord>(c, c.simulation.current->saved.tag_system); tags && tags->mail_counter_state) {
            ui.set_text(required(c.ui.mail_counter_root, 70), number_text(tags->mail_counter_state->total_count));
        }
        if (full || ui.clock() >= next_indicator) {
            refresh_indicators();
            next_indicator = ui.clock() + std::chrono::milliseconds { 2000 };
        }
        const bool paused = gameplay_paused ? gameplay_paused() : false;
        if (animate && research_animating && !paused) {
            ui.set_value(required(primary, 95), (required(primary, 95).value + 1) % 3);
        }
        auto& challenge = required(primary, 96);
        const auto ancestors_visible = [](const UiControl& control) {
            for (auto* node = &control; node; node = node->parent) {
                if (!node->visible()) {
                    return false;
                }
            }
            return true;
        };
        bool challenge_present = false;
        if (const auto* challenge_manager = record<ChallengeManagerRecord>(c, c.simulation.current->saved.challenge_manager)) {
            for (const auto& row : challenge_manager->records) {
                challenge_present |= row.status == ChallengeStatus::preparing || row.status == ChallengeStatus::active;
            }
        }
        if (animate && !paused && challenge_present) {
            ui.set_visible(challenge, true);
        }
        if (animate && !paused && challenge_present && ancestors_visible(challenge)) {
            bool warning = false;
            if (const auto* challenges = record<ChallengeManagerRecord>(c, c.simulation.current->saved.challenge_manager)) {
                for (const auto& row : challenges->records) {
                    warning |= row.status == ChallengeStatus::active && row.sent_time_warning;
                }
            }
            if (warning) {
                auto next = challenge.value + challenge_step;
                if (next <= 8) {
                    next = 8;
                    challenge_step = 1;
                }
                if (next >= 11) {
                    next = 11;
                    challenge_step = -1;
                }
                ui.set_value(challenge, next);
            } else {
                ui.set_value(challenge, challenge.value ? challenge.value - 1 : 7);
            }
        }
        if (panel && ui.owns(panel) && c.ui.panels.objectives_refresh_pending) {
            refresh_objectives_transition();
        }
        if (highlight && !ui.owns(highlight)) {
            highlight = nullptr;
        }
    }

    void update_tooltip() {
        const auto* picker = world_camera_picker_state(c);
        auto* hover = ui.hover();
        const bool control_help = hover && hover != primary && hover != secondary;
        const bool construction_help = construction_runtime && c.construction.mode && std::holds_alternative<DefaultWorldInteraction>(c.staff.interaction.handler.value);
        const auto text = picker && (!hover || hover == primary || hover == secondary) ? std::u16string_view { picker->help_text } : (!hover || hover == primary || hover == secondary) ? construction_help ? std::u16string_view { construction_runtime->state.cursor_help } : std::u16string_view { world_cursor_help } : std::u16string_view {};
        if (!tooltip && text.empty() && !control_help && (!tooltip_override || tooltip_override->empty())) {
            return;
        }
        if (!fonts) {
            fonts = std::make_unique<UiFontLibrary>(root);
        }
        if (!tooltip) {
            tooltip = std::make_unique<UiTooltipController>(ui, nullptr, *fonts, c, root);
        }
        if (tooltip_offsets) {
            tooltip->offsets(*tooltip_offsets);
        }
        tooltip->enabled(tooltips_enabled);
        tooltip->frame_rate_below_clip(construction_runtime && construction_runtime->state.frame_rate_below_clip);
        if (tooltip_override) {
            tooltip->update(*tooltip_override, ui.pointer_position(), viewport_pixels[0], viewport_pixels[1]);
        } else if (control_help) {
            tooltip->update_help(hover, ui.pointer_position(), viewport_pixels[0], viewport_pixels[1]);
        } else {
            tooltip->update(text, ui.pointer_position(), viewport_pixels[0], viewport_pixels[1]);
        }
    }

    void close_panel() {
        if (!panel || !ui.owns(panel)) {
            panel = nullptr;
            return;
        }
        auto* closing_panel = std::exchange(panel, nullptr);
        if (c.ui.active_root == closing_panel) {
            c.ui.active_root = nullptr;
        }
        ui.destroy(closing_panel);
        ui.set_input_root(nullptr);
        ui.set_focus(secondary->visible() ? secondary : primary);
        std::erase_if(scrolling_text, [this](const auto& item) {
            return !ui.owns(item.first);
        });
        if (!closing) {
            close_objectives_advisor();
            c.ports.ui.clear_control_particle_group(c);
            update_objectives_panel_and_queue_mission_advisor_action(c);
        }
    }

    std::int32_t objectives_advisor_action() {
        //Original93E3E0 (open) and93CE1C (close) contain the same18 entries.
        constexpr std::array<std::int32_t, 18> actions { 547, 548, 549, 550, 551, 552, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562, 563, 564 };
        const auto mission = get_or_create_theme_global_info_manager(c).mission_number;
        if (mission < 0 || static_cast<std::size_t>(mission) >= actions.size()) {
            throw std::out_of_range("Objectives mission Advisor action");
        }
        return actions[static_cast<std::size_t>(mission)];
    }

    AdvisorRecord& objectives_advisor(World* world) {
        if (!world) {
            throw std::logic_error("Objectives Advisor World is not bound");
        }
        return require_bound(record<AdvisorRecord>(c, world->saved.advisor), "Objectives World Advisor is not bound");
    }

    void open_objectives_advisor() {
        //55A860..55A887: clearing the score tutorial gate precedes candidate
        //scoring. The mission narrative is queued immediately, whereas631 is a
        //separate delayed UI-panel request (5F16C0,503AD0).
        get_or_create_theme_global_info_manager(c).advisor_eligibility_flags[6] = 0;
        auto candidate = world_operations::make_advisor_action_candidate(c, objectives_advisor_action());
        auto& primary_advisor = objectives_advisor(c.referenced_world());
        (void)world_operations::queue_advisor_action_candidate(c, &primary_advisor, candidate);
        auto& advisor = objectives_advisor(c.simulation.current);
        advisor.from_ui_panel = 1;
        advisor.delayed_message = 631;
        advisor.delay_ticks = c.ports.clock.read_advisor_ui_delay_clock(c);
    }

    void close_objectives_advisor() {
        //55BB48..55BBC6: both pending requests are cancelled, followed by the
        //mission narrative. Clearing the panel flag prevents631 firing later.
        auto& advisor = objectives_advisor(c.simulation.current);
        advisor.from_ui_panel = 0;
        advisor.panel_up = 0;
        for (const auto action : { 438, 631, objectives_advisor_action() }) {
            world_operations::cancel_advisor_action_by_id(c, &advisor, action);
        }
    }

    void open_objectives();
    void refresh_objectives();

    bool refresh_objectives_transition() {
        if (!panel || !ui.owns(panel)) {
            return false;
        }
        if (!objectives_staff_initialized && query_mission_obstacle_research_status(c).status == MissionResearchStatus::complete) {
            //55BC1B..55BC71: native list absence records the pre-research phase.
            //Both the event30 path and the host refresh path must take this branch.
            close_panel();
            world_operations::queue_scored_advisor_action(c, 506);
            open_objectives();
            return true;
        }
        refresh_objectives();
        return false;
    }

    void update_objectives_text(UiControl& body, const std::u16string& text) {
        objectives_text = text;
        if (text.empty()) {
            objectives_text_refresh_suppressed = false;
        }
        if (objectives_text_refresh_suppressed) {
            return;
        }
        ui.set_text(body, text);
        ui.set_visible(body, true);
        auto& scroll = required(&body, 846946);
        if (text.empty()) {
            ui.set_range(scroll, 0, 0);
            ui.set_value(scroll, 0);
            ui.set_visible(scroll, false);
        }
        body.text_offset = { 0, -scroll.value };
    }

    std::u16string objective_requirements(ThemeGlobalInfoManager&);
    std::u16string objective_status(ThemeGlobalInfoManager&);
    std::intptr_t objective_event(UiControl&, const UiEvent&);
    void draw_control(UiControl&, UiDrawList&, UiRect, std::uint32_t, std::uint32_t);

    struct ScrollableText {
        std::uint32_t scrollbar {};
        std::uint64_t lifetime {};
    };

    std::map<UiControl*, ScrollableText> scrolling_text;

    void bind_scrollable_text(UiControl& body, std::optional<std::uint32_t> scrollbar) {
        if (!scrollbar) {
            scrolling_text.erase(&body);
            return;
        }
        if (!ui.owns(&body)) {
            throw std::invalid_argument("Scrollable UI body is not owned");
        }
        auto& child = required(&body, *scrollbar);
        if (child.parent != &body) {
            throw std::invalid_argument("Scrollable UI body requires its direct child");
        }
        scrolling_text[&body] = { *scrollbar, ui.lifetime_serial(&body) };
    }

    void prepare_scrollable_text(std::uint32_t pixel_width, std::uint32_t pixel_height) {
        std::erase_if(scrolling_text, [this](const auto& entry) {
            return !ui.owns(entry.first) || ui.lifetime_serial(entry.first) != entry.second.lifetime;
        });
        for (const auto& [body, binding] : scrolling_text) {
            if (!body->visible()) {
                continue;
            }
            const auto font = fonts->slot(body->font, pixel_width);
            if (!font) {
                throw std::runtime_error(std::format("Scrollable UI font: {}", font.error().reason));
            }
            UiTextStyle style;
            style.wrap = true;
            style.scale = { 2048.f / static_cast<float>(pixel_width), 1536.f / static_cast<float>(pixel_height) };
            style.horizontal = static_cast<UiTextHorizontal>(std::clamp(body->horizontal_alignment, 0, 2));
            auto full = bounds(body->attachment_rect);
            full[3] = full[1] + 100000.f;
            const auto measured = fonts->measure(**font, body->text, full, style);
            if (!measured) {
                throw std::runtime_error(std::format("Scrollable UI measure: {}", measured.error().reason));
            }
            const auto height = static_cast<std::int32_t>(measured->bounds[3] - measured->bounds[1]);
            const auto overflow = std::max(0, height - body->attachment_rect.height());
            auto& scroll = required(body, binding.scrollbar);
            ui.set_range(scroll, 0, overflow);
            ui.set_visible(scroll, overflow > 0);
            body->text_offset = { 0, -scroll.value };
        }
    }
};

void WorldUiRuntime::State::dispatch_mail(const WorldMailUiAction& action) {
    const ThingHeader* target {};
    if (action.identity) {
        if (!action.target || action.target >= c.entities.size()) {
            return;
        }
        target = c.entities[action.target].payload;
        if (target != action.identity || target->retirement_pending) {
            return;
        }
    }
    using K = WorldMailUiActionKind;
    match (action.kind) {
        case K::focus_thing => {
            if (!target) {
                return;
            }
            const MapObjectBase* base {};
            if (target->kind == ThingKind::person) {
                base = &reinterpret_cast<const PersonRuntimeView*>(target)->base;
            } else if (is_staff_record_kind(target->kind)) {
                base = &reinterpret_cast<const StaffRuntimeRecord*>(target)->base;
            } else if (target->kind == ThingKind::park_object) {
                base = &reinterpret_cast<const ParkObjectRuntimeView*>(target)->base;
            }
            if (base) {
                //5033C0 floors8.8 positions to cells, then adds WORLD-unit offsets.
                //Copy the resulting coordinates; no borrowed object escapes dispatch.
                request(WorldPositionFocusAction { 10 * (base->x >> 8) + action.x_offset, 10 * (base->y >> 8) + action.y_offset });
            }
            return;
        }
        case K::focus_cell => {
            if (action.argument > 0 && static_cast<std::size_t>(action.argument) <= map_cell_count) {
                const auto cell = action.argument - 1;
                request(WorldPositionFocusAction { 10 * (cell % 128), 10 * (cell / 128) });
            }
            return;
        }
        case K::visitor_locator => {
            visitors->open_locator(action.target);
            return;
        }
        case K::staff_locator => {
            staff->open_locator(action.target);
            return;
        }
        case K::build => {
            if (!build) {
                throw std::logic_error("Mail Build route has no construction owner");
            }
            build->open_build(std::to_underlying(action.category), static_cast<std::uint16_t>(action.argument));
            return;
        }
        case K::challenges => {
            if (management) {
                management->open_challenges(static_cast<ChallengeScriptIndex>(action.argument));
            } else {
                request(WorldCommandAction { WorldUiCommand::challenges, action.argument });
            }
            return;
        }
        case K::objectives => {
            open_objectives();
            return;
        }
        case K::research => {
            command(WorldUiCommand::research);
            return;
        }
        case K::wage_demand => {
            staff->open_wage(action.target, action.argument);
            return;
        }
        case K::loans => {
            command(WorldUiCommand::loans);
            return;
        }
        case K::hire => {
            staff->open_hire(action.staff_type);
            return;
        }
        case K::overview => {
            request(WorldCommandAction { WorldUiCommand::overview_map, action.argument });
            return;
        }
        case K::park_gates => {
            command(WorldUiCommand::park_gates);
            return;
        }
        case K::maintenance => {
            if (target && target->kind == ThingKind::park_object) {
                (void)handle_park_object_maintenance_request(c, *c.entities.payload<ParkObjectRuntimeView>(action.target), true);
            }
            return;
        }
        case K::park_statistics => {
            command(WorldUiCommand::park_statistics);
            return;
        }
        case _ => {
        }
    } throw std::invalid_argument("Unknown typed Mail UI action");
}

void WorldUiRuntime::State::open_objectives() {
    dismiss_panels();
    open_objectives_advisor();
    reset_menu();
    details = false;
    objectives_staff_initialized = query_mission_obstacle_research_status(c).status == MissionResearchStatus::complete;
    objectives_text_refresh_suppressed = false;
    objectives_text.clear();
    const auto parsed = ui.parse_template(primary, std::as_bytes(std::span { objectives_template }), [this](UiControl& control, const UiEvent& event) {
        return objective_event(control, event);
    });
    if (!parsed || !*parsed) {
        throw std::runtime_error(std::format("Original Objectives UI template: {}", (parsed ? std::string { "no root" } : parsed.error().reason)));
    }
    panel = *parsed;
    //501CD0 installs the panel only after resetting the live interaction owner.
    //The host then drops old pointer tracking without another mode transition.
    reset_world_interaction();
    ui.set_selected(required(&required(primary, 62), 64), 67);
    primary->cursor_id = 0;
    request(WorldResetInteractionAction {});
    objectives_advisor(c.simulation.current).panel_up = 1;
    c.ui.active_root = panel;
    auto& content = required(panel, 846931);
    ui.set_primary_callback(content, [this](UiControl& control, const UiEvent& event) {
        return objective_event(control, event);
    });
    auto& tabs = required(&content, 846935);
    ui.set_primary_callback(tabs, [this](UiControl& control, const UiEvent& event) {
        return objective_event(control, event);
    });
    caption(required(panel, 846954), text(461), 5);
    caption(required(&tabs, 846937), text(463), 7);
    caption(required(&tabs, 846936), text(464), 7);
    auto& proceed = required(&content, 846952);
    proceed.text_constraints = { true, true };
    caption(proceed, text(0x52), 7);
    proceed.color = { 200, 200, 220, 255 };
    proceed.help_index = 100;
    auto& details_button = required(&content, 846953);
    details_button.text_constraints = { true, true };
    caption(details_button, text(0x3DC), 7);
    auto& heading = required(&content, 846934);
    heading.font = 5;
    heading.text_constraints = { true, true };
    auto& body = required(&content, 846938);
    body.text_constraints = { true, true };
    for (auto id : { 846939u, 846940u, 846942u, 846943u, 846944u, 846945u }) {
        auto& text_control = required(&body, id);
        text_control.font = 6;
        text_control.vertical_alignment = 1;
    }
    for (auto id : { 846939u, 846942u, 846944u }) {
        required(&body, id).color = { 234, 239, 102, 255 };
    }
    caption(required(&body, 846942), text(0x359), 6, 0);
    caption(required(&body, 846939), text(0x358), 6, 0);
    caption(required(&body, 846944), text(0x357), 6, 0);
    auto& description = required(&body, 846946);
    description.minimum = 0;
    description.maximum = 0;
    ui.set_visible(description, false);
    body.font = 7;
    body.horizontal_alignment = body.vertical_alignment = 0;
    body.text_offset = {};
    bind_scrollable_text(body, 846946);
    auto& list_container = required(&body, 846947);
    for (const auto id : { 846949u, 846950u, 846951u }) {
        required(&list_container, id).text_constraints = { true, true };
    }
    auto& staff_list = required(&list_container, 846948);
    if (objectives_staff_initialized) {
        staff_list.font = 7;
        staff_list.vertical_alignment = 1;
        staff_list.row_height_font_padding = 0;
        ui.set_list_sort_key(staff_list, 3, false);
        for (std::uint32_t column = 0; column != 3; ++column) {
            caption(required(&staff_list, 16 + column), text(87 + column), 7);
        }
    }
    ui.set_visible(list_container, false);
    ui.set_primary_callback(body, [this](UiControl& control, const UiEvent& event) {
        //55B9A0/52C4C0: the body owns text; its direct scrollbar owns a
        //positive downward position and the attachment uses its negative
        //offset.
        if (event.type == 2048 && event.id == 846946) {
            control.text_offset = { 0, -required(&control, 846946).value };
        } else if (event.type == 2049) {
            objectives_text_refresh_suppressed = true;
        } else if (event.type == 2050) {
            objectives_text_refresh_suppressed = false;
            update_objectives_text(control, objectives_text);
        }
        return objective_event(control, event);
    });
    ui.set_primary_callback(description, [this](UiControl& control, const UiEvent& event) {
        if (event.type == 2049 || event.type == 2050) {
            (void)ui.dispatch(control.parent, event);
        }
        return ui.secondary(control, event);
    });
    ui.set_periodic(*panel, 0, UiPeriodicEvent { 16, 524416, 0, std::chrono::milliseconds { 4000 }, ui.clock() + std::chrono::milliseconds { 4000 }, true });
    required(panel, 4294967294u).help_index = 1;
    //501CD0/5027B0 change keyboard focus only. Objectives remains a child of
    //the primary canvas, so the HUD and World outside its bounds still hit.
    ui.set_focus(panel);
    refresh_objectives();
}

std::u16string WorldUiRuntime::State::objective_requirements(ThemeGlobalInfoManager& manager) {
    //Native55CA60: requirements and current progress are separate paragraphs;
    //promotion and challenge progress use the same production mission owners
    //as Proceed eligibility, rather than inferred UI state.
    const auto& balance = manager.mission_research;
    const auto source = manager.selected_theme_info ? manager.selected_theme_info->source_ordinal : theme_source_count;
    std::u16string result = text(83) + u"\n";
    if (balance.promotion_number) {
        if (manager.current_promotion_index < balance.promotion_number) {
            std::uint32_t row {};
            match (manager.mission_number) {
                case 2  => row = 94;
                case 4  => row = source == 1 ? 95 : 100;
                case 10 => row = source == 0 ? 96 : 101;
                case 14 => row = source == 1 ? 97 : 102;
                case _  => {
                }
            } if (row) {
                result += text(row) + u"\n";
            }
        } else {
            const auto promotion = localized(c.messages.mail_language.promotions, balance.promotion_number);
            const std::array arguments { BfstWideStringArgument { 12, promotion } };
            result += localized(c.messages.mail_language.ui_text, 99, {}, arguments) + u"\n";
        }
    }
    //Native9BED18 target/current STAT_TITLES ordinals for metric0..8.
    constexpr std::array<std::array<std::uint32_t, 2>, 9> titles { { { { 0, 0 } }, { { 0, 0 } }, { { 7, 8 } }, { { 1, 2 } }, { { 0, 0 } }, { { 0, 0 } }, { { 5, 6 } }, { { 9, 0xA } }, { { 0xB, 0xC } } } };
    const auto& stat_titles = table("STAT_TITLES.str");
    const auto append_metrics = [&](bool current) {
        //Approved55CA60 repair: retain slot correspondence instead of treating
        //the count of configured rows as a populated prefix.
        for (std::size_t i = 0; i < balance.exit_statistics.size(); ++i) {
            if (balance.exit_statistics[i].objective_kind == MissionObjectiveMetric::none) {
                continue;
            }
            const auto& objective = manager.objective_entries[i];
            const auto kind = static_cast<std::uint32_t>(objective.objective_kind);
            if (kind >= titles.size()) {
                throw std::out_of_range("Objectives mission statistic kind");
            }
            const std::array argument { BfstSignedIntegerArgument { 0, current ? objective.current_value : objective.target_value } };
            result += localized(stat_titles, titles[kind][current ? 1 : 0], argument) + u"\n";
        }
    };
    append_metrics(false);
    std::int32_t required {}, completed {}, theme {};
    get_mission_required_challenge_progress(c, &required, &completed, &theme);
    const auto stat = [&](std::uint32_t row, std::int32_t value) {
        const std::array arguments { BfstSignedIntegerArgument { 0, value } };
        return localized(stat_titles, row, arguments);
    };
    if (required > 0) {
        result += stat(3, required) + u"\n";
    }
    result += u"\n";
    if (balance.obstacle_golden_tickets) {
        result += text(85) + u"\n" + stat(balance.obstacle_golden_tickets == 1 ? 13 : 14, balance.obstacle_golden_tickets) + u"\n\n";
    }
    result += text(86) + u"\n";
    if (balance.promotion_number) {
        const auto promotion = localized(c.messages.mail_language.promotions, manager.current_promotion_index);
        const std::array arguments { BfstWideStringArgument { 12, promotion } };
        result += localized(c.messages.mail_language.ui_text, 98, {}, arguments) + u"\n";
    }
    append_metrics(true);
    if (required > 0) {
        result += stat(4, completed) + u"\n";
    }
    if (balance.obstacle_golden_tickets) {
        result += stat(15, manager.golden_ticket_balance);
    }
    return result;
}

std::u16string WorldUiRuntime::State::objective_status(ThemeGlobalInfoManager& manager) {
    //Native55C350/55C600/55C800 select the actionable status at this park.
    const auto& balance = manager.mission_research;
    if (!manager.selected_theme_info) {
        throw std::logic_error("Objectives requires a selected theme");
    }
    const auto source = manager.selected_theme_info->source_ordinal;
    if (source != static_cast<std::uint32_t>(balance.ending_theme)) {
        if (manager.current_promotion_index < balance.promotion_number) {
            return objective_requirements(manager) + u"\n";
        }
        std::u16string name;
        if (balance.ending_theme >= 0 && balance.ending_theme <= 2) {
            constexpr std::array<std::uint32_t, 3> name_rows { 3, 0, 1 };
            name = localized(table("THEMENAMES.str"), name_rows[balance.ending_theme]);
        }
        const std::array arguments { BfstWideStringArgument { 5, name } };
        return text(86) + u"\n" + localized(c.messages.mail_language.ui_text, 75, {}, arguments) + u"\n";
    }
    if (!is_obstacle_mission_research_enabled(c)) {
        return objective_requirements(manager) + u"\n";
    }
    const auto status = query_mission_obstacle_research_status(c);
    if (status.status != MissionResearchStatus::incomplete) {
        return {};
    }
    auto* lab = record<ResearchLabRecord>(c, c.simulation.current->saved.research_lab);
    const auto progress = lab ? get_world_research_project_progress_percent(lab, find_unfinished_obstacle_research(lab)) : 0;
    if (progress) {
        const std::array arguments { BfstSignedIntegerArgument { 11, progress } };
        return text(86) + u"\n" + localized(c.messages.mail_language.ui_text, 91, arguments) + u"\n";
    }
    auto result = text(86) + u"\n" + text(103) + u"\n";
    if (!lab) {
        result += text(104) + u"\n";
    }
    if (!c.simulation.current->saved.first_researcher) {
        result += text(105) + u"\n";
    }
    return result + text(106) + u"\n";
}

void WorldUiRuntime::State::refresh_objectives() {
    if (!panel || !ui.owns(panel)) {
        return;
    }
    auto& manager = get_or_create_theme_global_info_manager(c);
    auto& content = required(panel, 846931);
    auto& body = required(&content, 846938);
    const auto mission = static_cast<std::uint32_t>(manager.mission_number);
    const auto operation = localized(table("OPERATION.str"), mission);
    caption(required(&content, 846934), operation, 5);
    caption(required(&body, 846943), operation, 6, 0);
    const auto variant = std::ranges::find(c.messages.tag_system_message_variants, 1, &TagMessageVariant::variant_id);
    if (variant != c.messages.tag_system_message_variants.end()) {
        caption(required(&body, 846940), localized(c.messages.mail_language.tag_system, static_cast<std::uint32_t>(variant->category_resource_id)), 6, 0);
        ui.set_value(required(&body, 846941), static_cast<std::int32_t>(variant->variant_code) - 1);
    }
    caption(required(&body, 846945), c.finance.shares_finance.ownership_context ? c.finance.shares_finance.ownership_context->current_profile_name : std::u16string {}, 6, 0);
    //Original9BECCC mission portrait lookup, consumed after the1220-byte
    //template.
    constexpr std::array<std::int32_t, 18> portraits { 0xA, 0xC, 0x4, 0xF, 0x4, 0x8, 0x6, 0x4, 0xB, 0xD, 0x4, 0x11, 0x10, 0xE, 0x4, 0x7, 0x9, 0x4 };
    auto& portrait = required(&required(&content, 846932), 846933);
    if (mission < portraits.size()) {
        ui.set_value(portrait, portraits[mission] - 1);
    } else {
        ui.set_visible(portrait, false);
    }
    auto& list_container = required(&body, 846947);
    ui.set_visible(list_container, false);
    std::u16string result;
    if (!details) {
        const bool promoted = manager.mission_number != 0 && manager.mission_number != 2 && manager.mission_research.promotion_number != 0 && manager.current_promotion_index >= manager.mission_research.promotion_number;
        const std::array arguments { BfstSignedIntegerArgument { 0, manager.objective_entries[0].target_value } };
        result = localized(table(promoted ? "MISSION_DESC_GOTPROMO.str" : "MISSION_DESC.str"), mission, manager.mission_number == 0 ? std::span<const BfstSignedIntegerArgument> { arguments } : std::span<const BfstSignedIntegerArgument> {});
        //55D62C/5CD290 append the selected TagSystem sender's localized
        //two-line signature after two native line breaks.
        if (variant == c.messages.tag_system_message_variants.end()) {
            throw std::logic_error("Objectives briefing requires TagSystem variant1");
        }
        result += u"\n\n" + localized(c.messages.mail_language.tag_system, static_cast<std::uint32_t>(variant->category_resource_id)) + u"\n" + localized(c.messages.mail_language.tag_system, static_cast<std::uint32_t>(variant->secondary_resource_id));
    } else {
        const auto status = query_mission_obstacle_research_status(c);
        if (status.status == MissionResearchStatus::complete) {
            ui.set_visible(list_container, true);
            caption(required(&list_container, 846949), text(84), 7, 0, 0);
            const auto& stat_titles = table("STAT_TITLES.str");
            const auto stat = [&](std::uint32_t row, std::int32_t value) {
                const std::array arguments { BfstSignedIntegerArgument { 0, value } };
                return localized(stat_titles, row, arguments);
            };
            const auto tickets = manager.mission_research.obstacle_golden_tickets;
            caption(required(&list_container, 846950), tickets ? text(85) + u" " + stat(tickets == 1 ? 13 : 14, tickets) : std::u16string {}, 7, 0, 0);
            auto& rows = required(&list_container, 846948);
            std::vector<UiListRow> staff_rows;
            auto* hq = record<StaffHqRecord>(c, c.simulation.current->saved.staff_hq);
            const bool staffed = hq && std::ranges::none_of(hq->staff_slots, [](const auto& slot) {
                                     return slot.staff_type != StaffType::unsupported_index && !slot.assigned_staff;
                                 });
            auto staffing = text(86) + u"\n";
            if (tickets) {
                staffing += stat(15, manager.golden_ticket_balance) + u"\n";
            }
            staffing += text(staffed ? 93 : 92);
            caption(required(&list_container, 846951), staffing, 7, 0, 0);
            if (hq) {
                for (std::size_t i = 0; i < hq->staff_slots.size(); ++i) {
                    const auto& requirement = hq->staff_slots[i];
                    if (requirement.staff_type == StaffType::unsupported_index) {
                        break;
                    }
                    UiListRow row;
                    row.identity = static_cast<std::intptr_t>(i);
                    row.cells.push_back(localized(c.messages.mail_language.staff_types, static_cast<std::uint32_t>(requirement.staff_type) * 2));
                    const auto ability = static_cast<std::uint32_t>(requirement.required_ability);
                    row.cells.push_back(ability < manager.theme_ability_activation_flags.size() && manager.theme_ability_activation_flags[ability] ? localized(c.messages.mail_language.abilities, ability) : text(114));
                    if (const auto* worker = record<StaffRuntimeRecord>(c, requirement.assigned_staff)) {
                        row.cells.emplace_back(worker->name.begin(), std::ranges::find(worker->name, char16_t {}));
                    } else {
                        row.cells.push_back(u" " + localized(c.messages.mail_language.abilities, 0));
                    }
                    staff_rows.push_back(std::move(row));
                }
            }
            //55E020 selects the third (employee) column ascending, after filling
            //the native20-slot sequence up to its unsupported-staff sentinel.
            ui.clear_list_rows(rows, false);
            ui.set_list_sort_key(rows, 3, false);
            ui.set_list_rows(rows, std::move(staff_rows), false, false);
        } else {
            result = objective_status(manager);
        }
    }
    update_objectives_text(body, result);
    caption(required(&content, 846953), text(details ? 0x3DD : 0x3DC), 7);
    const bool proceed = can_proceed_with_current_objective(c);
    ui.set_enabled(required(&content, 846952), proceed);
    if (proceed) {
        c.ports.ui.configure_ui_control_particles(c, &required(&content, 846952));
    }
    c.ui.panels.objectives_refresh_pending = false;
}

std::intptr_t WorldUiRuntime::State::objective_event(UiControl& control, const UiEvent& e) {
    if (closing) {
        return ui.secondary(control, e);
    }
    if (e.type == 20) {
        //UiRuntime notifies before freeing descendants. Remove borrowed body
        //keys now, including independent body destruction, before allocator reuse.
        std::erase_if(scrolling_text, [&](const auto& entry) {
            for (auto* ancestor = entry.first; ui.owns(ancestor); ancestor = ancestor->parent) {
                if (ancestor == &control) {
                    return true;
                }
            }
            return false;
        });
    }
    if (e.type == 5) {
        //Native55BB0A closes the panel and queues the distinct dismissal action;
        //event20's cleanup queues the mission reminder, not this action506.
        close_panel();
        world_operations::queue_scored_advisor_action(c, 506);
        return 1;
    }
    if (e.type == 16 && e.id == 524416) {
        c.ui.panels.objectives_refresh_pending = true;
    } else if (e.type == 30 && c.ui.panels.objectives_refresh_pending) {
        if (refresh_objectives_transition()) {
            return 0;
        }
    } else if (e.type == 20 && &control == panel) {
        panel = nullptr;
        if (c.ui.active_root == &control) {
            c.ui.active_root = nullptr;
        }
        ui.set_input_root(nullptr);
        ui.set_focus(secondary->visible() ? secondary : primary);
        close_objectives_advisor();
        c.ports.ui.clear_control_particle_group(c);
        update_objectives_panel_and_queue_mission_advisor_action(c);
    } else if (e.type == 256) {
        if (e.id == 4294967294u) {
            close_panel();
            world_operations::queue_scored_advisor_action(c, 506);
            return 1;
        }
        if (e.id == 846953) {
            details = !details;
            auto& body = required(&required(panel, 846931), 846938);
            objectives_text_refresh_suppressed = false;
            ui.set_value(required(&body, 846946), 0);
            body.text_offset = {};
            refresh_objectives();
            return 1;
        }
        if (e.id == 846936) {
            close_panel();
            world_operations::queue_scored_advisor_action(c, 506);
            command(WorldUiCommand::challenges);
            return 1;
        }
        if (e.id == 846955) {
            queue_world_advisor_help_action(c, 438);
            return 1;
        }
        if (e.id == 846952) {
            if (!can_proceed_with_current_objective(c)) {
                return 1;
            }
            if (is_obstacle_mission_research_enabled(c)) {
                if (!resolve_mission_obstacle(c) || !prepare_mission_completion(c, c.simulation.current->saved.current_obstacle)) {
                    return 1;
                }
                WorldEvent event { ObstacleRemovedEvent { c.simulation.current->saved.current_obstacle } };
                dispatch_world_event_to_subscribers(c, get_world_event_subscription_registry(c), &event, deliver_world_record_event, nullptr);
            } else {
                auto* controller = record<MissionControllerRecord>(c, c.simulation.current->saved.mission_controller);
                if (!controller) {
                    throw std::logic_error("Objectives Proceed has no World mission controller");
                }
                if (!start_mission_completion_presentation(c, controller)) {
                    return 1;
                }
            }
            close_panel();
            world_operations::queue_scored_advisor_action(c, 506);
            return 1;
        }
    } else if (e.type == 65547 && (e.id > 255 ? e.id >> 8 : e.id) == 27 && (static_cast<std::uint32_t>(e.value) & 0xFF0000u) == 0) {
        //507FA0: unmodified Escape closes an active panel on KEYUP.
        return objective_event(control, UiEvent { 5, 0, 0 });
    }
    return ui.secondary(control, e);
}

void WorldUiRuntime::State::draw_control(UiControl& control, UiDrawList& output, UiRect clip, std::uint32_t pixel_width, std::uint32_t pixel_height) {
    if (!ui.owns(&control) || !control.visible()) {
        return;
    }
    const UiNativeDepthScope attachment_depth(output, ui_attachment_depth(std::max(control.secondary_placement.depth, 1)));
    if (control_draw_hook) {
        control_draw_hook(output, control, clip, pixel_width, pixel_height);
    }
    //Message body wrapping/range is prepared before its ordinary attachment
    //text is emitted, including the first paint after a viewport change.
    if (mail) {
        mail->append(output, control, clip, pixel_width, pixel_height);
    }
    const auto append_asset = [&](std::uint32_t hash, const UiAttachmentPlacement& attachment, UiRect rectangle) {
        if (!hash) {
            return;
        }
        UiModelPlacement place;
        place.bounds = bounds(rectangle);
        place.clip = bounds(clip);
        place.pivot = { static_cast<float>(attachment.pivot.x), static_cast<float>(attachment.pivot.y) };
        place.part_base = attachment.part_base;
        place.part_value = attachment.part_value;
        place.depth = static_cast<std::int16_t>(attachment.depth);
        place.rotation = static_cast<std::int16_t>(attachment.rotation);
        //Caption colors do not tint the original model attachment.
        if (!assets->append(output, hash, place) && !assets->find(hash) && !assets->is_original_null_binding(hash)) {
            //A known model can intentionally select a native-null part, e.g.
            //Staff thought icon999. The shipped staffstat binding is also null;
            //every other unknown model identity remains a resource error.
            const auto message = std::format("Missing original UI asset hash {}", hash);
            if (std::ranges::find(diagnostics, message) == diagnostics.end()) {
                diagnostics.push_back(message);
            }
        }
    };
    append_asset(control.asset_hash, control.primary_placement, control.rect);
    append_asset(control.secondary_asset_hash, control.secondary_placement, control.attachment_rect);
    if (control.id == 80 && gradient) {
        const auto r = control.attachment_rect;
        const auto left = static_cast<float>(r.left), top = static_cast<float>(r.top), bottom = static_cast<float>(r.bottom);
        const auto right = static_cast<float>(r.right);
        const auto z = 1.f - static_cast<float>(control.secondary_placement.depth) / 40.f;
        const auto fraction = static_cast<float>(control.value) / 1024.f;
        const auto middle = bottom - static_cast<float>(r.height()) * fraction;
        //4E78D0/4E7FE0: two vertical quads share the moving Y boundary.
        //Upper uses the right half of the original texture; lower uses its left.
        const auto quad = [&](float y0, float y1, float u0, float u1, float v0, float v1) {
            if (y1 <= y0) {
                return;
            }
            const auto base = static_cast<std::uint32_t>(output.vertices.size());
            output.vertices.insert(output.vertices.end(), { { { left, y0, z }, { u0, v0 }, { 255, 255, 255, 255 } }, { { right, y0, z }, { u1, v0 }, { 255, 255, 255, 255 } }, { { right, y1, z }, { u1, v1 }, { 255, 255, 255, 255 } }, { { left, y1, z }, { u0, v1 }, { 255, 255, 255, 255 } } });
            UiDrawCommand command;
            command.texture = gradient;
            command.clip = bounds(clip);
            command.first_index = static_cast<std::uint32_t>(output.indices.size());
            command.index_count = 6;
            output.indices.insert(output.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
            output.commands.push_back(std::move(command));
        };
        quad(top, middle, .5f, 1.f, 1.f, fraction);
        quad(middle, bottom, 0.f, .5f, fraction, 0.f);
    }
    if (!control.text.empty() || control.scalar_text || control.kind == UiControlKind::edit_box) {
        auto font = fonts->slot(control.font, pixel_width);
        if (!font) {
            throw std::runtime_error(std::format("Original UI font: {}", font.error().reason));
        }
        UiTextStyle style;
        //512330 defaults to an unconstrained attachment;512680 preserves its
        //LEFT/CENTER/RIGHT anchor while using a virtual wide line. Constraint
        //state belongs to the actual attachment/controller binding.
        style.wrap = control.text_constraints.width;
        style.color = control.color;
        style.horizontal = static_cast<UiTextHorizontal>(std::clamp(control.horizontal_alignment, 0, 2));
        style.vertical = static_cast<UiTextVertical>(std::clamp(control.vertical_alignment, 0, 2));
        style.scale = { 2048.f / static_cast<float>(pixel_width), 1536.f / static_cast<float>(pixel_height) };
        //84B186 binds primary text to the outer rectangle;84B2DE binds
        //secondary text to the separately stored attachment rectangle.511F90
        //clamps a nonpositive surface depth to1 before deriving its Z parameter.
        style.depth = 1.f - static_cast<float>(std::max(1, control.primary_shadow_text ? control.primary_placement.depth : control.secondary_placement.depth)) / 40.f;
        const auto offset_area = [&](UiRect rectangle, UiPoint offset) {
            //512680 truncates logical offsets through physical pixel coordinates.
            //RIGHT subtracts from right/bottom; LEFT/CENTER add to left/top.
            const auto x = static_cast<std::int32_t>(static_cast<std::int64_t>(pixel_width) * offset.x / 2048);
            const auto y = static_cast<std::int32_t>(static_cast<std::int64_t>(pixel_height) * offset.y / 1536);
            const auto dx = static_cast<std::int32_t>(static_cast<float>(x) * style.scale[0]);
            const auto dy = static_cast<std::int32_t>(static_cast<float>(y) * style.scale[1]);
            if (style.horizontal == UiTextHorizontal::right) {
                rectangle.right -= dx;
                rectangle.bottom -= dy;
            } else {
                rectangle.left += dx;
                rectangle.top += dy;
            }
            return rectangle;
        };
        const bool explicit_offset = control.text_offsets_explicit || control.kind == UiControlKind::button || control.text_offset.x || control.text_offset.y || scrolling_text.contains(&control);
        //512330 uses the default constructor Y offset (native global12).
        //84A3E7 subsequently sends font-specific9/27 ONLY to the secondary
        //attachment; button/scroll callbacks may explicitly override text offsets.
        const auto secondary_offset = explicit_offset ? control.text_offset : control.primary_shadow_text ? UiPoint { 0, control.default_text_y_offset } : UiPoint { 0, control.font == 0 ? 27 : 9 };
        const auto text_surface = control.primary_shadow_text ? control.rect : control.attachment_rect;
        auto area = offset_area(text_surface, secondary_offset);
        auto text_clip = clipped(clip, text_surface);
        if (const auto scrolling = scrolling_text.find(&control); scrolling != scrolling_text.end()) {
            //52C4C0 lays out the BODY attachment; its direct child is only the
            //scrollbar. Keep the original opcode3 viewport as the clipping area.
            area = control.attachment_rect;
            style.wrap = true;
            text_clip = clipped(clip, area);
            style.vertical = UiTextVertical::top;
            const auto& scroll = required(&control, scrolling->second.scrollbar);
            control.text_offset = { 0, -scroll.value };
            area.top += control.text_offset.y;
            area.bottom = area.top + 100000;
        }
        auto displayed = control.password ? std::u16string(control.text.size(), u'*') : control.text;
        if (control.scalar_text) {
            match (*control.scalar_text) {
                case UiScalarTextKind::signed_integer  => displayed = number_text(control.value);
                case UiScalarTextKind::signed_currency => displayed = signed_cash_text(control.value);
                case UiScalarTextKind::percentage      => {
                    {
                        const std::array argument { BfstSignedIntegerArgument { 6, control.value } };
                        auto formatted = format_bfst_wide_string(c.messages.mail_language.ui_text, c.messages.mail_language.conversion, 959, 63, {}, argument);
                        if (!formatted) {
                            throw std::runtime_error(std::format("UI percentage: {}", formatted.error().reason));
                        }
                        displayed = std::move(*formatted);
                    }
                }
                case _ => {
                }
            }
        }
        const auto prefix_length = control.append_text_affix ? 0 : control.text_affix.size();
        if (control.append_text_affix) {
            displayed += control.text_affix;
        } else {
            displayed.insert(0, control.text_affix);
        }
        std::optional<UiModelPlacement> edit_overlay;
        if (control.kind == UiControlKind::edit_box) {
            //Binary CLIENTAREA writes native layout_rect_02c (84E07C). Both
            //text attachment binding and858B85's caret/scroll geometry use it;
            //the outer control rectangle belongs to its frame and hit region.
            const auto edit_client = control.attachment_rect;
            control.edit_pointer_y_bias = (**font).line_advance / 2;
            const auto line_height = std::max<std::int32_t>(1, static_cast<std::int32_t>(1536u * (**font).line_advance / pixel_height));
            const auto set_edit_area = [&] {
                area = offset_area(text_surface, { secondary_offset.x + control.edit_text_scroll.x, control.edit_vertical_scroll_applied ? control.edit_text_scroll.y : secondary_offset.y });
            };
            set_edit_area();
            auto measured = fonts->measure(**font, displayed, bounds(area), style);
            if (!measured) {
                throw std::runtime_error(std::format("UI edit metrics: {}", measured.error().reason));
            }
            //Font positions already include the attachment/client origin. Native
            //5149F0 returns local positions and859941 adds that SAME client origin;
            //rebasing them to control.rect shifts every inset editor's highlight.
            const auto position_index = [&](std::size_t raw, std::size_t count) {
                auto index = static_cast<std::int64_t>(raw);
                if (!control.append_text_affix) {
                    index += static_cast<std::int64_t>(prefix_length);
                } else if (index >= static_cast<std::int64_t>(count) - static_cast<std::int64_t>(control.text_affix.size())) {
                    index -= static_cast<std::int64_t>(control.text_affix.size());
                }
                return index < 0 || index >= static_cast<std::int64_t>(count) ? count - 1 : static_cast<std::size_t>(index);
            };
            const auto point = [&](std::size_t index) {
                if (measured->positions.empty()) {
                    return UiPoint { edit_client.left, edit_client.top };
                }
                const auto& p = measured->positions[position_index(index, measured->positions.size())];
                return UiPoint { static_cast<std::int32_t>(p[0]), static_cast<std::int32_t>(p[1]) };
            };
            const auto anchor_index = std::min(control.selection_begin, control.text.size());
            const auto active_index = std::min(control.selection_end, control.text.size());
            const auto old_scroll = control.edit_text_scroll;
            const auto old_vertical_applied = control.edit_vertical_scroll_applied;
            const auto active = point((control.flags & 128u) ? anchor_index : active_index);
            const auto local_x = active.x - edit_client.left;
            const auto local_y = active.y - edit_client.top;
            if (!(control.flags & 128u)) {
                //Native858B85 reserves eight logical units when the active endpoint
                //crosses the client edge; moving back to index0 restores the origin.
                if (local_x > edit_client.width()) {
                    control.edit_text_scroll.x += edit_client.width() - local_x - 8;
                } else if (local_x < 0) {
                    if (anchor_index && active_index) {
                        control.edit_text_scroll.x -= local_x + 8;
                    } else {
                        control.edit_text_scroll.x = 0;
                    }
                }
            } else {
                if (local_y + line_height > edit_client.height()) {
                    control.edit_text_scroll.y -= line_height * ((local_y + line_height - edit_client.height()) / line_height + 1);
                    control.edit_vertical_scroll_applied = true;
                } else if (local_y < 0) {
                    control.edit_text_scroll.y += line_height * (-local_y / line_height);
                    control.edit_vertical_scroll_applied = true;
                }
            }
            if (old_scroll.x != control.edit_text_scroll.x || old_scroll.y != control.edit_text_scroll.y || old_vertical_applied != control.edit_vertical_scroll_applied) {
                set_edit_area();
                measured = fonts->measure(**font, displayed, bounds(area), style);
                if (!measured) {
                    throw std::runtime_error(std::format("UI scrolled edit metrics: {}", measured.error().reason));
                }
            }
            const auto positions = std::make_shared<const std::vector<std::array<float, 2>>>(std::move(measured->positions));
            const auto affix_length = control.text_affix.size();
            const auto append_affix = control.append_text_affix;
            control.text_position = [positions, line_height, prefix_length, affix_length, append_affix](const UiControl&, std::size_t raw) {
                if (positions->empty()) {
                    return UiRect {};
                }
                auto index = static_cast<std::int64_t>(raw);
                if (!append_affix) {
                    index += static_cast<std::int64_t>(prefix_length);
                } else if (index >= static_cast<std::int64_t>(positions->size()) - static_cast<std::int64_t>(affix_length)) {
                    index -= static_cast<std::int64_t>(affix_length);
                }
                const auto at = index < 0 || index >= static_cast<std::int64_t>(positions->size()) ? positions->size() - 1 : static_cast<std::size_t>(index);
                const auto& p = (*positions)[at];
                const auto x = static_cast<std::int32_t>(p[0]);
                const auto y = static_cast<std::int32_t>(p[1]);
                return UiRect { x, y, x + 8, y + line_height };
            };
            if (control.edit_caret_asset_hash && ui.focus() == &control && control.state > 0) {
                const auto anchor = control.text_position(control, anchor_index);
                const auto endpoint = control.text_position(control, active_index);
                const bool selected = anchor_index != active_index;
                const auto right = selected ? endpoint.left : anchor.left + 8;
                UiRect rectangle { std::clamp(std::min(anchor.left, right), edit_client.left, edit_client.right), anchor.top, std::clamp(std::max(anchor.left, right), edit_client.left, edit_client.right), std::min(edit_client.bottom, selected && (control.flags & 128u) ? endpoint.bottom : anchor.bottom) };
                //Native858B85 orders only the X edges. Reverse multiline selection
                //retains anchorY/endpointY;4E68A0 renders its signed height (and uses
                //dimension+0.5 even at zero), rather than sorting or dropping it.
                {
                    UiModelPlacement placement;
                    placement.bounds = bounds(rectangle);
                    placement.clip = bounds(clipped(clip, edit_client));
                    placement.pivot = { static_cast<float>(control.primary_placement.pivot.x), static_cast<float>(control.primary_placement.pivot.y) };
                    placement.depth = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(control.render_depth + 3));
                    placement.rotation = static_cast<std::int16_t>(control.primary_placement.rotation);
                    edit_overlay = placement;
                }
            }
        }
        //54D770 explicitly creates primary black and secondary colored text
        //surfaces for cash, interaction cost and golden-ticket count. Their
        //offset comes from the two real rectangles, not a fixed drop-shadow delta.
        if (control.has_primary_text_attachment) {
            auto shadow = style;
            shadow.color = { 0, 0, 0, 0xBE };
            shadow.depth = 1.f - static_cast<float>(std::max(1, control.primary_placement.depth)) / 40.f;
            const auto primary_offset = explicit_offset ? control.text_offset : UiPoint { 0, control.default_text_y_offset };
            const auto shadow_area = offset_area(control.rect, primary_offset);
            const auto drawn = fonts->append(output, **font, displayed, bounds(shadow_area), shadow, bounds(clipped(clip, control.rect)));
            if (!drawn) {
                throw std::runtime_error(std::format("UI text shadow: {}", drawn.error().reason));
            }
        }
        if (control.primary_shadow_text || control.secondary_shadow_text) {
            //515FE0 paints gray/white into the surface; the attachment render then
            //modulates that surface by its current color. Direct glyph submission
            //must retain that modulation, including command-menu hover animation.
            auto shadow_style = style;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                shadow_style.color[channel] = static_cast<std::uint8_t>(static_cast<unsigned>(style.color[channel]) * (channel == 3 ? 200u : 128u) / 255u);
            }
            auto shadow_area = bounds(area);
            shadow_area[0] -= 2.f * style.scale[0];
            shadow_area[2] -= 2.f * style.scale[0];
            shadow_area[1] += 2.f * style.scale[1];
            shadow_area[3] += 2.f * style.scale[1];
            const auto shadow_drawn = fonts->append(output, **font, displayed, shadow_area, shadow_style, bounds(text_clip));
            if (!shadow_drawn) {
                throw std::runtime_error(std::format("UI shadow attachment: {}", shadow_drawn.error().reason));
            }
        }
        const auto drawn = fonts->append(output, **font, displayed, bounds(area), style, bounds(text_clip));
        if (!drawn) {
            throw std::runtime_error(std::format("UI text layout: {}", drawn.error().reason));
        }
        if (edit_overlay && !assets->append(output, control.edit_caret_asset_hash, *edit_overlay)) {
            const auto message = std::format("Missing original edit caret asset hash {}", control.edit_caret_asset_hash);
            if (std::ranges::find(diagnostics, message) == diagnostics.end()) {
                diagnostics.push_back(message);
            }
        }
    }
    presenter->append(output, control, clip, pixel_width, pixel_height);
    if (staff) {
        staff->append(output, control, clip, pixel_width, pixel_height);
    }
    if (objects) {
        objects->append(output, control, clip, pixel_width, pixel_height);
    }
    if (build) {
        build->append(output, control, clip, pixel_width, pixel_height);
    }
    if (visitors) {
        visitors->append(output, control, clip, pixel_width, pixel_height);
    }
    const auto children = control.children;
    for (auto* child : children) {
        if (ui.owns(child)) {
            draw_control(*child, output, clip, pixel_width, pixel_height);
        }
    }
}

WorldUiRuntime::WorldUiRuntime(WorldRuntime& c, const std::filesystem::path& root, std::function<void()> reset_interaction, ParkConstructionRuntime* construction) : state_(std::make_unique<State>(c, root, std::move(reset_interaction), construction)) {}

WorldUiRuntime::~WorldUiRuntime() = default;

UiRuntime& WorldUiRuntime::controls() {
    return state_->ui;
}

const UiRuntime& WorldUiRuntime::controls() const {
    return state_->ui;
}

UiControl* WorldUiRuntime::primary_root() const {
    return state_->primary;
}

UiControl* WorldUiRuntime::secondary_root() const {
    return state_->secondary;
}

std::array<std::uint32_t, 2> WorldUiRuntime::viewport_size() const {
    return state_->viewport_pixels;
}

std::size_t WorldUiRuntime::drain_input() {
    std::size_t total {};
    //A secondary lifecycle/layout callback can enqueue primary application
    //work. Preserve each queue's existing FIFO order and drain both phases
    //to quiescence before handing the next input to another World owner.
    while (const auto count = state_->ui.drain(std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::size_t>::max())) {
        total += count;
    }
    return total;
}

void WorldUiRuntime::advance(std::chrono::milliseconds elapsed) {
    if (!std::exchange(state_->frame_polled, false)) {
        if (auto poll = state_->frame_poll) {
            poll();
        }
    }
    state_->ui.update(elapsed);
    state_->staff->advance();
    if (state_->objects) {
        state_->objects->advance();
    }
    if (state_->build) {
        state_->build->advance();
    }
    if (state_->coaster) {
        state_->coaster->advance();
    }
    state_->visitors->advance();
    state_->mail->advance();
    if (state_->management) {
        state_->management->advance(elapsed);
    }
    state_->refresh_hud(false, true);
    state_->update_tooltip();
}

void WorldUiRuntime::bind_gameplay_pause(std::function<bool()> provider) {
    state_->gameplay_paused = std::move(provider);
}

void WorldUiRuntime::bind_frame_poll(std::function<void()> poll) {
    state_->frame_poll = std::move(poll);
}

void WorldUiRuntime::begin_frame() {
    state_->frame_polled = true;
    if (auto poll = state_->frame_poll) {
        poll();
    }
}

void WorldUiRuntime::set_action_handler(std::function<void(const WorldUiAction&)> handler) {
    state_->action_handler = std::move(handler);
}

std::vector<WorldUiAction> WorldUiRuntime::take_actions() {
    return std::exchange(state_->pending_actions, {});
}

void WorldUiRuntime::request_action(const WorldUiAction& action) {
    state_->request(action);
}

void WorldUiRuntime::dispatch_mail_action(const WorldMailUiAction& action) {
    state_->dispatch_mail(action);
}

void WorldUiRuntime::close_context_menus() {
    auto& s = *state_;
    s.reset_menu();
    s.staff->close_context();
    if (s.objects) {
        s.objects->close_context();
    }
    s.visitors->close_context();
}

void WorldUiRuntime::set_interaction_cost(std::int32_t value) {
    state_->interaction_cost = value;
}

const std::vector<std::string>& WorldUiRuntime::diagnostics() const {
    return state_->diagnostics;
}

void WorldUiRuntime::open_objectives() {
    state_->open_objectives();
}

void WorldUiRuntime::close_panel() {
    state_->dismiss_panels();
}

WorldStaffUiController& WorldUiRuntime::staff_controller() {
    return *state_->staff;
}

WorldParkObjectUiController& WorldUiRuntime::object_controller() {
    return require_bound(state_->objects, "Park-object UI requires its construction owner");
}

WorldBuildUiController& WorldUiRuntime::build_controller() {
    return require_bound(state_->build, "Build UI requires its construction owner");
}

WorldCoasterEditorUiController& WorldUiRuntime::coaster_editor_controller() {
    return require_bound(state_->coaster, "Coaster UI requires its editor owner");
}

WorldVisitorsUiController& WorldUiRuntime::visitor_controller() {
    return *state_->visitors;
}

WorldMailUiController& WorldUiRuntime::mail_controller() {
    return *state_->mail;
}

WorldManagementUiController& WorldUiRuntime::management_controller() {
    return require_bound(state_->management, "Management UI has no application settings owner");
}

WorldUiPanelHost WorldUiRuntime::panel_host() const {
    return state_->panels;
}

void WorldUiRuntime::dispatch_command(WorldUiCommand command) {
    state_->command(command);
}

void WorldUiRuntime::bind_management(WorldManagementUiHost host) {
    auto& s = *state_;
    s.management.reset();
    host.panels = s.panels;
    if (host.language.empty()) {
        host.language = "American";
    }
    host.open_objectives = [&s] {
        s.open_objectives();
    };
    host.bind_scrollable_text = [&s](UiControl& body, std::optional<std::uint32_t> scrollbar) {
        s.bind_scrollable_text(body, scrollbar);
    };
    s.management = std::make_unique<WorldManagementUiController>(s.c, s.ui, s.primary, s.root, std::move(host));
}

void WorldUiRuntime::bind_construction_actions(std::function<bool(std::uint16_t)> object, std::function<bool(const std::filesystem::path&, std::int32_t)> design, std::function<void(ThingId)> retire) {
    state_->begin_object_build = std::move(object);
    state_->begin_coaster_design = std::move(design);
    state_->retire_authoring = std::move(retire);
}

void WorldUiRuntime::bind_object_actions(std::function<bool(ThingId)> queue, std::function<bool(ThingId)> track, std::function<void(ThingId)> ride, std::function<bool(ThingId)> relocate, std::function<bool(ThingId, CoasterEditorEntryTool)> coaster_tool) {
    state_->edit_object_queue = std::move(queue);
    state_->edit_object_track = std::move(track);
    state_->ride_object = std::move(ride);
    state_->relocate_object = std::move(relocate);
    state_->edit_coaster_tool = std::move(coaster_tool);
}

void WorldUiRuntime::bind_toolbar_actions(std::function<bool(std::uint32_t, bool)> handler) {
    state_->select_toolbar_tool = std::move(handler);
}

bool WorldUiRuntime::select_toolbar_tool(std::uint32_t id) {
    if (id < 65 || id > 68) {
        return false;
    }
    auto& s = *state_;
    auto& group = s.required(&s.required(s.primary, 62), 64);
    if (group.selected_id == 65 && id != 65) {
        s.toolbar_selection(65, false);
    }
    s.select_toolbar_presentation(id);
    s.toolbar_selection(id, true);
    return true;
}

void WorldUiRuntime::preferences(const UiPreferences& options) {
    state_->tooltips_enabled = options.tooltips_on != 0;
    if (state_->tooltip) {
        state_->tooltip->enabled(state_->tooltips_enabled);
    }
    state_->staff->preferences(options);
    if (state_->objects) {
        state_->objects->preferences(options);
    }
    state_->visitors->preferences(options);
    state_->c.advisor.presentation.advisor_on = options.advisor_on != 0;
    state_->c.advisor.presentation.tutorial_on = options.tutorial_on != 0;
}

void WorldUiRuntime::bind_scrollable_text(UiControl& body, std::optional<std::uint32_t> scrollbar) {
    state_->bind_scrollable_text(body, scrollbar);
}

void WorldUiRuntime::set_draw_underlay(std::function<void(UiDrawList&, std::uint32_t, std::uint32_t)> callback) {
    state_->draw_underlay = std::move(callback);
}

void WorldUiRuntime::set_control_draw_hook(std::function<void(UiDrawList&, const UiControl&, UiRect, std::uint32_t, std::uint32_t)> callback) {
    state_->control_draw_hook = std::move(callback);
}

void WorldUiRuntime::set_shortcut_handler(std::function<bool(const UiEvent&, WorldUiShortcutRoute)> handler) {
    state_->shortcut_handler = std::move(handler);
}

void WorldUiRuntime::set_tooltip_override(std::optional<std::u16string> text) {
    state_->tooltip_override = std::move(text);
}

void WorldUiRuntime::set_world_cursor_help(std::u16string text) {
    state_->world_cursor_help = std::move(text);
}

void WorldUiRuntime::set_tooltip_offsets(std::optional<UiPoint> offsets) {
    state_->tooltip_offsets = offsets;
    if (!offsets) {
        state_->tooltip.reset(); //Restore actual constructor/SAM defaults.
    } else if (state_->tooltip) {
        state_->tooltip->offsets(*offsets);
    }
}

bool WorldUiRuntime::simulation_paused() const {
    return state_->staff->simulation_paused() || (state_->objects && state_->objects->simulation_paused()) || (state_->management && state_->management->simulation_paused());
}

bool WorldUiRuntime::dismiss_outside(UiPoint point) {
    if (state_->staff->dismiss_outside(point)) {
        return true;
    }
    if (state_->visitors->dismiss_outside(point)) {
        return true;
    }
    return state_->objects && state_->objects->dismiss_outside(point);
}

void WorldUiRuntime::hide_persistent() {
    close_panel();
    state_->hidden = true;
    state_->ui.set_visible(*state_->primary, false);
    state_->ui.set_visible(*state_->secondary, false);
}

void WorldUiRuntime::restore_persistent() {
    state_->hidden = false;
    state_->ui.set_visible(*state_->secondary, false);
    state_->ui.set_input_root(nullptr);
    state_->ui.set_visible(*state_->primary, true);
    state_->ui.set_focus(state_->primary);
    state_->refresh_hud(true);
}

UiControl* WorldUiRuntime::create_playback_window() {
    auto& s = *state_;
    //The animation manager destroys its completed window. A later panel can
    //reuse that address, so a bare owns(pointer) must not authorize its
    //deletion.
    if (s.playback_serial && s.ui.lifetime_serial(s.playback) == s.playback_serial) {
        s.ui.destroy(s.playback);
    }
    //5D0400 appends directly to the event-routing root, independently of both
    //hidden HUDs. Showing the secondary HUD here exposed its postcard/return
    //buttons throughout the intro and other scripted camera sequences.
    s.playback = &s.ui.create(nullptr, UiControlKind::window, 1, 0, { 0, 0, 2047, 1535 });
    s.playback_serial = s.ui.lifetime_serial(s.playback);
    s.ui.set_input_root(s.playback);
    //5D0400 focuses this window only through the caller's skippable profile2
    //branch. Profile1 leaves focus to the Intro root/Skip handler.
    return s.playback;
}

void WorldUiRuntime::configure_control_particles(UiControl* control) {
    auto& s = *state_;
    s.highlight = s.ui.owns(control) ? control : nullptr;
}

void WorldUiRuntime::animate_mail(UiControl* control, std::int32_t kind, std::chrono::milliseconds interval) {
    auto& s = *state_;
    if (!s.ui.owns(control)) {
        return;
    }
    if (kind < 0 || kind > 2 || interval.count() < 0) {
        throw std::invalid_argument("Mail animation contract");
    }
    s.ui.set_visible(*control, true);
    s.mail_animation = { true, kind, 0, interval, s.ui.clock() };
}

void WorldUiRuntime::set_advisor_lighting(MeshPoint ambient, std::uint32_t diffuse_mask) {
    if (!std::ranges::all_of(ambient, [](float value) {
            return std::isfinite(value);
        })) {
        throw std::invalid_argument("Advisor ambient lighting is nonfinite");
    }
    auto& s = *state_;
    s.advisor_lighting.ambient = ambient;
    s.advisor_lighting.diffuse_mask = diffuse_mask;
    s.advisor_lighting_bound = true;
}

void WorldUiRuntime::build_draw(UiDrawList& output, std::uint32_t width, std::uint32_t height) {
    if (!width || !height) {
        throw std::invalid_argument("World UI drawable dimensions must be nonzero");
    }
    auto& s = *state_;
    s.viewport_pixels = { width, height };
    if (!s.ui.presentation_visible()) {
        return;
    }
    s.update_tooltip();
    if (!s.assets) {
        s.assets = std::make_unique<UiAssetLibrary>(s.root, "American", s.c.resources.meshes ? s.c.resources.meshes->texture_set() : MeshTextureSet::full, s.c.resources.meshes ? s.c.resources.meshes->animated_textures() : true);
    }
    s.assets->animated_textures(s.c.resources.meshes ? s.c.resources.meshes->animated_textures() : true);
    if (!s.gradient) {
        const auto texture = s.assets->texture("hapgradient.tga");
        if (!texture) {
            throw std::runtime_error(describe_texture_error(texture.error()));
        }
        s.gradient = *texture;
    }
    if (!s.fonts) {
        s.fonts = std::make_unique<UiFontLibrary>(s.root);
    }
    if (!s.presenter) {
        s.presenter = std::make_unique<UiControlPresenter>(*s.assets, *s.fonts, [state = &s](std::int32_t value) {
            return state->signed_cash_text(value);
        });
    }
    output.clear();
    output.canvas_width = 2048;
    output.canvas_height = 1536;
    //Measure and update ranges before drawing any attachment. The first paint
    //therefore uses the correct thumb, clipping and scroll position.
    s.prepare_scrollable_text(width, height);
    if (s.management) {
        s.management->append(output, { 0, 0, 2048, 1536 }, width, height);
    }
    if (s.draw_underlay) {
        s.draw_underlay(output, width, height);
    }
    const auto roots = s.ui.roots();
    for (auto* control : roots) {
        if (s.ui.owns(control)) {
            s.draw_control(*control, output, { 0, 0, 2048, 1536 }, width, height);
        }
    }
    if (!s.advisor_scene) {
        s.advisor_scene = std::make_unique<AdvisorUiScene>(s.c, s.root);
    }
    if (s.advisor_scene->registered()) {
        if (!s.advisor_lighting_bound && s.advisor_lighting_source != s.c.source.selected_resource_source_name) {
            s.advisor_lighting = load_installed_mesh_lighting(s.root, s.c.source.selected_resource_source_name);
            s.advisor_lighting_source = s.c.source.selected_resource_source_name;
        }
        //Native category1 Advisor and category3 attachments share depth. Keep
        //the registered mesh's native projection, but assign a separate layer
        //so its varying Z cannot intersect a control's frame/text/button depths.
        //Attachment118 is the separate 2D fallback, not this consumer's gate.
        s.advisor_scene->append(output, s.advisor_lighting);
    }
    for (const auto& diagnostic : s.advisor_scene->diagnostics()) {
        if (std::ranges::find(s.diagnostics, diagnostic) == s.diagnostics.end()) {
            s.diagnostics.push_back(diagnostic);
        }
    }
    for (const auto& diagnostic : s.assets->diagnostics()) {
        if (std::ranges::find(s.diagnostics, diagnostic) == s.diagnostics.end()) {
            s.diagnostics.push_back(diagnostic);
        }
    }
}
} //namespace simcoaster
