#pragma once
#include "simcoaster/application_session.hpp"
#include "simcoaster/world.hpp"
#include <map>

namespace simcoaster::application_session_detail {
[[noreturn]] void fail(ApplicationSessionFailure, const std::filesystem::path&, std::string);
ApplicationSessionError profile_error(const CampaignProfileError&);
bool equal_path(const std::filesystem::path&, const std::filesystem::path&);
bool valid_basename(std::u16string_view);
bool extension_is(const std::filesystem::path&, std::u16string_view);

struct FileIdentity {
    std::filesystem::path path;
    ApplicationFileKind kind{};
    std::optional<std::uint32_t> profile;
    std::uint64_t profile_generation{};
    std::array<std::uint32_t, 7> stamp{};
};

std::array<std::uint32_t, 7> file_stamp(const std::filesystem::path&);
ApplicationFileDate file_date(const std::filesystem::path&);

class FileTransaction {
    struct Entry {
        std::filesystem::path destination, staged, backup;
        bool existed{}, published{};
    };

    std::filesystem::path directory_;
    std::vector<Entry> entries_;
    bool preserve_{};

public:
    explicit FileTransaction(const std::filesystem::path& root);
    ~FileTransaction();
    std::filesystem::path stage(const std::filesystem::path&);
    std::filesystem::path read(const std::filesystem::path&) const;
    void copy(const std::filesystem::path& source, const std::filesystem::path& destination);
    void commit();
};
} //namespace simcoaster::application_session_detail

namespace simcoaster {
struct ApplicationSession::State {
    struct SourceTransition {
        std::int32_t source{};
        bool from_profile{}, designer{};
        std::optional<ApplicationCoasterSourceSelection> coaster_source;
        std::uint64_t serial{};
    };

    struct SavedParkTransition {
        std::filesystem::path path;
        std::string theme;
    };

    struct GameTransition {
        ApplicationGameRequest request;
        std::optional<std::pair<std::uint32_t, std::uint64_t>> coaster_mesh;
    };

    struct QuitTransition {};

    struct Pending {
        std::uint64_t epoch{};
        std::variant<SourceTransition, GameTransition, SavedParkTransition, QuitTransition> payload;
    };

    std::filesystem::path root;
    std::vector<std::string> sources;
    UiPreferences defaults;
    ApplicationSessionHost host;
    std::unique_ptr<ParkRuntime> park;
    std::optional<CampaignProfile> campaign;
    std::optional<ApplicationCoasterSourceSelection> coaster_selection;
    std::unique_ptr<ApplicationProfiles> profiles;
    ApplicationSessionPage page = ApplicationSessionPage::intro;
    std::int32_t selected = 3, active = -1;
    std::uint64_t epoch{}, next_file{};
    std::deque<Pending> requests;
    std::map<std::uint64_t, application_session_detail::FileIdentity> files;
    std::vector<ApplicationSessionError> diagnostics;
    bool processing{};
    bool binding_rollback{}, transition_completed{};
    bool post_load_chooser_pending{};
    ApplicationWorldEntry world_entry = ApplicationWorldEntry::none;
    State(const std::filesystem::path&, std::span<const std::string_view>, UiPreferences, ApplicationSessionHost);
    CampaignProfile capture() const;
    void saved_metadata(const CampaignProfile&);
    std::filesystem::path profile_directory() const;
    std::filesystem::path source_file(std::int32_t, std::u16string_view, bool gmi = false) const;
    void complete(std::uint64_t, std::optional<ApplicationSessionError> = {});

    struct ProfileBinding {
        std::optional<std::uint32_t> slot;
    };

    void replace(std::unique_ptr<ParkRuntime>,
                 CampaignProfile,
                 std::int32_t,
                 ApplicationSessionPage,
                 std::uint64_t,
                 ApplicationWorldEntry = ApplicationWorldEntry::none,
                 bool post_load_chooser = false,
                 std::function<void()> publish = {},
                 std::optional<ApplicationCoasterSourceSelection> = {},
                 std::optional<ProfileBinding> = {});
    bool can_autosave() const;
    void
    stage_save(application_session_detail::FileTransaction&, CampaignProfile&, std::u16string_view, bool gmi = false);
    void persist_profile();
    void discard_intro_profile_selection();
    void source(const SourceTransition&);
    void load_named(const ApplicationLoadParkRequest&, std::uint64_t);
    void load_path(const SavedParkTransition&);
    void save_named(const ApplicationSaveParkRequest&, std::uint64_t);
    void save_coaster(const ApplicationSaveCoasterRequest&);
    void delete_file(const ApplicationFileSelection&, ApplicationFileKind);
    void game(const ApplicationGameRequest&);
    void quit(std::uint64_t);
    void validate_file(const ApplicationFileSelection&, ApplicationFileKind) const;
    std::vector<ApplicationFileEntry> list(ApplicationFileKind);
};
} //namespace simcoaster
