#pragma once
#include "world_types.hpp"
#include <compare>
#include <optional>
#include <string>
#include <vector>

namespace simcoaster {
struct WorldRuntime;
struct MailMessage;

struct WorldMailIdentity {
    std::int32_t tag{};
    std::uint32_t sequence{};
    auto operator<=>(const WorldMailIdentity&) const = default;
};
enum class WorldMailUiActionKind {
    focus_thing,
    focus_cell,
    visitor_locator,
    staff_locator,
    build,
    challenges,
    objectives,
    research,
    wage_demand,
    loans,
    hire,
    overview,
    park_gates,
    maintenance,
    park_statistics
};

struct WorldMailUiAction {
    WorldMailUiActionKind kind{};
    ThingId target{};
    std::int32_t argument{};
    ObjectUiCategory category{};
    StaffType staff_type{};
    std::int32_t x_offset{}, y_offset{};
    //Snapshot for queued host navigation. Retirement invalidates the action
    //before an allocator may recycle either the pointer or numeric ThingId.
    const ThingHeader* identity{};
};

struct WorldMailUiRow {
    WorldMailIdentity key;
    std::u16string subject, date;
    std::uint32_t packed_date{};
    bool consumed{};
    std::uint16_t counter_slot{};
};

MailMessage* find_ui_mail(WorldRuntime&, WorldMailIdentity);
std::vector<WorldMailUiRow> world_mail_ui_rows(WorldRuntime&);
bool consume_ui_mail(WorldRuntime&, WorldMailIdentity);
bool delete_ui_mail(WorldRuntime&, WorldMailIdentity);
std::optional<WorldMailUiAction> resolve_world_mail_ui_action(WorldRuntime&, const MailMessage&);
} //namespace simcoaster
