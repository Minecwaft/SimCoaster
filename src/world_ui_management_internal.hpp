#pragma once
#include "simcoaster/ui_modal_scope.hpp"
#include "ui_panel_presentation.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_ui_management.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include <map>

namespace simcoaster {
struct WorldManagementUiController::State : UiPanelPresentation {
    static constexpr UiPanelPresentationPolicy panel_presentation{
        .missing_control = "Original management template has no control ",
        .text_error = "Management localized row ",
        .list_error = "Management original list column mismatch",
        .list_style = UiPanelListStyle::required_aligned_columns
    };
    enum class Page {
        none,
        finance,
        loans,
        shares,
        gates,
        awards,
        statistics,
        challenges,
        options
    };
    WorldRuntime& c;
    UiModalScope message_scope{ c };
    UiFocusBookmark modal_focus;
    UiMessageControls message_controls;
    UiRuntime& ui;
    mutable UiPanelBindings bindings{ ui };
    UiControl *primary{}, *root{}, *list_control{}, *graph_control{}, *name_editor{}, *message_wrapper{},
        *message_root{};
    std::filesystem::path game_root;
    WorldManagementUiHost host;
    Page page{};
    std::vector<UiControl*> roots;
    std::unique_ptr<UiFontLibrary> local_fonts;
    std::unique_ptr<UiGraphPresenter> graph_presenter;
    std::vector<UiGraphSeriesStyle> graph_styles;
    StatisticsGraphUiState shares_graph;
    std::uint64_t graph_revision{}, loan_revision{}, award_revision{};
    std::int32_t advisor_action = -1, staged_fee{}, shares_sort{}, loan_sort{}, challenge_sort = 1, challenge_mode{},
                 options_page = 95020;
    std::optional<std::size_t> selected_challenge;
    ThingId gate_target{};
    bool challenge_advisor_latched{};
    std::optional<std::int32_t> highlighted_award;
    std::uint32_t award_step{}, award_phase{}, award_next_ms{}, award_started_ms{};
    UiPreferences options_snapshot, options_current;
    ManagementGraphicsPreferences graphics_snapshot, graphics_current;
    ManagementGraphicsCapabilities graphics_caps;
    bool closing{}, refreshing{}, options_pause_owned{}, options_committed{};
    std::function<void()> message_accepted;
    std::vector<std::byte> challenge_generic, challenge_specific;
    std::uint32_t pixel_width = 1100, pixel_height = 760;

    struct ScrollText {
        UiControl* scroll{};
        UiRect bounds{};
    };

    std::map<UiControl*, ScrollText> scrolling;

    State(WorldRuntime&, UiRuntime&, UiControl*, const std::filesystem::path&, WorldManagementUiHost);
    ~State();
    UiControl& parse(std::span<const unsigned char>, std::uint32_t);
    void begin(Page);
    void activate();
    void close();
    void close_message();
    void close_name();
    void title(std::uint32_t, std::u16string_view);
    void scalar(std::uint32_t, std::int32_t, bool currency = false, std::uint32_t font = 6);
    void edit(UiControl&, std::int32_t, std::int32_t, std::int32_t, std::u16string_view affix = {});
    void message(std::u16string, std::u16string, std::function<void()> accepted = {});
    UiFontLibrary& fonts();
    void stage_advisor(std::int32_t);
    void help_advisor(std::int32_t);
    void cancel_advisor(std::int32_t);
    World& world() const;
    BankRecord& bank() const;
    ParkAnalyserState& analyser() const;
    ParkObjectRuntimeView& gates() const;
    std::intptr_t event(UiControl&, const UiEvent&);
    void refresh();
    void scroll_text(UiControl&, std::uint32_t, std::u16string);
    void open_finance();
    void refresh_finance();
    void open_loans();
    void refresh_loans();
    void loan_action(std::int32_t);
    void open_shares();
    void refresh_shares_graph();
    void refresh_shares();
    void open_gates();
    void open_gate_name();
    void open_awards(std::optional<std::int32_t>);
    void refresh_awards();
    void open_statistics();
    void refresh_statistics();
    void open_challenges(std::optional<ChallengeScriptIndex>);
    ChallengeManagerRecord& challenges() const;
    void cancel_challenge_advisors(std::optional<std::int32_t> except = {});
    void queue_challenge_advisor();
    std::u16string challenge_briefing(const ChallengeRecord&) const;
    std::u16string challenge_details(const ChallengeRecord&) const;
    std::u16string challenge_status(const ChallengeRecord&, bool preparing, std::u16string unchanged) const;
    void refresh_challenges(bool rebuild = false);
    void refresh_challenge_details();
    void open_options();
    void configure_options();
    void refresh_options();
    void commit_options(bool);
    std::intptr_t finance_event(UiControl&, const UiEvent&);
    std::intptr_t park_event(UiControl&, const UiEvent&);
    std::intptr_t challenge_event(UiControl&, const UiEvent&);
    std::intptr_t options_event(UiControl&, const UiEvent&);
};
} //namespace simcoaster
