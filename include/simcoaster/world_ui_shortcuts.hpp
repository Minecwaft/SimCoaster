#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace simcoaster {
struct WorldRuntime;
struct UiEvent;
enum class WorldUiCommand : std::int32_t;
//UiEvent.value retains native repeat state in bit24 and modifier BYTE2.
//The Win32 extended-key bit is carried separately so4D0960 can distinguish
//an extended cursor key from the same unextended virtual-key code.
inline constexpr std::uint32_t world_ui_extended_key_flag = std::uint32_t{ 1 } << 25;
std::uint16_t normalize_world_ui_platform_key(std::uint32_t virtual_key, std::uint32_t mapped_character, bool extended);
std::uint32_t world_ui_platform_key_flags(std::uintptr_t lparam, std::uint32_t modifiers);
enum class WorldUiShortcutFamily {
    shortcuts,
    game
};
enum class WorldUiShortcutRoute {
    primary,
    panel,
    secondary,
    hidden_hud
};
enum class WorldUiShortcutAction {
    exit_special_view,
    open_objectives,
    open_training_room, //Argument is the live first Training Room ThingId.
    show_tip_of_the_day,
    open_debug_panel_selector,
    cancel_interaction,
    rotate_clockwise,
    rotate_counterclockwise,
    enter_delete_mode,
    toggle_hud,
    backtrack,
    decrease_game_speed,
    increase_game_speed,
    reset_game_speed,
    close_park,
    open_load_dialog,
    open_save_dialog,
    open_coaster_name_dialog,
    activate_advisor_special_candidate
};

struct WorldUiShortcutHost {
    std::string language = "American";
    std::function<void()> close_context_menu;
    std::function<void(WorldUiCommand)> command;
    //Returns the actual callback result (e.g. cancel returns false for the
    //base handler). The argument is zero except open_training_room.
    std::function<bool(WorldUiShortcutAction, std::int32_t)> action;
    //UiRuntime retains physical VK<<8 IDs for editable widgets. Normalize
    //only at the command-family boundary, using4D0960's mapped character or
    //raw VK fallback and the original extended-key flag. Unextended F1/F3/F12
    //remain raw112/114/123; the five numpad operators are always packed.
    //If absent, input IDs are already in the native normalized namespace.
    std::function<std::uint16_t(const UiEvent&)> native_key_code;
    //If absent, the saved command-enabled policy in WorldRuntime is used.
    std::function<bool(WorldUiShortcutFamily)> enabled;
};

//Owns the original9A88C0/9A8520 tables and their KEYBOARD.str rebindings.
//Call only after the focused widget/controller and the camera family have
//had their native chance to handle the event. Panels dispatch shortcuts
//only; secondary roots perform restricted lookups, never the game family.
class WorldUiShortcuts {
    struct State;
    std::unique_ptr<State> state_;

public:
    WorldUiShortcuts(WorldRuntime&, const std::filesystem::path& game_root, WorldUiShortcutHost);
    ~WorldUiShortcuts();
    WorldUiShortcuts(const WorldUiShortcuts&) = delete;
    WorldUiShortcuts& operator=(const WorldUiShortcuts&) = delete;
    bool key(const UiEvent&, WorldUiShortcutRoute);
    std::optional<std::uint16_t>
    action_for_key(WorldUiShortcutFamily, std::uint16_t native_key, std::uint16_t modifiers) const;
    bool active(WorldUiShortcutFamily, std::uint16_t action) const;
    //Match430BE0 when the host disables command families or loses input.
    void clear_active();
};
} //namespace simcoaster
