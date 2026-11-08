#pragma once
#include "simcoaster/ui_control.hpp"
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace simcoaster {
struct UiRuntime::State {
    struct Queued {
        UiControl* target{};
        std::uint64_t serial{};
        UiEvent event;
        bool routed{};
    };

    struct Tracked {
        UiControl* target{};
        UiPoint point{};
        std::chrono::milliseconds tick{};
        std::uint8_t state{};
    };

    std::vector<std::unique_ptr<UiControl>> controls;
    std::unordered_map<const UiControl*, std::uint64_t> serials;
    std::unordered_set<UiControl*> destruction_notifications;
    std::vector<UiControl*> roots;
    std::deque<Queued> primary, secondary;
    std::array<Tracked, 3> tracked;
    std::array<UiControl*, 3> down{};
    std::vector<UiEventRouteFilter> filters;
    UiControl *focused{}, *captured{}, *hovered{}, *interaction{}, *input_root{}, *route{};
    UiPoint pointer{};
    UiHelpQuery help;
    std::chrono::milliseconds now{};
    std::size_t capacity{}, depth{};
    std::uint64_t next_serial{}, dropped{}, pointer_cancel_epoch{}, tree_revision{};
    bool active = true, draining{}, locale_is_1037{};
    bool presentation_visible = true;
    std::function<void(UiControl*, const UiEvent&)> observer;

    bool alive(const UiControl* p) const {
        return p && serials.contains(p) && !p->destroying;
    }
};
} //namespace simcoaster
