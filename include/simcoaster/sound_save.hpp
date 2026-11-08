#pragma once
#include "sound_catalog.hpp"

namespace simcoaster {
enum class SoundTargetKind : std::uint32_t {
    conditional_sequence = 1,
    sequential = 2,
    single = 3,
    emitter = 4,
    weighted_sequence = 5,
    conditional_emitter = 6,
    child = 7,
    kind8 = 8,
    conditional_child = 9,
    playlist = 10
};

struct SoundSavedPayload {
    [[=save::Field { "keys", 0 }]] std::array<std::uint8_t, 4> keys {};
    [[=save::Field { "values", 1 }]] std::array<std::uint8_t, 4> values {};
    //Native order is max_z,min_x,max_x,min_z, not memory member order.
    [[=save::Field { "max_z", 2 }]] std::int32_t max_z {};
    [[=save::Field { "min_x", 3 }]] std::int32_t min_x {};
    [[=save::Field { "max_x", 4 }]] std::int32_t max_x {};
    [[=save::Field { "min_z", 5 }]] std::int32_t min_z {};
};

struct SoundSavedTargetFields {
    [[=save::Field { "position", 0 }]] std::array<float, 3> position {};
    [[=save::Field { "saved_zero", 1 }]] std::uint32_t saved_zero {};
    [[=save::Field { "parameter", 2 }]] float parameter {};
    [[=save::Field { "catalog_slot", 3 }]] std::uint32_t catalog {};
    [[=save::Field { "descriptor_index", 4 }]] std::uint32_t descriptor {};
    [[=save::Field { "selection_index", 5 }]] std::uint32_t selection {};
    [[=save::Field { "sample_index", 6 }]] std::uint32_t sample {};
};

static_assert(save::value_width<SoundSavedTargetFields> == 36);
static_assert(save::value_width<SoundSavedPayload> == 24);

struct SoundSavedTarget {
    SoundTargetKind kind = SoundTargetKind::single;
    SoundSavedTargetFields fields;
    std::optional<SoundSavedPayload> payload;
    std::uint8_t has_xz_bounds {};
    std::uint32_t target_id {};
};

struct SoundSavedTargetChain {
    std::uint32_t id {};
    //The original recursively serializes child_link. Flatten this owned chain
    //to keep input depth from consuming the C++ stack. Child kinds are7 or9.
    std::vector<SoundSavedTarget> nodes;
};

struct SoundBackendSnapshot {
    std::array<float, 3> listener {};
    SoundCatalogReferences catalogs;
    std::uint32_t pool_blocks {};
    std::vector<SoundSavedTargetChain> targets;
};

struct SoundBackendSnapshotPrefix {
    SoundBackendSnapshot snapshot;
    std::size_t bytes_consumed {};
};

//Native89D9D0 leaves extent zero when its active list is empty, despite
//writing the catalog map and pool count. Bound that form by its actual fields,
//and retain strict declared extents for every ordinary nonzero block.
std::expected<SoundBackendSnapshotPrefix, SoundError> parse_sound_backend_snapshot_prefix(std::span<const std::byte>);
std::expected<SoundBackendSnapshot, SoundError> parse_sound_backend_snapshot(std::span<const std::byte>);
std::expected<std::vector<std::byte>, SoundError> encode_sound_backend_snapshot(const SoundBackendSnapshot&);
} //namespace simcoaster
