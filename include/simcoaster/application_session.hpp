#pragma once
#include "application_coaster_source.hpp"
#include "application_game_ui.hpp"
#include "application_profiles.hpp"
#include "park_runtime.hpp"
#include <deque>

namespace simcoaster {
enum class ApplicationSessionPage {
    intro,
    profiles,
    chooser,
    park,
    coaster_designer,
    quit
};
//State10 distinguishes the first Science prebuilt from an ordinary save even
//though both restore TPWS. This is transient entry policy, never save data.
enum class ApplicationWorldEntry {
    none,
    fresh,
    first_prebuilt,
    restored
};
enum class ApplicationSessionFailure {
    state,
    profile,
    source,
    load,
    save,
    copy,
    remove,
    stale_file,
    external
};

struct ApplicationSessionError {
    ApplicationSessionFailure failure {};
    std::filesystem::path path;
    std::string detail;
};

struct ApplicationSessionHost {
    //Called at real asset-read boundaries while a replacement is staged.
    //The host pumps only loading/window messages, never old World callbacks.
    std::function<void()> resource_pump;
    std::function<ApplicationHardwareProfile()> hardware_profile;
    //Called after a replacement World has been staged. Destroy frontend owners
    //and clear every borrowed input/render
    //reference here, while the previous World still exists. This callback must
    //not throw and must remain valid through ApplicationSession destruction.
    std::function<void(ParkRuntime*)> release_world_views;
    //Binding may throw. The outgoing World survives until binding and deferred
    //file publication succeed. Recovery calls this again with binding_rollback()
    //true; reconstruct views without restarting Intro/State10 presentation.
    std::function<void(ParkRuntime*, ApplicationSessionPage)> bind_world;
    //Completion precedes release_world_views so the request's old UI owner can
    //close successfully without accessing the replacement World's controls.
    std::function<void(std::uint64_t, std::optional<ApplicationSessionError>)> complete_request;
    //Mailbox/address-book and hardware configuration have separate codecs and
    //owners. These hooks report actual errors; they are not successful no-ops.
    //An empty directory with loading=true clears the selected profile services.
    std::function<std::expected<void, ApplicationSessionError>(const std::filesystem::path&, bool loading)> profile_services;
    std::function<std::expected<void, ApplicationSessionError>()> save_configuration;
    std::function<std::expected<void, ApplicationSessionError>(const ApplicationGameRequest&)> external_request;
};

//Owns the active World and its campaign across the original profile/source/
//save/load/menu loop. UI callbacks enqueue World transitions; process_requests
//is called after notification dispatch, never from a borrowed control
//callback.
class ApplicationSession {
    struct State;
    std::unique_ptr<State> state_;

public:
    ApplicationSession(const std::filesystem::path& game_root, std::span<const std::string_view> registered_sources, UiPreferences native_defaults, ApplicationSessionHost);
    ~ApplicationSession();
    ApplicationSession(const ApplicationSession&) = delete;
    ApplicationSession& operator=(const ApplicationSession&) = delete;
    ParkRuntime* park();
    const ParkRuntime* park() const;
    ApplicationProfiles& profiles();
    const CampaignProfile* campaign() const;
    CampaignTipHistory& tip_history();
    bool take_post_load_chooser_pending();
    ApplicationWorldEntry world_entry() const;
    bool binding_rollback() const;
    const ApplicationCoasterSourceSelection* current_coaster_source() const;
    ApplicationSessionPage page() const;
    std::int32_t selected_source() const;
    std::int32_t active_source() const;
    void select_source(std::int32_t);
    std::expected<void, ApplicationSessionError> boot_intro();
    std::expected<void, ApplicationSessionError> activate_profile(std::uint32_t);
    std::expected<void, ApplicationSessionError> create_profile(std::uint32_t, std::u16string_view, bool easy_template = false);
    std::expected<void, ApplicationSessionError> erase_profile(std::uint32_t, std::uint64_t expected_generation);
    void request_source(std::int32_t, bool from_profile = false, bool coaster_designer = false);
    void request_coaster_source(ApplicationCoasterSourceSelection);
    std::expected<bool, ApplicationSessionError> begin_same_source(const ApplicationCoasterSourceSelection&);
    std::expected<void, ApplicationSessionError> cancel_coaster_source_profile_route();
    std::expected<void, ApplicationSessionError> recreate_graphics(const ApplicationHardwareProfile&);
    void request(ApplicationGameRequest);
    //Startup/diagnostic entry still uses the same staged replacement boundary.
    void request_saved_park(std::filesystem::path, std::string theme);
    void request_quit();
    void process_requests();
    bool has_pending_requests() const;
    bool has_pending_world_transition() const;
    std::expected<std::vector<ApplicationFileEntry>, ApplicationSessionError> list_files(ApplicationFileKind);
    std::expected<void, ApplicationSessionError> save_current_profile();
    const std::vector<ApplicationSessionError>& diagnostics() const;
};
} //namespace simcoaster
