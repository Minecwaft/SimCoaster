#pragma once
#include "ui_context_menu.hpp"
#include "simcoaster/ui_modal_scope.hpp"
#include "ui_panel_presentation.hpp"
#include "simcoaster/bfst_strings.hpp"
#include "simcoaster/ui_font.hpp"
#include "simcoaster/ui_object_preview.hpp"
#include "simcoaster/ui_staff_preview.hpp"
#include "simcoaster/world_staff_ui.hpp"
#include "simcoaster/world_ui_runtime.hpp"
#include "simcoaster/world_ui_staff.hpp"
#include <map>

namespace simcoaster {
struct WorldStaffUiController::State : UiPanelPresentation {
    static constexpr UiPanelPresentationPolicy panel_presentation { .missing_control = "Original Staff template missing control ", .text_error = "Staff UI text row ", .list_error = "Staff list columns disagree with original template" };
    enum class Page {
        none,
        hire,
        list,
        details,
        locator,
        finance,
        activity,
        staff_room,
        training,
        patrol,
        training_budget,
        staff_ability
    };
    WorldRuntime& c;
    UiModalScope message_scope { c };
    UiMessageControls message_controls;
    std::filesystem::path game_root;
    UiRuntime& ui;
    mutable UiPanelBindings bindings { ui };
    UiControl* primary {};
    WorldStaffUiRuntime model;
    StaffUiPanelHost host;
    Page page {};
    ThingId page_target {};
    UiControl *root {}, *selector {}, *ability {}, *context {}, *name_editor {}, *wage_dialog {}, *confirmation {};
    std::vector<UiControl*> page_roots;
    std::map<UiControl*, std::unique_ptr<UiStaffPreview>> previews;
    std::map<UiControl*, std::unique_ptr<UiObjectPreview>> object_previews;
    std::map<std::size_t, StaffCandidate> hire_candidate_identities;
    ThingId auxiliary_staff {};
    const ThingHeader* auxiliary_identity {};
    std::uint64_t auxiliary_hire_time {};
    UiContextMenu context_menu;
    bool context_filtered {};
    std::int32_t budget_history6 {}, budget_history3_minus4 {};
    bool wage_transition_pending {};
    std::optional<std::size_t> ability_slot;
    std::optional<StaffFinanceAdjustment> finance_presentation_mode;
    std::int32_t staff_ability_sort_key = 1;
    std::optional<std::int32_t> current_context_family;
    bool closing {}, refreshing {}, selector_shifted {};
    //Native459520 initializes both policies true. The owning settings import
    //supplies any saved overrides; menu code consumes these semantic fields.
    bool confirm_deletions = true, animate_menus = true;
    std::uint32_t pixel_width = 1100, pixel_height = 760;
    std::unique_ptr<UiFontLibrary> local_fonts;
    std::function<void()> message_accepted;
    UiControl* message_previous_focus {};
    UiControl* message_wrapper {};
    std::chrono::milliseconds next_refresh {};

    State(WorldRuntime&, UiRuntime&, UiControl*, const std::filesystem::path&, StaffUiPanelHost);
    ~State();
    UiControl* find(std::uint32_t) const;
    UiFontLibrary& fonts();
    void affixed_edit(UiControl& stepper, std::u16string_view affix, std::uint32_t font = 6, std::size_t capacity = 6, std::array<std::uint8_t, 4> color = { 255, 255, 255, 255 }, bool append = false);
    void button(UiControl&, std::uint32_t row, std::uint32_t help = 4294967295u);
    void meter(UiControl&, std::string_view texture = "ridestatbar.tga", std::int32_t vertical_inset = 10);
    void preview(UiControl&, ThingId);
    void candidate_preview(UiControl&, std::size_t);
    void object_preview(UiControl&, ThingId);

    template <class Callback>
    UiControl& parse(std::span<const unsigned char> bytes, std::uint32_t id, Callback callback, std::vector<UiControl*>* owners = nullptr) {
        return parse_ui_panel(ui, primary, bytes, id, std::move(callback), owners, { "Staff template word ", true, "Original Staff template has no requested root ", true, UiPanelRootLookup::direct });
    }

    void begin(Page);
    void stage_advisor(std::int32_t);
    void close();
    void retire(ThingId);
    void refresh();
    void refresh_page();
    void close_auxiliary();
    void close_selector();
    void close_ability(bool restore_marker = true);
    void message(std::uint32_t row, std::function<void()> accepted = {});
    void message_text(std::u16string, std::function<void()> accepted = {});
    void open_staff_ability(ThingId);
    void refresh_staff_ability();
    std::intptr_t staff_ability_event(UiControl&, const UiEvent&);

    void open_hire(StaffType = StaffType::unsupported_index);
    void refresh_hire();
    void open_list(StaffType = StaffType::unsupported_index, ThingId = 0);
    void refresh_list();
    void open_details(ThingId);
    void refresh_details();
    std::intptr_t details_event(UiControl&, const UiEvent&);
    void open_locator(ThingId = 0);
    void refresh_locator();
    std::intptr_t roster_event(UiControl&, const UiEvent&);
    void open_finance();
    void refresh_finance();
    std::intptr_t finance_event(UiControl&, const UiEvent&);
    void open_activity(ThingId);
    void refresh_activity();
    std::intptr_t activity_event(UiControl&, const UiEvent&);
    void open_room(ThingId);
    void refresh_room();
    void open_training(ThingId);
    void refresh_training();
    void open_selector();
    void refresh_selector();
    void open_ability(std::size_t);
    void refresh_ability();
    void open_patrol();
    void refresh_patrol();
    std::intptr_t event(UiControl&, const UiEvent&);
    std::intptr_t selector_event(UiControl&, const UiEvent&);
    std::intptr_t ability_event(UiControl&, const UiEvent&);
    std::intptr_t room_event(UiControl&, const UiEvent&);
    std::intptr_t training_event(UiControl&, const UiEvent&);
    std::intptr_t patrol_event(UiControl&, const UiEvent&);

    //Auxiliary Staff dialogs and original role context menus have their own
    //roots and callbacks, while sharing model identities and closure ordering.
    void open_context(ThingId, UiPoint, bool filtered = false);
    StaffRuntimeRecord* aux_live_staff() const;
    ParkObjectRuntimeView* aux_live_room() const;
    void refresh_context();
    void open_name(ThingId, bool attached_to_panel = false);
    void open_wage(ThingId, bool result = false);
    void refresh_wage();
    void open_training_budget();
    std::intptr_t context_event(UiControl&, const UiEvent&);
    std::intptr_t name_event(UiControl&, const UiEvent&);
    std::intptr_t wage_event(UiControl&, const UiEvent&);
    std::intptr_t budget_event(UiControl&, const UiEvent&);
};
} //namespace simcoaster
