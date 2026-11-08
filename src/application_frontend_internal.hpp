#pragma once
#include "simcoaster/ui_panel_runtime.hpp"
#include "simcoaster/application_frontend.hpp"
#include "simcoaster/world.hpp"
#include <map>

namespace simcoaster {
struct ApplicationFrontendResumeState::Detail {
    const WorldRuntime* context{};
    bool skip_requested{}, final_speech{}, pulse_enabled{}, pulse_phase{}, map_view_active{}, suppress_template{},
        lobby_bound{}, lobby_intro{}, metric_complete{}, fade_ready{};
    std::array<bool, 3> intro_visible{};
    std::int32_t speech_index = -1, speech_status{}, pulse_deadline{}, selected_area = -1, previous_area = -1,
                 directional{}, ambient{};
    std::uint32_t selected_profile{}, intro_script_id{}, angle_tick{}, fade_tick{}, intro_pulse_button{};
    std::uint16_t initial_mode{};
    float angle{}, spin_target{}, spin_origin{}, dolly{}, height_offset{};
    std::u16string profile_edit;
};

struct ApplicationFrontendController::State {
    WorldRuntime& c;
    UiRuntime& ui;
    mutable UiPanelBindings bindings{ ui };
    UiControl *parent{}, *intro{}, *profile{}, *overview{};
    std::filesystem::path game_root;
    ApplicationFrontendHost host;
    std::string language;
    ApplicationFrontendPhase phase{};
    std::shared_ptr<int> lifetime = std::make_shared<int>();
    std::vector<UiControl*> owners;
    std::vector<std::string> diagnostics;
    std::map<std::string, std::vector<std::byte>, std::less<>> tables;
    std::array<ApplicationProfileView, 5> profiles{};
    std::uint32_t selected_profile{}, pixel_width = 640, pixel_height = 480;
    bool closing{}, restoring{}, commands_disabled{}, skip_requested{}, suppress_buttons{}, final_speech{},
        pulse_enabled{}, pulse_phase{}, map_view_active{}, suppress_template{}, selection_locked{}, area_click{},
        left_down{}, right_down{}, metric_complete{}, fade_ready{}, transition_queued{}, hide_chooser_close{};
    std::int32_t speech_index = -1, speech_status = 0, pulse_deadline{}, selected_area = -1, previous_area = -1,
                 profile_source = 1;
    std::uint32_t intro_script_id{}, lobby_script_id{}, lobby_mesh_slot{}, angle_tick{}, fade_tick{}, left_repeat{},
        right_repeat{}, area_sound_deadline{}, area_sound{}, map_sound{}, intro_pulse_button{};
    std::uint64_t lobby_mesh_generation{};
    std::uint32_t unavailable_color{};
    std::uint16_t mode{}, initial_mode{};
    float angle{}, spin_target{}, spin_origin{}, dolly{}, height_offset = -7;
    std::int32_t directional = 255, ambient = 85;
    std::int16_t saved_speed{};
    double saved_scale{};
    bool lobby_bound{}, lobby_intro{}, intro_sound_active{};
    std::array<std::uint16_t, 320 * 240> mask{};

    struct AreaCamera {
        std::int32_t x{}, y{}, z{}, zoom{}, rotation{};
    };

    std::array<AreaCamera, 15> area_cameras{};
    std::optional<AreaCamera> destination;

    struct NodeBackup {
        std::size_t index{};
        std::uint32_t flags{};
        std::vector<std::uint32_t> colors;
        bool modified{};
    };

    std::vector<NodeBackup> node_backups;

    State(WorldRuntime&, UiRuntime&, UiControl*, const std::filesystem::path&, ApplicationFrontendHost, std::string);
    ~State();
    UiControl& required(UiControl*, std::uint32_t);
    UiControl* parse(std::span<const unsigned char>, std::uint32_t);
    void bind(UiControl&, bool = false);
    void remove(UiControl*&);
    void close();
    std::intptr_t event(UiControl&, const UiEvent&);
    std::u16string format(std::span<const std::byte>,
                          std::uint32_t,
                          std::span<const BfstWideStringArgument> = {},
                          std::span<const BfstSignedIntegerArgument> = {});
    std::u16string
    text(std::uint32_t, std::span<const BfstWideStringArgument> = {}, std::span<const BfstSignedIntegerArgument> = {});
    const std::vector<std::byte>& table(std::string_view);
    std::u16string theme_name(int);
    void caption(UiControl&, std::u16string_view, std::uint32_t = 6, std::int32_t = 0, std::int32_t = 0);
    void numeric(UiControl&, std::int32_t, UiScalarTextKind);
    AdvisorRecord* advisor();
    void queue_advisor(std::int32_t);
    void cancel_advisor(std::int32_t);
    void commands(bool);
    void restore_park_ui();
    void open_intro(bool);
    void finish_intro(bool);
    void play_intro();
    void skip_intro();
    void update_intro();
    void open_profiles();
    void refresh_profiles(bool = true);
    void select_profile(std::uint32_t, bool = true);
    void refresh_profile_summary();
    void create_profile();
    void profile_action(std::uint32_t);
    void enter_profile();
    void open_chooser(int, bool);
    void refresh_overview();
    void navigate(int);
    void bind_lobby(bool);
    void leave_lobby();
    void restore_nodes();
    void shade_nodes();
    void select_mode(std::uint16_t, bool = true);
    void update_lobby();
    void update_area();
    void publish_lobby_view();
    void begin_area();
    std::uint32_t clock();
    bool key(std::uint32_t, bool, std::uint32_t);
};
} //namespace simcoaster
