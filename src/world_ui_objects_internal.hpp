#pragma once
#include "ui_context_menu.hpp"
#include "simcoaster/ui_modal_scope.hpp"
#include "ui_panel_presentation.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/ui_graph.hpp"
#include "simcoaster/ui_object_preview.hpp"
#include "simcoaster/world_object_ui_runtime.hpp"
#include "simcoaster/world_ui_objects.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include <map>

namespace simcoaster {
struct ObjectPanelSpec {
    ParkObjectUiPanel panel;
    std::span<const unsigned char> words;
    std::uint32_t root, preview, statistics, status, name, previous, next, apply, toggle_open, context, help, bulk;
    std::int32_t advisor;
};

const ObjectPanelSpec& object_panel_spec(ParkObjectUiPanel);

struct WorldParkObjectUiController::State : UiPanelPresentation {
    static constexpr UiPanelPresentationPolicy panel_presentation { .missing_control = "Original park-object template missing control ", .text_error = "Park-object UI text row ", .caption_font = 6, .caption_horizontal = 0, .list_error = "Park-object list disagrees with original template columns" };
    enum class Page {
        none,
        settings,
        list,
        finance,
        statistics,
        opinions,
        promotion,
        upgrades,
        special,
        research,
        research_picker,
        research_edit
    };
    WorldRuntime& c;
    UiModalScope message_scope { c };
    UiMessageControls message_controls;
    std::filesystem::path game_root;
    UiRuntime& ui;
    mutable UiPanelBindings bindings { ui };
    UiControl* primary {};
    WorldUiPanelHost host;
    WorldParkObjectUiRuntime model;
    Page page {};
    UiControl *root {}, *sublist {}, *context {}, *name_editor {}, *message_wrapper {}, *message_root {}, *message_previous_focus {};
    UiControl* context_previous_root {};
    std::uint64_t context_previous_serial {};
    bool context_previous_enabled {};
    ThingId page_target {}, auxiliary_target {};
    ThingId confirmation_target {};
    const ThingHeader* auxiliary_identity {};
    std::uint64_t auxiliary_generation {};
    CellId context_cell {};
    std::int32_t context_family {};
    std::uint32_t context_mesh {};
    std::uint64_t context_mesh_generation {};
    std::vector<UiControl*> roots;
    std::map<UiControl*, std::unique_ptr<UiObjectPreview>> previews;
    std::unique_ptr<UiFontLibrary> local_fonts;
    std::unique_ptr<UiGraphPresenter> graph_presenter;
    UiControl* graph_control {};
    std::uint32_t pixel_width = 1100, pixel_height = 760;
    std::chrono::milliseconds next_refresh {};
    bool closing {}, refreshing {}, confirm_deletions = true, animate_menus = true, context_filtered {};
    std::int32_t advisor_action = -1;
    std::function<void()> message_accepted;
    UiContextMenu context_menu;
    std::array<std::int32_t, 6> list_saved_selection {};
    std::optional<ObjectUiCategory> presented_category;
    std::optional<std::uint64_t> settings_input_revision;
    std::optional<ParkObjectFinanceMode> presented_finance;
    bool research_return_to_picker {}, research_picker_initialized {};
    std::int32_t research_sort_key = 2;
    std::array<std::int32_t, 2> research_cached_projects { -1, -1 };
    std::array<ThingId, 2> research_cached_consultants {};
    std::vector<ThingId> research_available;
    std::map<ThingId, const ThingHeader*> research_identities;
    ThingId research_retiring {};
    std::int32_t research_slot {};
    std::int32_t research_project = -1;
    std::vector<ThingId> research_workers;
    ThingId research_consultant {};

    State(WorldRuntime&, UiRuntime&, UiControl*, const std::filesystem::path&, WorldUiPanelHost);
    ~State();

    template <class Callback>
    UiControl& parse(std::span<const unsigned char> bytes, std::uint32_t id, Callback callback, bool page_root = true) {
        return parse_ui_panel(ui, primary, bytes, id, std::move(callback), page_root ? &roots : nullptr, { "Original park-object template: ", false, "Original park-object template has no root ", true });
    }

    std::u16string money(std::int32_t) const;
    UiFontLibrary& fonts();
    void label(std::uint32_t, std::uint32_t, std::uint32_t font = 6, std::int32_t horizontal = 0);
    void scalar(std::uint32_t, std::int32_t, UiScalarTextKind = UiScalarTextKind::signed_integer);
    void meter(std::uint32_t, std::int32_t, std::int32_t inset = 10);
    void affixed_edit(UiControl&, std::u16string_view, std::size_t = 6, std::uint32_t = 6);
    void preview(UiControl&, ThingId, std::uint32_t animation = 5);
    void begin(Page);
    void activate();
    void close();
    void close_auxiliary();
    void retire(ThingId);
    void stage_advisor(std::int32_t);
    void help_advisor(std::int32_t);
    void refresh();
    void refresh_page();
    void open_object(ThingId);
    void open_settings(ThingId);
    void configure_settings();
    void refresh_settings();
    std::intptr_t settings_event(UiControl&, const UiEvent&);
    void open_list();
    void rebuild_list();
    void refresh_list();
    std::intptr_t list_event(UiControl&, const UiEvent&);
    void open_finance();
    void refresh_finance();
    std::intptr_t finance_event(UiControl&, const UiEvent&);
    void open_statistics(ThingId);
    void refresh_statistics();
    std::intptr_t statistics_event(UiControl&, const UiEvent&);
    void open_opinions(ThingId);
    void refresh_opinions();
    std::intptr_t opinions_event(UiControl&, const UiEvent&);
    void open_promotion(ThingId, bool);
    void refresh_promotion();
    std::intptr_t promotion_event(UiControl&, const UiEvent&);
    void open_upgrade(ThingId);
    void refresh_upgrade();
    std::intptr_t upgrade_event(UiControl&, const UiEvent&);
    void open_special(ThingId);
    void refresh_special();
    std::intptr_t special_event(UiControl&, const UiEvent&);
    void open_context(ThingId, UiPoint, bool, std::int32_t family = 0, std::uint32_t mesh_slot = 0, CellId cell = 0);
    void refresh_context();
    void execute_context(std::uint32_t);
    std::intptr_t context_event(UiControl&, const UiEvent&);
    void open_name(ThingId, bool);
    std::intptr_t name_event(UiControl&, const UiEvent&);
    ParkObjectRuntimeView* auxiliary_object() const;
    void restore_input();
    void message(std::uint32_t, std::function<void()> = {});
    void message_text(std::u16string, std::function<void()> = {}, UiMessageButtonKind = UiMessageButtonKind::cancel);
    void open_research();
    void refresh_research();
    void open_research_picker();
    void refresh_research_picker();
    void open_research_edit(std::int32_t, std::int32_t project = -1);
    void refresh_research_edit();
    std::intptr_t research_event(UiControl&, const UiEvent&);
    std::intptr_t research_picker_event(UiControl&, const UiEvent&);
    std::intptr_t research_edit_event(UiControl&, const UiEvent&);
    std::intptr_t common_event(UiControl&, const UiEvent&);
    void append(UiDrawList&, UiControl&, UiRect, std::uint32_t, std::uint32_t);
};
} //namespace simcoaster
