#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include "world_types.hpp"

namespace simcoaster {
struct RetirementEvent {
    std::uint16_t retired_id;
};

struct PersonCreatedEvent {
    ThingId person_id;
};

struct ParkObjectCreatedEvent {
    ThingId park_object_id;
};

struct ChallengeProgressEvent {
    ChallengeType type;
    std::int32_t delta;
    ThingId target, auxiliary;
};

struct ParkObjectRemovedEvent {
    ThingId park_object_id;
};

struct ParkObjectClosingEvent {
    ThingId park_object_id;
};

template <ParkObjectState State>
struct ParkObjectStateChangedEvent {
    ThingId park_object_id;
};

using ParkObjectBrokenDownEvent = ParkObjectStateChangedEvent<ParkObjectState::broken_down>;
using ParkObjectCondemnedEvent = ParkObjectStateChangedEvent<ParkObjectState::condemned>;
using ParkObjectUpgradeRequestedEvent = ParkObjectStateChangedEvent<ParkObjectState::upgrade_requested>;
enum class CalendarEventKind : std::int32_t {
    day = 11,
    month = 12,
    year = 13
};

template <CalendarEventKind Kind>
struct CalendarChangedEvent {};

using DayChangedEvent = CalendarChangedEvent<CalendarEventKind::day>;
using MonthChangedEvent = CalendarChangedEvent<CalendarEventKind::month>;
using YearChangedEvent = CalendarChangedEvent<CalendarEventKind::year>;

struct Tag19Event {
    std::uint32_t tag;
};

struct TagMailEvent {
    std::int32_t tag_id;
    std::uint16_t thing_id;
    std::uint32_t secondary;
    std::int32_t parameter = -1;
};

struct HeapPayloadEvent {
    GameOverCompanyEmail* payload;
};

struct AnimationCompletedEvent {
    std::uint16_t target;
    std::int32_t completed_slot0;
};

struct WeatherChangedEvent {
    std::int32_t quality, length;
    WeatherCategory category;
};

struct AdvisorActionEvent {
    AdvisorQueueEntry candidate;
};

struct StaffRestAreaEvent {
    ThingId staff;
    std::int32_t entered;
};

struct StaffTrainingStartedEvent {
    ThingId staff;
};

struct StaffTrainingCompletedEvent {
    ThingId staff;
};

struct StaffStrikeEvent {
    StaffType category;
    std::int32_t code, parameter;
};
enum class PersonActionCode : std::int32_t {
    stink_bomb = 1,
    toilet_booby_trap = 2,
    litter_bin_prank = 3,
    vegetation_fire_prank = 4,
    scatter_litter = 5,
    balloon_prank = 6,
    spread_vicious_rumours = 7,
    whinge_at_staff = 8
};

struct PersonActionEvent {
    ThingId person_id;
    PersonActionCode action;
};

struct ResearchAvailabilityEvent {
    std::uint16_t definition_id;
    ResearchCategory category;
};

struct CurrentObstacleEvent {
    std::uint16_t placed_thing;
};

struct MissionObjectiveProgressEvent {};

struct ObstacleRemovedEvent {
    std::uint16_t placed_thing;
};

struct PromotionEvent {
    std::int32_t rank;
    std::int32_t share_prize{};
};
enum class ChallengeNotificationCode : std::int32_t {
    offered = 0,
    started = 1,
    succeeded = 2,
    failed = 3,
    time_warning = 4
};

struct ChallengeNotificationEvent {
    ChallengeRecord* record;
    ChallengeNotificationCode code;
};

struct ChallengeCompletionEvent {
    ChallengeScriptIndex script_index;
};

struct MissionAdvancedEvent {};

//Unported handlers can carry an explicitly sized external payload. Runtime
//event dispatch never guesses a payload by adding an x86 object-header size.
struct ExternalWorldEvent {
    std::int32_t code;
    std::span<const std::byte> payload;
};

struct WorldEvent {
    using Value = std::variant<RetirementEvent,
                               ChallengeProgressEvent,
                               DayChangedEvent,
                               MonthChangedEvent,
                               YearChangedEvent,
                               Tag19Event,
                               TagMailEvent,
                               HeapPayloadEvent,
                               AnimationCompletedEvent,
                               WeatherChangedEvent,
                               AdvisorActionEvent,
                               PersonActionEvent,
                               ResearchAvailabilityEvent,
                               CurrentObstacleEvent,
                               MissionObjectiveProgressEvent,
                               ObstacleRemovedEvent,
                               PromotionEvent,
                               MissionAdvancedEvent,
                               ParkObjectRemovedEvent,
                               ParkObjectClosingEvent,
                               ParkObjectBrokenDownEvent,
                               ParkObjectCondemnedEvent,
                               ParkObjectUpgradeRequestedEvent,
                               PersonCreatedEvent,
                               ParkObjectCreatedEvent,
                               ChallengeNotificationEvent,
                               ChallengeCompletionEvent,
                               StaffRestAreaEvent,
                               StaffTrainingStartedEvent,
                               StaffTrainingCompletedEvent,
                               StaffStrikeEvent,
                               ExternalWorldEvent>;
    Value value;

    template <class T>
        requires std::constructible_from<Value, T>
    explicit WorldEvent(T event) : value(std::move(event)) {}

    template <class T>
    T& get() {
        return std::get<T>(value);
    }

    template <class T>
    const T& get() const {
        return std::get<T>(value);
    }

    std::int32_t type() const {
        return match (value) -> std::int32_t {
            case { RetirementEvent }                 => 33;
            case { PersonCreatedEvent }              => 34;
            case { ParkObjectCreatedEvent }          => 6;
            case { ChallengeProgressEvent }          => 32;
            case { ParkObjectRemovedEvent }          => 10;
            case { ParkObjectClosingEvent }          => 20;
            case { ParkObjectBrokenDownEvent }       => 7;
            case { ParkObjectCondemnedEvent }        => 8;
            case { ParkObjectUpgradeRequestedEvent } => 9;
            case { DayChangedEvent }                 => 11;
            case { MonthChangedEvent }               => 12;
            case { YearChangedEvent }                => 13;
            case { Tag19Event }                      => 19;
            case { TagMailEvent }                    => 24;
            case { HeapPayloadEvent }                => 25;
            case { AnimationCompletedEvent }         => 41;
            case { WeatherChangedEvent }             => 1;
            case { AdvisorActionEvent }              => 5;
            case { StaffRestAreaEvent }              => 15;
            case { StaffTrainingStartedEvent }       => 42;
            case { StaffTrainingCompletedEvent }     => 43;
            case { StaffStrikeEvent }                => 23;
            case { PersonActionEvent }               => 14;
            case { ResearchAvailabilityEvent }       => 17;
            case { CurrentObstacleEvent }            => 35;
            case { MissionObjectiveProgressEvent }   => 0x27;
            case { ObstacleRemovedEvent }            => 36;
            case { PromotionEvent }                  => 37;
            case { MissionAdvancedEvent }            => 44;
            case { ChallengeNotificationEvent }      => 27;
            case { ChallengeCompletionEvent }        => 38;
            case { const ExternalWorldEvent& event } => event.code;
            case _                                   => throw std::logic_error("world event has no payload");
        };
    }
};
} //namespace simcoaster
