#pragma once
#include "simcoaster/ui_modal_scope.hpp"
#include "ui_panel_presentation.hpp"
#include "simcoaster/application_game_ui.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/world.hpp"
#include <map>

namespace simcoaster {
struct ApplicationGameUiController::State : UiPanelPresentation {
    static constexpr UiPanelPresentationPolicy panel_presentation{ .missing_control =
                                                                       "Original application control absent: ",
                                                                   .text_error = "Application UI text: ",
                                                                   .text_error_row = false,
                                                                   .caption_font = -1,
                                                                   .caption_horizontal = 0 };
    enum class Page {
        none,
        menu,
        load,
        save,
        coaster_name
    };
    WorldRuntime& c;
    UiModalScope message_scope{ c };
    UiFocusBookmark modal_focus;
    UiMessageControls message_controls;
    UiRuntime& ui;
    mutable UiPanelBindings bindings{ ui };
    UiControl *primary{}, *wrapper{}, *root{}, *list{}, *message_wrapper{}, *message_root{}, *message_input_root{};
    std::uint64_t message_input_serial{}, next_request = 1;
    std::filesystem::path game_root;
    ApplicationGameUiHost host;
    std::unique_ptr<UiFontLibrary> fonts;
    Page page{};
    bool closing{}, owns_pause{}, commands_disabled{}, profile_load{}, coaster_mode{};
    ThingId coaster{};
    const ThingHeader* coaster_identity{};
    std::uint64_t coaster_generation{};
    std::vector<ApplicationFileEntry> entries;
    std::vector<ApplicationGameRequest> requests;
    std::optional<ApplicationGameRequest> pending;
    std::map<UiControl*, std::int32_t> hover;
    std::function<void()> cancel_message, accept_message;
    std::int32_t file_sort{};

    State(WorldRuntime&, UiRuntime&, UiControl*, const std::filesystem::path&, ApplicationGameUiHost);
    ~State();
    void begin(Page, bool dismiss = true, bool pause = true);
    void close();
    void close_message();
    void dialog(std::span<const unsigned char>, std::uint32_t);
    void message(std::u16string,
                 std::u16string,
                 std::function<void()> cancel = {},
                 std::function<void()> accepted = {},
                 UiMessageButtonKind first = UiMessageButtonKind::cancel);
    void message_resource(std::uint32_t, std::function<void()> accepted = {});
    std::intptr_t event(UiControl&, const UiEvent&);
    std::intptr_t menu_event(UiControl&, const UiEvent&);
    std::intptr_t file_event(UiControl&, const UiEvent&);
    std::intptr_t coaster_event(UiControl&, const UiEvent&);
    void open_menu(bool);
    void menu_action(std::uint32_t);
    void transition(ApplicationGamePayload);
    void enqueue(ApplicationGameRequest, bool retain_dialog = false);
    void open_load(bool);
    void open_save(bool);
    void configure_files(bool);
    void refresh_files();
    const ApplicationFileEntry* selection() const;
    bool valid_save_name() const;
    void save(bool overwrite = false);
    void delete_selection();
    void load();
    void open_coaster_save();
    ParkObjectRuntimeView* first_ride() const;
    bool coaster_alive() const;
    void save_coaster(bool overwrite = false);
    UiFontLibrary& font_library();
};
} //namespace simcoaster
