#include "simcoaster/world_ui_shortcuts.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/ui_control.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_objects.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include <format>
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace simcoaster {
std::uint16_t normalize_world_ui_platform_key(std::uint32_t virtual_key, std::uint32_t mapped_character, bool extended) {
    //4D0960 bypasses MapVirtualKey for these five native numpad operators.
    if (virtual_key == 107 || virtual_key == 109 || virtual_key == 106 || virtual_key == 111 || virtual_key == 110) {
        return static_cast<std::uint16_t>((virtual_key & 255u) << 8);
    }
    const auto key = mapped_character ? mapped_character : virtual_key;
    return static_cast<std::uint16_t>(extended ? (key & 255u) << 8 : key);
}

std::uint32_t world_ui_platform_key_flags(std::uintptr_t lparam, std::uint32_t modifiers) {
    return static_cast<std::uint32_t>(lparam & 65535u) | (modifiers << 16) | ((lparam & (std::uintptr_t { 1 } << 30)) ? std::uint32_t { 1 } << 24 : 0u) | ((lparam & (std::uintptr_t { 1 } << 24)) ? world_ui_extended_key_flag : 0u);
}

namespace {
enum class Callback {
    none,
    menu,
    build,
    hire,
    park_statistics,
    all_items_noop,
    staff_list,
    visitor_list,
    finances,
    loans,
    gates,
    research,
    park_map,
    peep_location_noop,
    postcard,
    debug,
    mail,
    training,
    objectives,
    challenges,
    tips,
    cancel,
    rotate_positive,
    rotate_negative,
    delete_mode,
    hud,
    backtrack,
    speed_down,
    speed_up,
    speed_reset,
    close_park,
    load,
    save,
    advisor,
    success_noop
};

struct Entry {
    std::uint16_t action {}, key {}, modifiers {};
    Callback callback {};
    bool active {};
};

//Original9A88C0, including four declared zero rows. IDs9,13,16,22 are
//absent:430C20's attempted localized updates do not create missing entries.
//Literal function-key WORDs112/123 match4D0960's unextended raw-VK fallback.
//Platform normalization is independent of these original table identities.
constexpr std::array<Entry, 24> shortcut_rows { { { 0, 27, 0, Callback::menu },
                                                  { 1, 66, 0, Callback::build },
                                                  { 2, 72, 0, Callback::hire },
                                                  { 3, 73, 0, Callback::park_statistics },
                                                  { 4, 65, 0, Callback::all_items_noop },
                                                  { 5, 83, 0, Callback::staff_list },
                                                  { 6, 86, 0, Callback::visitor_list },
                                                  { 7, 70, 0, Callback::finances },
                                                  { 8, 76, 0, Callback::loans },
                                                  { 10, 71, 0, Callback::gates },
                                                  { 11, 82, 0, Callback::research },
                                                  { 12, 32, 0, Callback::park_map },
                                                  { 14, 86, 12, Callback::peep_location_noop },
                                                  { 15, 80, 12, Callback::postcard },
                                                  { 17, 123, 0, Callback::debug },
                                                  { 18, 77, 0, Callback::mail },
                                                  { 19, 84, 0, Callback::training },
                                                  { 20, 79, 0, Callback::objectives },
                                                  { 21, 67, 0, Callback::challenges },
                                                  { 23, 112, 0, Callback::tips },
                                                  {},
                                                  {},
                                                  {},
                                                  {} } };
//Original9A8520, including its trailing zero row and missing action14.
constexpr std::array<Entry, 19> game_rows { { { 0, 27, 0, Callback::cancel },
                                              { 1, 9984, 12, Callback::rotate_positive },
                                              { 2, 9472, 12, Callback::rotate_negative },
                                              { 3, 44, 3, Callback::rotate_positive },
                                              { 4, 46, 3, Callback::rotate_negative },
                                              { 5, 44, 0, Callback::rotate_positive },
                                              { 6, 46, 0, Callback::rotate_negative },
                                              { 7, 11776, 0, Callback::delete_mode },
                                              { 8, 114, 0, Callback::hud },
                                              { 9, 8, 0, Callback::backtrack },
                                              { 10, 27904, 12, Callback::speed_down },
                                              { 11, 27392, 12, Callback::speed_up },
                                              { 12, 27136, 12, Callback::speed_reset },
                                              { 13, 67, 12, Callback::close_park },
                                              { 15, 79, 12, Callback::load },
                                              { 16, 83, 12, Callback::save },
                                              { 17, 112, 0, Callback::advisor },
                                              { 18, 86, 48, Callback::success_noop },
                                              {} } };

std::vector<std::byte> read_keyboard(const std::filesystem::path& path) {
    std::ifstream file { path, std::ios::binary | std::ios::ate };
    if (!file) {
        throw std::runtime_error(std::format("Keyboard shortcuts unavailable: {}", path.native_encoded_string()));
    }
    const auto length = file.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Invalid keyboard shortcut table size");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Unable to read keyboard shortcut table");
    }
    return bytes;
}

template <class Entries>
auto find_key(Entries& entries, std::uint16_t key, std::uint16_t modifiers) {
    return std::ranges::find_if(entries, [=](const auto& entry) {
        return entry.key == key && (entry.modifiers == modifiers || entry.modifiers == 64);
    });
}
} //namespace

struct WorldUiShortcuts::State {
    WorldRuntime& c;
    WorldUiShortcutHost host;
    std::array<Entry, 24> shortcuts = shortcut_rows;
    std::array<Entry, 19> game = game_rows;

    State(WorldRuntime& context, const std::filesystem::path& root, WorldUiShortcutHost callbacks) : c(context), host(std::move(callbacks)) {
        if (!host.close_context_menu || !host.command || !host.action || host.language.empty()) {
            throw std::invalid_argument("Keyboard shortcuts require complete owners");
        }
        const auto bytes = read_keyboard(root / "Data/Language" / host.language / "KEYBOARD.str");
        const auto localize = [&](auto& entries, std::uint16_t action, std::uint32_t row) {
            //430B00 bounds the requested action by the declared family count,
            //then uses first matching action identity. Missing actions stay absent.
            if (action >= entries.size()) {
                return;
            }
            const auto found = std::ranges::find(entries, action, &Entry::action);
            if (found == entries.end()) {
                return;
            }
            auto text = format_bfst_wide_string(bytes, c.messages.mail_language.conversion, row, 2);
            if (!text) {
                throw std::runtime_error(std::format("Keyboard shortcut row {}: {}", row, text.error().reason));
            }
            found->key = text->empty() ? 0 : static_cast<std::uint16_t>((*text)[0]);
        };
        //Exact430DA0..431012 action-to-KEYBOARD.str bindings. Several action
        //IDs differ from their row position and must be searched by identity.
        for (const auto [action, row] : std::array<std::pair<std::uint16_t, std::uint32_t>, 18> { { { 1, 0 }, { 2, 1 }, { 3, 2 }, { 4, 3 }, { 5, 4 }, { 6, 5 }, { 7, 6 }, { 8, 7 }, { 9, 8 }, { 10, 9 }, { 11, 10 }, { 13, 12 }, { 14, 13 }, { 15, 14 }, { 18, 18 }, { 19, 8 }, { 20, 19 }, { 21, 11 } } }) {
            localize(shortcuts, action, row);
        }
        for (const auto [action, row] : std::array<std::pair<std::uint16_t, std::uint32_t>, 8> { { { 3, 20 }, { 4, 21 }, { 5, 20 }, { 6, 21 }, { 13, 16 }, { 14, 17 }, { 15, 24 }, { 16, 23 } } }) {
            localize(game, action, row);
        }
    }

    bool enabled(WorldUiShortcutFamily family) const {
        return host.enabled ? host.enabled(family) : c.advisor.presentation.commands_enabled;
    }

    bool action(WorldUiShortcutAction request, std::int32_t argument = 0) {
        return host.action(request, argument);
    }

    bool panel(WorldUiCommand command) {
        if (c.mode.game == GameMode::code_3) {
            return false;
        }
        host.close_context_menu();
        host.command(command);
        return true;
    }

    bool training() {
        if (c.mode.game == GameMode::code_3 || !c.simulation.current) {
            return false;
        }
        const auto helper = c.simulation.current->saved.helper_23;
        if (!helper || !c.entities.at(helper).payload) {
            return false; //42EF9F only tests the independent type23 getter for null.
        }
        auto* world = c.referenced_world();
        if (!world) {
            return false;
        }
        std::size_t traversed {};
        for (auto id = world->saved.first_park_object; id;) {
            if (++traversed > c.entities.size()) {
                throw std::logic_error("Cyclic Training Room shortcut object chain");
            }
            const auto* header = c.entities.at(id).payload;
            if (!header || header->kind != ThingKind::park_object) {
                throw std::logic_error("Invalid Training Room shortcut object chain");
            }
            const auto& object = *reinterpret_cast<const ParkObjectRuntimeView*>(header);
            if ((std::to_underlying(object.capabilities) & std::to_underlying(ParkObjectCapabilities::training_room)) != 0) {
                (void)action(WorldUiShortcutAction::open_training_room, id);
                return true;
            }
            id = object.next_park_object;
        }
        return false;
    }

    bool invoke(Callback callback) {
        using A = WorldUiShortcutAction;
        using C = WorldUiCommand;
        match (callback) {
            case Callback::none => return false;
            case Callback::menu => {
                //42FCD0 routes a running global animation to its native skip request,
                //rather than opening a second menu over the scene.
                if (c.animation.thing) {
                    if (c.animation.control.skip_enabled) {
                        c.animation.control.completion_requested = 1;
                    }
                } else {
                    host.close_context_menu();
                    host.command(C::game_menu);
                }
                return true;
            }
            case Callback::build                                          => return panel(C::attraction_build);
            case Callback::hire                                           => return panel(C::staff_hire);
            case Callback::park_statistics                                => return panel(C::park_statistics);
            case Callback::staff_list                                     => return panel(C::staff_list);
            case Callback::visitor_list                                   => return panel(C::visitor_list);
            case Callback::finances                                       => return panel(C::financial_statistics);
            case Callback::loans                                          => return panel(C::loans);
            case Callback::gates                                          => return panel(C::park_gates);
            case Callback::park_map                                       => return panel(C::park_map);
            case Callback::all_items_noop or Callback::peep_location_noop => {
                //4F8740 and4F8840 are literal RET stubs, not missing panel owners.
                if (c.mode.game == GameMode::code_3) {
                    return false;
                }
                host.close_context_menu();
                return true;
            }
            case Callback::research => {
                if (c.mode.game == GameMode::code_3) {
                    return false;
                }
                host.close_context_menu();
                for (const auto definition : std::array<std::uint16_t, 3> { 1404, 2405, 3420 }) {
                    if (find_first_park_object_record_id_by_definition_id(c, definition)) {
                        host.command(C::research);
                        break;
                    }
                }
                return true; //430218 succeeds even when there is no research building.
            }
            case Callback::postcard => {
                host.close_context_menu();
                host.command(C::postcard);
                return true;
            }
            case Callback::debug => {
                if (c.debug.cheat_commands.cheat_commands_enabled) {
                    (void)action(A::open_debug_panel_selector);
                }
                return true;
            }
            case Callback::mail => {
                if (c.mode.game == GameMode::code_3) {
                    return false;
                }
                host.command(C::mail);
                return true;
            }
            case Callback::training   => return training();
            case Callback::objectives => {
                if (c.mode.game == GameMode::code_3) {
                    return false;
                }
                (void)action(A::open_objectives);
                return true;
            }
            case Callback::challenges => {
                if (c.mode.game == GameMode::code_3) {
                    return false;
                }
                host.command(C::challenges);
                return true;
            }
            case Callback::tips => {
                (void)action(A::show_tip_of_the_day);
                return true;
            }
            case Callback::cancel          => return action(A::cancel_interaction);
            case Callback::rotate_positive => {
                (void)action(A::rotate_clockwise);
                return true;
            }
            case Callback::rotate_negative => {
                (void)action(A::rotate_counterclockwise);
                return true;
            }
            case Callback::delete_mode => {
                host.close_context_menu();
                (void)action(A::enter_delete_mode);
                return true;
            }
            case Callback::hud => {
                (void)action(A::toggle_hud);
                return true;
            }
            case Callback::backtrack => {
                (void)action(A::backtrack);
                return true;
            }
            case Callback::speed_down => {
                (void)action(A::decrease_game_speed);
                return true;
            }
            case Callback::speed_up => {
                (void)action(A::increase_game_speed);
                return true;
            }
            case Callback::speed_reset => {
                (void)action(A::reset_game_speed);
                return true;
            }
            case Callback::close_park => {
                (void)action(A::close_park);
                return true;
            }
            case Callback::load => {
                if (c.mode.game != GameMode::code_3) {
                    (void)action(A::open_load_dialog);
                }
                return true; //42F515 returns1 even for the mode3 no-op.
            }
            case Callback::save => {
                (void)action(c.mode.game == GameMode::code_3 ? A::open_coaster_name_dialog : A::open_save_dialog);
                return true;
            }
            case Callback::advisor => {
                (void)action(A::activate_advisor_special_candidate);
                return true;
            }
            case Callback::success_noop => {
                return true; //430610 literally returns1 without reading any state.
            }
            case _ => {
            }
        } throw std::logic_error("Unknown original keyboard callback");
    }

    template <class Entries>
    bool dispatch(Entries& entries, WorldUiShortcutFamily family, std::uint16_t key, std::uint16_t modifiers, bool down) {
        if (!enabled(family)) {
            for (auto& entry : entries) {
                entry.active = false;
            }
            return false;
        }
        auto found = find_key(entries, key, modifiers);
        if (found == entries.end()) {
            return false;
        }
        //All43 original rows have null on_key_down. Their active flags still
        //change, but a matching KEYDOWN does not consume the command chain.
        if (down) {
            found->active = true;
            return false;
        }
        const auto result = invoke(found->callback);
        found->active = false;
        return result;
    }
};

WorldUiShortcuts::WorldUiShortcuts(WorldRuntime& c, const std::filesystem::path& root, WorldUiShortcutHost host) : state_(std::make_unique<State>(c, root, std::move(host))) {}

WorldUiShortcuts::~WorldUiShortcuts() = default;

std::optional<std::uint16_t> WorldUiShortcuts::action_for_key(WorldUiShortcutFamily family, std::uint16_t key, std::uint16_t modifiers) const {
    const auto find = [&](const auto& entries) -> std::optional<std::uint16_t> {
        const auto found = find_key(entries, key, modifiers);
        return found == entries.end() ? std::nullopt : std::optional { found->action };
    };
    return family == WorldUiShortcutFamily::shortcuts ? find(state_->shortcuts) : find(state_->game);
}

bool WorldUiShortcuts::active(WorldUiShortcutFamily family, std::uint16_t action) const {
    if (!state_->enabled(family)) {
        return false;
    }
    const auto find = [&](const auto& entries) {
        const auto found = std::ranges::find(entries, action, &Entry::action);
        return found != entries.end() && found->active;
    };
    return family == WorldUiShortcutFamily::shortcuts ? find(state_->shortcuts) : find(state_->game);
}

void WorldUiShortcuts::clear_active() {
    for (auto& entry : state_->shortcuts) {
        entry.active = false;
    }
    for (auto& entry : state_->game) {
        entry.active = false;
    }
}

bool WorldUiShortcuts::key(const UiEvent& event, WorldUiShortcutRoute route) {
    if (event.type != 65546 && event.type != 65547) {
        return false;
    }
    auto& s = *state_;
    const auto key = s.host.native_key_code ? s.host.native_key_code(event) : static_cast<std::uint16_t>(event.id);
    const auto modifiers = static_cast<std::uint16_t>((static_cast<std::uintptr_t>(event.value) >> 16u) & 255u);
    const bool down = event.type == 65546;
    if (route == WorldUiShortcutRoute::secondary || route == WorldUiShortcutRoute::primary) {
        if (!down && (s.c.view.flags & 22u) && (key == 27 || action_for_key(WorldUiShortcutFamily::shortcuts, key, modifiers) == 16)) {
            (void)s.action(WorldUiShortcutAction::exit_special_view);
            return true;
        }
        if (route == WorldUiShortcutRoute::secondary) {
            if (!down && action_for_key(WorldUiShortcutFamily::shortcuts, key, modifiers) == 15) {
                //507DDB calls enter_postcard_mode directly, unlike42FCA0: no
                //context-menu close is inserted on this secondary-root path.
                s.host.command(WorldUiCommand::postcard);
                return true;
            }
            return false;
        }
    }
    if (route == WorldUiShortcutRoute::hidden_hud) {
        if (!down) {
            if (s.c.simulation.current && s.c.simulation.current->saved.state == WorldState::transition) {
                if (key == 27 && !modifiers) {
                    s.host.command(WorldUiCommand::game_menu);
                    return true;
                }
            } else if ((key == 27 && !modifiers) || action_for_key(WorldUiShortcutFamily::game, key, modifiers) == 8) {
                (void)s.action(WorldUiShortcutAction::toggle_hud);
                return true;
            } else if (action_for_key(WorldUiShortcutFamily::shortcuts, key, modifiers) == 15) {
                s.host.command(WorldUiCommand::postcard);
                return true;
            }
        }
        return false;
    }
    if (route == WorldUiShortcutRoute::panel && !down) {
        //507FA0 handles these before dispatching shortcuts. Escape belongs to
        //the concrete panel's close handler; it must never open the game menu.
        if (key == 27) {
            return false;
        }
        if (key == 112 && !modifiers) {
            (void)s.action(WorldUiShortcutAction::activate_advisor_special_candidate);
            return true;
        }
    }
    if (s.dispatch(s.shortcuts, WorldUiShortcutFamily::shortcuts, key, modifiers, down)) {
        return true;
    }
    return route == WorldUiShortcutRoute::primary && s.dispatch(s.game, WorldUiShortcutFamily::game, key, modifiers, down);
}
} //namespace simcoaster
