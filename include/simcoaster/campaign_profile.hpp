#pragma once
#include "ui_preferences.hpp"
#include "world.hpp"

namespace simcoaster {
//Profile-owned state lives across source World destruction. These are native
//wire values, never process pointers or an assumed fixed set of theme keys.
struct CampaignTipHistory {
    std::int32_t count {};
    std::array<std::uint8_t, 92> seen {};
};

struct CampaignSpamHistory {
    std::vector<std::uint8_t> sent;
    std::uint8_t special_text {};
    std::int32_t year {}, elapsed_days {}, sends_this_year {};
};

struct CampaignAmbientRecord {
    std::uint32_t event_id {}, sound_handle {}, resource_source_index {};
    //5BF470 ignores this fourth word on read. Preserve an imported opaque word
    //for lossless decoding; a newly created record writes zero, never a pointer.
    std::uint32_t legacy_link_word {};
};

struct CampaignAuxiliaryState {
    CampaignTipHistory tips;
    //Native list write order. Its reader prepends, reversing this vector when
    //materializing the live ambience scheduler, not when round-tripping bytes.
    std::vector<CampaignAmbientRecord> ambient;
    CampaignSpamHistory spam;
};

struct CampaignProfile {
    std::int32_t selected_source {};
    std::uint32_t version = 12;
    std::unique_ptr<ThemeGlobalInfoManager> theme = std::make_unique<ThemeGlobalInfoManager>();
    SharesFinanceState shares;
    UiPreferences options;
    std::uint32_t first_hour_time {};
    CampaignAuxiliaryState auxiliary;
};

struct CampaignProfileError {
    std::filesystem::path path;
    std::size_t offset {};
    std::string reason;
};

//74D110/74D840 and their complete subordinate transfers. Input is staged and
//checked through EOF; a failed load cannot partially update a live campaign.
std::expected<CampaignProfile, CampaignProfileError> decode_campaign_profile(std::span<const std::byte>, UiPreferences defaults = {});
std::expected<std::vector<std::byte>, CampaignProfileError> encode_campaign_profile(const CampaignProfile&);
std::expected<CampaignProfile, CampaignProfileError> load_campaign_profile(const std::filesystem::path&, UiPreferences defaults = {});
std::expected<void, CampaignProfileError> write_campaign_profile(const std::filesystem::path&, const CampaignProfile&);
//Apply before creating/restoring World records. Loading the mission SAM first
//is essential: its initializer clears the tracking rows subsequently imported.
void apply_campaign_profile(WorldRuntime&, const CampaignProfile&);
//Native profile selection refreshes global campaign owners while the lobby
//World and its ATM/Intro scripts remain alive. The application closes World
//management pages before using this entry point.
void apply_campaign_profile_to_lobby(WorldRuntime&, const CampaignProfile&);
CampaignProfile create_fresh_campaign_profile(WorldRuntime& service_host, const UiPreferences&, CampaignAuxiliaryState = {});
CampaignProfile capture_campaign_profile(const WorldRuntime&, const UiPreferences&, std::int32_t selected_source, CampaignAuxiliaryState);
} //namespace simcoaster
