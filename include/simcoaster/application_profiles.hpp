#pragma once
#include "application_frontend.hpp"
#include "campaign_profile.hpp"

namespace simcoaster {
//Disk/profile identity and summaries only. The application session owns its
//active CampaignProfile, World, source changes, configuration and mail state.
class ApplicationProfiles {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationProfiles(const std::filesystem::path& game_root,
                        std::span<const std::string_view> registered_sources,
                        UiPreferences default_options,
                        std::function<CampaignProfile()> new_campaign_factory);
    ~ApplicationProfiles();
    ApplicationProfiles(const ApplicationProfiles&) = delete;
    ApplicationProfiles& operator=(const ApplicationProfiles&) = delete;
    //All five directories are enumerated. Native offline Profile controls use
    //slots0..3; selected mode4 is reserved and must not load the fifth as a new
    //offline button. Corrupt/missing metadata retains the directory identity
    //and a diagnostic, but activation reports the failure.
    std::expected<void, CampaignProfileError> refresh();
    std::array<ApplicationProfileView, 5> views() const;
    std::optional<std::uint32_t> selected_slot() const;
    std::filesystem::path profile_directory(std::uint32_t slot) const;
    std::filesystem::path source_directory(std::uint32_t slot, std::uint32_t resource_source) const;
    std::optional<std::filesystem::path> newest_metadata(std::uint32_t slot) const;
    //Native call sites persist the previous selection before loading another.
    //Pass its current captured campaign; on any I/O/decode failure, the active
    //selection remains unchanged. Mail/configuration hooks remain session-owned.
    std::expected<CampaignProfile, CampaignProfileError> activate(std::uint32_t slot,
                                                                  CampaignProfile* previous = nullptr);
    //Stage import before publishing the selected player. Commit validates the
    //staged directory identity without rereading or writing metadata.
    std::expected<CampaignProfile, CampaignProfileError>
    load_for_activation(std::uint32_t slot, std::optional<std::filesystem::path> exact_metadata = {});
    std::expected<void, CampaignProfileError> commit_activation(std::uint32_t slot);
    //Roll back an already selected player's identity without reloading a newer
    //GMS or writing metadata. Reject replacement/deletion of its directory.
    std::expected<void, CampaignProfileError> restore_selection(std::optional<std::uint32_t> slot,
                                                                std::uint64_t expected_generation);
    std::expected<void, CampaignProfileError>
    create(std::uint32_t slot, std::u16string_view name, bool copy_easymode_template = false);
    //Native7764C0 clears the slot even if recursive directory removal fails.
    //Return the actual error while invalidating generation/selection so a
    //stale confirmation cannot delete a subsequently created player.
    std::expected<void, CampaignProfileError> erase(std::uint32_t slot, std::uint64_t expected_generation);
    //Native writer updates basename/use_gmi in the active manager. The session
    //must skip automatic GMS writes in coaster-editor mode3 (777120).
    std::expected<void, CampaignProfileError>
    save_selected(CampaignProfile&, std::u16string_view basename = u"autosave", bool gmi = false);
    void clear_selection();
    const std::vector<CampaignProfileError>& diagnostics() const;
};
} //namespace simcoaster
