#pragma once
#include "ui_control.hpp"
#include <map>

namespace simcoaster {
struct WorldRuntime;

//A controller owns this cache; any tree mutation invalidates its projections.
//Keys include the lookup root because original templates reuse child IDs.
class UiPanelBindings {
    UiRuntime& ui_;
    mutable std::uint64_t revision_ {};
    mutable std::map<std::pair<const UiControl*, std::uint32_t>, UiControl*> controls_;

public:
    explicit UiPanelBindings(UiRuntime& ui) : ui_(ui) {}

    UiControl* find(UiControl*, std::uint32_t) const;
    UiControl& required(UiControl*, std::uint32_t, std::string_view missing) const;
};

struct UiRequiredControl {
    std::uint32_t id;
};

struct UiMessageControls {
    [[=UiRequiredControl { 60004 }]] UiControl* title {};
    [[=UiRequiredControl { 60001 }]] UiControl* cancel {};
    [[=UiRequiredControl { 60002 }]] UiControl* okay {};
    [[=UiRequiredControl { 60003 }]] UiControl* close {};
};

UiMessageControls bind_ui_message_controls(UiPanelBindings&, UiControl*, std::string_view missing);
std::span<const unsigned char> ui_message_template();
UiControl& create_ui_message_wrapper(UiRuntime&);
void split_ui_message_title(std::u16string& heading, std::u16string& body);
void cancel_ui_advisor_action(WorldRuntime&, std::int32_t action, bool clear_panel_flags = true);

enum class UiPanelRootLookup {
    direct,
    recursive
};

struct UiPanelTemplatePolicy {
    std::string_view parse_error;
    bool include_word {};
    std::string_view missing_root;
    bool include_root_id {};
    UiPanelRootLookup lookup = UiPanelRootLookup::recursive;
};

namespace ui_panel_detail {
template <class Callback>
struct CommandRoute {
    UiRuntime& ui;
    Callback owner;
    UiCallback widget;

    std::intptr_t operator()(UiControl& sender, const UiEvent& event) {
        if (event.type >= 256 && event.type <= 2055) {
            return owner(sender, event);
        }
        return widget ? widget(sender, event) : ui.secondary(sender, event);
    }
};

//This borrowed binder exists only during synchronous template construction.
//Controls store copies of the concrete callback, never this stack reference.
struct Binder {
    void* callback;
    void (*install)(void*, UiRuntime&, UiControl&, bool root);

    template <class Callback>
    explicit Binder(Callback& value)
        : callback(&value),
          install([](void* pointer, UiRuntime& ui, UiControl& control, bool root) {
              auto& owner = *static_cast<Callback*>(pointer);
              if (root) {
                  ui.set_primary_callback(control, owner);
              } else {
                  const auto widget = control.primary_callback;
                  ui.set_primary_callback(control, CommandRoute<Callback> { ui, owner, widget });
              }
          }) {}
};

void bind(UiRuntime&, UiControl&, Binder, bool route_root);
UiControl& parse(UiRuntime&, UiControl* parent, std::span<const unsigned char>, std::uint32_t root_id, Binder, std::vector<UiControl*>* owners, UiPanelTemplatePolicy);
} //namespace ui_panel_detail

template <class Callback>
void bind_ui_panel(UiRuntime& ui, UiControl& top, Callback callback, bool route_root = true) {
    ui_panel_detail::bind(ui, top, ui_panel_detail::Binder(callback), route_root);
}

template <class Callback>
UiControl& parse_ui_panel(UiRuntime& ui, UiControl* parent, std::span<const unsigned char> bytes, std::uint32_t root_id, Callback callback, std::vector<UiControl*>* owners, UiPanelTemplatePolicy policy) {
    return ui_panel_detail::parse(ui, parent, bytes, root_id, ui_panel_detail::Binder(callback), owners, policy);
}
} //namespace simcoaster
