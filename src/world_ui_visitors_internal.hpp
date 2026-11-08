#pragma once
#include "ui_context_menu.hpp"
#include "ui_panel_presentation.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/ui_person_preview.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "simcoaster/world_ui_visitors.hpp"
#include "simcoaster/world_visitor_ui.hpp"
#include <map>

namespace simcoaster {
struct WorldVisitorsUiController::State : UiPanelPresentation {
    static constexpr UiPanelPresentationPolicy panel_presentation{ .missing_control =
                                                                       "Original Visitors template missing control ",
                                                                   .text_error = "Visitors UI text: ",
                                                                   .text_error_row = false,
                                                                   .caption_font = 6,
                                                                   .caption_horizontal = 0 };
    enum class Page {
        none,
        statistics,
        list,
        profile,
        locator
    };
    WorldRuntime& c;
    UiRuntime& ui;
    mutable UiPanelBindings bindings{ ui };
    UiControl *primary{}, *root{}, *context{};
    std::filesystem::path game_root;
    WorldUiPanelHost host;
    Page page{};
    ThingId target{}, retiring{}, context_person{};
    const ThingHeader *target_identity{}, *context_identity{};
    std::uint32_t target_arrival{}, context_arrival{};
    std::int32_t advisor = -1, list_sort = 1, list_selected = -1, locator_filter{};
    VisitorPopulation population = VisitorPopulation::all;
    std::map<ThingId, const ThingHeader*> list_identities;
    std::unique_ptr<UiPersonPreview> preview;
    std::unique_ptr<UiFontLibrary> local_fonts;
    bool closing{}, refreshing{}, animate_menus = true;
    UiControl* context_previous{};
    std::uint64_t context_previous_serial{};
    bool context_previous_enabled{};
    UiContextMenu context_menu;
    std::chrono::milliseconds next_refresh{};
    UiHelpQuery help_query;
    std::u16string help_buffer;
    std::uint32_t pixel_width = 1100, pixel_height = 760;
    State(WorldRuntime&, UiRuntime&, UiControl*, const std::filesystem::path&, WorldUiPanelHost);
    ~State();

    template <class Callback>
    UiControl& parse(std::span<const unsigned char> bytes, std::uint32_t id, Callback callback) {
        return parse_control(primary, bytes, id, std::move(callback), "Unable to construct original Visitors template");
    }
    std::u16string thought(std::uint32_t) const;
    void label(std::uint32_t, std::uint32_t, std::uint32_t font = 6, std::int32_t horizontal = 0);
    void scalar(std::uint32_t, std::int32_t);
    void meter(std::uint32_t, std::string_view, std::int32_t inset = 10);
    void begin(Page);
    void activate();
    void close();
    void close_context();
    void stage(std::int32_t);
    void help();
    void refresh();
    void retire(ThingId);
    PersonRuntimeView* person() const;
    void select(ThingId);
    void open_profile(ThingId);
    void refresh_profile();
    void navigate(bool);
    void open_list();
    void refresh_list();
    void refresh_list_details();
    void open_statistics();
    void refresh_statistics();
    void open_locator(ThingId);
    void refresh_locator();
    void locator_select(std::int32_t, bool previous = false);
    void open_context(ThingId, UiPoint);
    void refresh_context();
    std::intptr_t context_event(UiControl&, const UiEvent&);
    std::intptr_t event(UiControl&, const UiEvent&);
    void append(UiDrawList&, UiControl&, UiRect, std::uint32_t, std::uint32_t);
};
} //namespace simcoaster
