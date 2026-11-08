#pragma once
#include "world_types.hpp"
#include <expected>

namespace simcoaster {
struct World;
enum class MissionTransitionFailureKind {
    missing_asset,
    malformed_asset,
    missing_target,
    stale_transition
};

struct MissionTransitionFailure {
    MissionTransitionFailureKind kind{};
    std::string resource, reason;
};

//Process-owned dependencies. Never serialized or reconstructed from a player save.
struct PreparedMissionTransition {
    World* world{};
    std::uint64_t world_epoch{};
    ThemeGlobalInfoManager* manager{};
    std::int32_t mission{}, next_mission{};
    MissionResearchBalance directives;
    std::optional<MissionResearchBalance> next_balance;
    std::int32_t unlock_ordinal = -1;
    std::optional<ThemeGlobalBalance> unlock_balance;
    std::vector<std::byte> extent;
    ThingId target{};
    std::uint16_t target_definition{};
    const ThingHeader* target_identity{};
    std::uint64_t target_creation_time{};
    bool applying{}, target_consumed{}, target_invalidated{};
};

struct MissionTransitionRuntime {
    std::uint64_t world_epoch{};
    std::unique_ptr<PreparedMissionTransition> prepared;
    std::optional<MissionTransitionFailure> failure;
    bool committing{};
};
} //namespace simcoaster
