#pragma once
#include "entity_identity.hpp"
#include <concepts>
#include <variant>

namespace simcoaster {
struct ThingHeader;
enum class WorldUiCommand : std::int32_t;

struct WorldCommandAction {
    WorldUiCommand command {};
    std::int32_t parameter {};
};

struct WorldToolbarAction {
    std::int32_t control {};
};

struct WorldCameraAction {
    std::int32_t control {}, event {};
};

struct WorldSourceChangeAction {
    std::int32_t source {}, force {};
};

struct WorldFinishIntroAction {
    std::int32_t force {};
};

struct WorldCenterPointerAction {};

struct WorldResetInteractionAction {};

using WorldEntityTarget = EntityIdentity;

struct WorldDeleteParkObjectAction : WorldEntityTarget {};

struct WorldBuyParkObjectAction : WorldEntityTarget {};

struct WorldStaffFocusAction : WorldEntityTarget {
    std::int32_t follow {};
};

struct WorldObjectFocusAction : WorldEntityTarget {};

struct WorldPersonFocusAction : WorldEntityTarget {
    std::int32_t follow {};
};

struct WorldStaffCameraAction {
    std::int32_t key {}, event {};
};

struct WorldPositionFocusAction {
    std::int32_t x {}, y {};
};

using WorldUiAction = std::variant<WorldCommandAction, WorldToolbarAction, WorldCameraAction, WorldSourceChangeAction, WorldFinishIntroAction, WorldCenterPointerAction, WorldResetInteractionAction, WorldDeleteParkObjectAction, WorldBuyParkObjectAction, WorldStaffFocusAction, WorldStaffCameraAction, WorldObjectFocusAction, WorldPersonFocusAction, WorldPositionFocusAction>;

inline bool world_ui_action_targets(const WorldUiAction& action, ThingId id) {
    match (action) {
        case { const auto& payload } => {
            if constexpr (std::derived_from<std::remove_cvref_t<decltype(payload)>, WorldEntityTarget>) {
                return payload.identity && payload.id == id;
            } else {
                return false;
            }
        }
    } return false;
}

inline WorldUiAction world_object_trade_action(bool remove, WorldEntityTarget target) {
    if (remove) {
        return WorldDeleteParkObjectAction { target };
    }
    return WorldBuyParkObjectAction { target };
}
} //namespace simcoaster
