#pragma once
#include "world.hpp"
#include "sam_record.hpp"

namespace simcoaster {
struct RegionEffectStartupParameters {
    [[=sam::Field{ "Radius", sam::Kind::nonnegative_integer }]] std::int32_t radius{};
    [[=sam::Field{ "Happiness", sam::Kind::integer }]] std::int32_t happiness{};
    [[=sam::Field{ "Illness", sam::Kind::integer }]] std::int32_t illness{};
    [[=sam::Field{ "Hunger", sam::Kind::integer }]] std::int32_t hunger{};
    [[=sam::Field{ "Security", sam::Kind::integer }]] std::int32_t security{};
    [[=sam::Field{ "Attraction", sam::Kind::integer }]] std::int32_t attraction{};
};

struct ShareStartupParameters {
    [[=sam::Field{ "CompanySharesIssued", sam::Kind::integer }]] std::int32_t CompanySharesIssued{};
    [[=sam::Field{ "InitialSharesOwned", sam::Kind::integer }]] std::int32_t InitialSharesOwned{};
    [[=sam::Field{ "InitialSharePrice", sam::Kind::integer }]] std::int32_t InitialSharePrice{};
    [[=sam::Field{ "AnnRptSimilarPercentage", sam::Kind::integer }]] std::int32_t AnnRptSimilarPercentage{};
    [[=sam::Field{ "AnnRptBigChangePercentage", sam::Kind::integer }]] std::int32_t AnnRptBigChangePercentage{};
};

struct WorldStartupBalance {
    [[=sam::Group{ "ParkAward" }]] ParkAwardBalance awards;
    [[=sam::Group{ "ShareInfo" }]] ShareStartupParameters shares;
    [[=sam::Group{ "SpamBalance" }]] SpamBalance spam;
    [[=sam::Rows{ "RegionFX" }]] std::array<RegionEffectStartupParameters, 15> region_effects{};
    [[=sam::Extent{ "RegionFX" }]] std::uint32_t region_effect_count{};
};

void initialize_world_map_and_track_storage(World&, const WorldStartupBalance&);
void bind_restored_world_managers(WorldRuntime&, World&);
//Native MAP byte traversal transposes source rows/columns into World X/Y.
//Malformed dimensions/chunks are bounded instead of trusting pointer reads.
bool load_world_base_map(WorldRuntime&, World&, std::span<const std::byte>);
} //namespace simcoaster
