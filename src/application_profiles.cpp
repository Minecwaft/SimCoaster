#include "simcoaster/application_profiles.hpp"
#include "simcoaster/file_platform.hpp"
#include "simcoaster/sam_integer.hpp"
#include "simcoaster/sam_record.hpp"
#include "simcoaster/world_startup.hpp"
#include <format>
#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace simcoaster {
namespace {
struct ShareDefaults {
    [[=sam::Group { "ShareInfo" }]] ShareStartupParameters values;
};

CampaignProfileError error(const std::filesystem::path& path, std::string reason) {
    return { path, 0, std::move(reason) };
}

std::string folded(std::string text) {
    for (auto& letter : text) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return text;
}

std::expected<std::string, CampaignProfileError> read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return std::unexpected(error(path, "Cannot open original profile balance"));
    }
    const auto length = input.tellg();
    if (length < 0 || length > 16777216) {
        return std::unexpected(error(path, "Invalid profile balance size"));
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    input.seekg(0);
    if (!input.read(result.data(), length)) {
        return std::unexpected(error(path, "Cannot read original profile balance"));
    }
    return result;
}

bool valid_component(std::u16string_view name) {
    return !name.empty() && std::ranges::none_of(name, [](char16_t letter) {
        return !letter || std::u16string_view(u"\\/*?:|<>\"").contains(letter);
    });
}

std::int32_t percentage(std::int32_t shares, std::int32_t issued) {
    if (!issued) {
        return 0;
    }
    const auto product = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(shares) * 100u);
    if (product == std::numeric_limits<std::int32_t>::min() && issued == -1) {
        throw std::domain_error("Profile share ownership division overflow");
    }
    return product / issued;
}

std::optional<std::filesystem::path> newest(const std::filesystem::path& directory) {
    std::optional<std::filesystem::path> result;
    std::filesystem::file_time_type timestamp {};
    if (!std::filesystem::is_directory(directory)) {
        return result;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        //Native74CB70 uses *.GMS*: backups bearing a suffix after GMS also
        //qualify, while .GMI metadata does not become the active profile master.
        const auto name = folded(entry.path().filename().native_encoded_string());
        if (name.find(".gms") == name.npos) {
            continue;
        }
        const auto written = entry.last_write_time();
        if (!result || written > timestamp) {
            result = entry.path();
            timestamp = written;
        }
    }
    return result;
}
} //namespace

struct ApplicationProfiles::State {
    struct Slot {
        ApplicationProfileView view;
        std::filesystem::path directory;
        file_platform::DirectoryIdentity disk_identity;
    };

    std::filesystem::path root, users;
    std::vector<std::string> sources;
    UiPreferences options;
    std::function<CampaignProfile()> make_campaign;
    ShareStartupParameters shares;
    std::array<bool, 3> available {};
    std::array<Slot, 5> slots;
    std::optional<std::uint32_t> selected;

    struct Activation {
        std::uint32_t slot {};
        std::uint64_t generation {};
        file_platform::DirectoryIdentity identity;
    };

    std::optional<Activation> staged_activation;
    std::uint64_t generation {}, selected_generation {};
    std::vector<CampaignProfileError> diagnostics;

    State(const std::filesystem::path& path, std::span<const std::string_view> registered_sources, UiPreferences defaults, std::function<CampaignProfile()> factory) : root(std::filesystem::absolute(path).lexically_normal()), users(root / "save/users"), options(defaults), make_campaign(std::move(factory)) {
        if (!make_campaign || registered_sources.empty()) {
            throw std::invalid_argument("Profile storage requires actual sources and fresh campaign factory");
        }
        for (const auto name : registered_sources) {
            if (name.empty() || name == "." || name == ".." || name.find_first_of("\\/:\0", 0, 4) != name.npos) {
                throw std::invalid_argument("Invalid registered profile resource source");
            }
            sources.emplace_back(name);
        }
        const auto standard_path = root / "Data/levels/Standard.sam";
        const auto standard = read_text(standard_path);
        if (!standard) {
            throw std::runtime_error(std::format("Cannot load original ShareInfo profile defaults: {}: {}", standard_path.native_encoded_string(), standard.error().reason));
        }
        //Standard.sam is a mixed float/grid/string/integer receiver. Select its
        //actual ShareInfo group, retaining all five original leaves; the strict
        //integer-only projection rejects the preceding unrelated groups.
        ShareDefaults parsed_shares;
        const auto parsed = sam::parse_record<ShareDefaults, true>(parsed_shares, *standard);
        if (!parsed) {
            throw std::runtime_error(std::format("Invalid original ShareInfo profile defaults: {} at byte {}", standard_path.native_encoded_string(), parsed.error().offset));
        }
        shares = parsed_shares.values;
        constexpr std::array<std::string_view, 3> themes { "arabian", "science", "water" };
        for (std::size_t index = 0; index < themes.size(); ++index) {
            const auto balance_path = root / "Data/levels" / themes[index] / "global.sam";
            const auto text = read_text(balance_path);
            //The genuine theme schema includes Tickets and ParkName as well as
            //ParkInfo; do not feed the whole file to a one-field strict projection.
            ThemeGlobalBalance balance;
            if (!text || !sam::parse_integer_record(balance, *text)) {
                throw std::runtime_error(std::format("Cannot load original profile theme availability: {}", balance_path.native_encoded_string()));
            }
            available[index] = balance.available_from_start == 1;
        }
        for (auto& slot : slots) {
            slot.view.themes[1].available = true; //Native empty-slot reset, not a park unlock.
        }
    }

    Slot& slot(std::uint32_t index) {
        if (index >= slots.size()) {
            throw std::out_of_range("Profile slot index");
        }
        return slots[index];
    }

    const Slot& slot(std::uint32_t index) const {
        if (index >= slots.size()) {
            throw std::out_of_range("Profile slot index");
        }
        return slots[index];
    }

    std::expected<void, CampaignProfileError> load_mission(CampaignProfile& profile) const {
        if (!profile.theme) {
            return std::unexpected(error(root, "Campaign has no theme manager"));
        }
        const auto path = root / "Data/missions" / std::format("Miss{:02}.sam", profile.theme->mission_number);
        //Native activation retains decoded data even when the loader returns
        //false for Miss-1.sam. Completed campaigns have no such installed SAM.
        if (profile.theme->mission_number == -1 && !std::filesystem::exists(path)) {
            return {};
        }
        const auto text = read_text(path);
        if (!text) {
            return std::unexpected(text.error());
        }
        if (!text->empty() && !sam::parse_integer_record(profile.theme->mission_research, *text)) {
            return std::unexpected(error(path, "Invalid profile mission balance"));
        }
        return {};
    }

    std::expected<CampaignProfile, CampaignProfileError> load(std::uint32_t index, std::optional<std::filesystem::path> exact_metadata = {}) const {
        const auto& entry = slot(index);
        const auto path = exact_metadata ? std::optional { std::filesystem::absolute(*exact_metadata).lexically_normal() } : newest(entry.directory);
        if (!path) {
            return std::unexpected(error(entry.directory, "Profile has no *.GMS* master metadata"));
        }
        if (exact_metadata && !std::filesystem::equivalent(path->parent_path(), entry.directory)) {
            return std::unexpected(error(*path, "Selected metadata belongs to a different player directory"));
        }
        auto profile = load_campaign_profile(*path, options);
        if (!profile) {
            return profile;
        }
        //622130 obtains this transient label from the selected profile slot;
        //it is deliberately absent from the GMS shares serialization.
        profile->shares.ownership_context->current_profile_name = entry.view.name;
        if (const auto mission = load_mission(*profile); !mission) {
            return std::unexpected(mission.error());
        }
        profile->theme->save_file_basename = path->stem().u16string();
        return profile;
    }

    void summarize(Slot& entry, const CampaignProfile& profile) const {
        auto& view = entry.view;
        const auto* theme = profile.theme.get();
        view.promotion = theme && theme->current_promotion_index ? theme->current_promotion_index : 1;
        const auto* ownership = profile.shares.ownership_context.get();
        const auto* price = profile.shares.share_price_context.get();
        view.shares_owned = ownership && ownership->shares_owned != -1 ? ownership->shares_owned : shares.InitialSharesOwned;
        view.share_price = price && price->current_share_price != -1 ? price->current_share_price : shares.InitialSharePrice;
        view.share_percentage = ownership && ownership->ownership_percentage != -1 ? ownership->ownership_percentage : percentage(view.shares_owned, shares.CompanySharesIssued);
        view.golden_tickets = theme ? theme->golden_ticket_balance : 0;
        view.last_mission = theme ? theme->mission_number : -1;
        view.last_theme = static_cast<ApplicationThemeCategory>(theme ? theme->mission_research.ending_theme : 0);
        constexpr std::array<std::string_view, 3> names { "arabian", "science", "water" };
        for (std::size_t index = 0; index < names.size(); ++index) {
            const ThemeGlobalSaveInfo* info {};
            if (theme) {
                for (const auto& [name, record] : theme->theme_info_records) {
                    if (folded(name) == names[index]) {
                        info = &record;
                        break;
                    }
                }
            }
            auto& target = view.themes[index];
            target.available = info ? info->is_available != 0 : available[index];
            target.completed_challenges = target.available && info ? static_cast<std::uint16_t>(info->challenges_done) : 0;
        }
    }

    std::expected<void, CampaignProfileError> save(std::uint32_t index, CampaignProfile& profile, std::u16string_view basename, bool gmi) {
        auto& entry = slot(index);
        if (!entry.view.present || !profile.theme || !valid_component(basename)) {
            return std::unexpected(error(entry.directory, "Invalid profile metadata destination"));
        }
        profile.theme->save_file_basename = basename;
        profile.theme->use_gmi_suffix = gmi ? 1 : 0;
        if (profile.selected_source == -1) {
            profile.selected_source = 3;
        }
        const auto path = entry.directory / std::filesystem::path(std::u16string(basename) + (gmi ? u".GMI" : u".GMS"));
        const auto written = write_campaign_profile(path, profile);
        if (!written) {
            return written;
        }
        summarize(entry, profile);
        return {};
    }
};

ApplicationProfiles::ApplicationProfiles(const std::filesystem::path& root, std::span<const std::string_view> sources, UiPreferences defaults, std::function<CampaignProfile()> factory) : state_(std::make_unique<State>(root, sources, defaults, std::move(factory))) {}

ApplicationProfiles::~ApplicationProfiles() = default;

std::expected<void, CampaignProfileError> ApplicationProfiles::refresh() {
    auto& s = *state_;
    try {
        std::filesystem::create_directories(s.users);
        std::filesystem::create_directories(s.root / "save/online");
        const auto previous = s.slots;
        std::array<State::Slot, 5> staged;
        for (auto& slot : staged) {
            slot.view.themes[1].available = true;
        }
        for (const auto& entry : std::filesystem::directory_iterator(s.users)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto filename = entry.path().filename().u16string();
            if (filename.size() < 2 || filename.front() < u'1' || filename.front() > u'5') {
                continue;
            }
            const auto index = static_cast<std::size_t>(filename.front() - u'1');
            auto& target = staged[index];
            if (target.view.present) {
                s.diagnostics.push_back(error(entry.path(), "Multiple directories claim the same native " "profile slot; last enumerated wins"));
            }
            target.directory = entry.path();
            target.disk_identity = file_platform::directory_identity(target.directory);
            target.view.present = true;
            target.view.name = filename.substr(1);
            target.view.generation = previous[index].view.present && previous[index].directory == target.directory && previous[index].disk_identity == target.disk_identity ? previous[index].view.generation : ++s.generation;
        }
        s.slots = std::move(staged);
        for (std::uint32_t index = 0; index < s.slots.size(); ++index) {
            auto& entry = s.slots[index];
            if (!entry.view.present) {
                continue;
            }
            auto loaded = s.load(index);
            if (loaded) {
                s.summarize(entry, *loaded);
            } else {
                s.diagnostics.push_back(loaded.error());
                auto defaults = s.make_campaign();
                s.summarize(entry, defaults);
            }
        }
        if (s.selected && (!s.slot(*s.selected).view.present || s.slot(*s.selected).view.generation != s.selected_generation)) {
            clear_selection();
        }
        return {};
    } catch (const std::filesystem::filesystem_error& failure) {
        return std::unexpected(error(failure.path1(), failure.what()));
    }
}

std::array<ApplicationProfileView, 5> ApplicationProfiles::views() const {
    std::array<ApplicationProfileView, 5> result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = state_->slots[index].view;
    }
    return result;
}

std::optional<std::uint32_t> ApplicationProfiles::selected_slot() const {
    return state_->selected;
}

std::filesystem::path ApplicationProfiles::profile_directory(std::uint32_t slot) const {
    return state_->slot(slot).directory;
}

std::filesystem::path ApplicationProfiles::source_directory(std::uint32_t slot, std::uint32_t source) const {
    return profile_directory(slot) / state_->sources.at(source);
}

std::optional<std::filesystem::path> ApplicationProfiles::newest_metadata(std::uint32_t slot) const {
    return newest(profile_directory(slot));
}

std::expected<CampaignProfile, CampaignProfileError> ApplicationProfiles::activate(std::uint32_t slot, CampaignProfile* previous) {
    auto& s = *state_;
    if (slot >= 4 || !s.slot(slot).view.present) {
        return std::unexpected(error(s.users, "Invalid ordinary offline profile selection"));
    }
    try {
        if (s.selected && previous) {
            if (const auto saved = save_selected(*previous); !saved) {
                return std::unexpected(saved.error());
            }
        }
        auto profile = load_for_activation(slot);
        if (!profile) {
            return profile;
        }
        if (const auto committed = commit_activation(slot); !committed) {
            return std::unexpected(committed.error());
        }
        return profile;
    } catch (const std::filesystem::filesystem_error& failure) {
        return std::unexpected(error(failure.path1(), failure.what()));
    }
}

std::expected<CampaignProfile, CampaignProfileError> ApplicationProfiles::load_for_activation(std::uint32_t slot, std::optional<std::filesystem::path> exact_metadata) {
    auto& s = *state_;
    s.staged_activation.reset();
    if (slot >= 4 || !s.slot(slot).view.present) {
        return std::unexpected(error(s.users, "Invalid ordinary offline profile selection"));
    }
    try {
        const auto& entry = s.slot(slot);
        if (file_platform::directory_identity(entry.directory) != entry.disk_identity) {
            return std::unexpected(error(entry.directory, "Player directory was replaced before activation"));
        }
        auto profile = s.load(slot, std::move(exact_metadata));
        if (!profile) {
            return profile;
        }
        if (file_platform::directory_identity(entry.directory) != entry.disk_identity) {
            return std::unexpected(error(entry.directory, "Player directory was replaced during activation"));
        }
        s.staged_activation = State::Activation { slot, entry.view.generation, entry.disk_identity };
        s.summarize(s.slot(slot), *profile);
        return profile;
    } catch (const std::filesystem::filesystem_error& failure) {
        return std::unexpected(error(failure.path1(), failure.what()));
    }
}

std::expected<void, CampaignProfileError> ApplicationProfiles::commit_activation(std::uint32_t slot) {
    auto& s = *state_;
    if (!s.staged_activation || s.staged_activation->slot != slot) {
        return std::unexpected(error(s.users, "No staged activation for this player"));
    }
    const auto staged = *s.staged_activation;
    s.staged_activation.reset();
    try {
        const auto& entry = s.slot(slot);
        if (!entry.view.present || entry.view.generation != staged.generation || file_platform::directory_identity(entry.directory) != staged.identity) {
            return std::unexpected(error(entry.directory, "Player identity changed before activation commit"));
        }
        s.selected = slot;
        s.selected_generation = staged.generation;
        return {};
    } catch (const std::filesystem::filesystem_error& failure) {
        return std::unexpected(error(failure.path1(), failure.what()));
    }
}

std::expected<void, CampaignProfileError> ApplicationProfiles::create(std::uint32_t index, std::u16string_view input, bool copy_easymode) {
    auto& s = *state_;
    if (index >= s.slots.size()) {
        return std::unexpected(error(s.users, "Invalid profile creation slot"));
    }
    std::u16string name(input);
    while (!name.empty() && name.back() == u' ') {
        name.pop_back();
    }
    //No profanity/word-list check exists in5697B0/7765D0. The native editbox
    //provides these filename exclusions and32-WCHAR capacity only.
    if (name.size() > 32 || !valid_component(name)) {
        return std::unexpected(error(s.users, "Invalid original player name"));
    }
    auto& entry = s.slot(index);
    if (entry.view.present) {
        if (const auto removed = erase(index, entry.view.generation); !removed) {
            return removed;
        }
    }
    const auto directory = s.users / std::filesystem::path(std::u16string(1, static_cast<char16_t>(u'1' + index)) + name);
    try {
        auto campaign = s.make_campaign();
        if (std::filesystem::exists(directory)) {
            return std::unexpected(error(directory, "Profile destination already exists outside the " "current slot identity"));
        }
        std::filesystem::create_directories(directory);
        for (const auto& source : s.sources) {
            const auto destination = directory / source;
            std::filesystem::create_directories(destination);
            const auto original = s.root / "Data/levels" / source / "easymode.TPWI";
            if (copy_easymode && std::filesystem::is_regular_file(original)) {
                std::filesystem::copy_file(original, destination / "easymode.TPWI", std::filesystem::copy_options::overwrite_existing);
            }
        }
        entry.directory = directory;
        entry.disk_identity = file_platform::directory_identity(directory);
        entry.view = {};
        entry.view.present = true;
        entry.view.name = std::move(name);
        entry.view.generation = ++s.generation;
        if (const auto written = s.save(index, campaign, u"autosave", false); !written) {
            return written;
        }
        //Native newly-created slot snapshots predate reloading its GMS. The UI
        //starts with no last mission, Science available, and Standard share
        //values.
        entry.view.promotion = 1;
        entry.view.last_mission = -1;
        entry.view.last_theme = ApplicationThemeCategory::arabian;
        entry.view.golden_tickets = 0;
        entry.view.shares_owned = s.shares.InitialSharesOwned;
        entry.view.share_price = s.shares.InitialSharePrice;
        entry.view.share_percentage = percentage(s.shares.InitialSharesOwned, s.shares.CompanySharesIssued);
        entry.view.themes = {};
        entry.view.themes[1].available = true;
        return {};
    } catch (const std::filesystem::filesystem_error& failure) {
        return std::unexpected(error(failure.path1(), failure.what()));
    }
}

std::expected<void, CampaignProfileError> ApplicationProfiles::erase(std::uint32_t index, std::uint64_t expected) {
    auto& s = *state_;
    if (index >= s.slots.size()) {
        return std::unexpected(error(s.users, "Invalid profile deletion slot"));
    }
    auto& entry = s.slot(index);
    if (!entry.view.present || entry.view.generation != expected) {
        return std::unexpected(error(entry.directory, "Player identity changed before deletion"));
    }
    const auto directory = entry.directory;
    std::optional<CampaignProfileError> failed;
    try {
        if (file_platform::directory_identity(directory) != entry.disk_identity) {
            return std::unexpected(error(directory, "Profile directory identity changed before deletion"));
        }
        //Resolve and check the absolute recursive-delete target before mutation.
        //Reject a directory junction which resolves outside the named users root.
        const auto root = std::filesystem::weakly_canonical(s.users);
        const auto target = std::filesystem::weakly_canonical(directory);
        if (target == root || target.parent_path() != root) {
            return std::unexpected(error(directory, "Profile deletion target is outside users root"));
        }
        std::filesystem::remove_all(directory);
    } catch (const std::filesystem::filesystem_error& failure) {
        failed = error(failure.path1(), failure.what());
    }
    entry = {};
    entry.view.generation = ++s.generation;
    entry.view.themes[1].available = true;
    if (s.selected == index) {
        clear_selection();
    }
    if (failed) {
        s.diagnostics.push_back(*failed);
        return std::unexpected(*failed);
    }
    return {};
}

std::expected<void, CampaignProfileError> ApplicationProfiles::save_selected(CampaignProfile& profile, std::u16string_view basename, bool gmi) {
    auto& s = *state_;
    if (!s.selected || !s.slot(*s.selected).view.present || s.slot(*s.selected).view.generation != s.selected_generation) {
        return std::unexpected(error(s.users, "No unchanged active player to save"));
    }
    try {
        const auto& entry = s.slot(*s.selected);
        if (file_platform::directory_identity(entry.directory) != entry.disk_identity) {
            return std::unexpected(error(entry.directory, "Active player directory was replaced before saving"));
        }
        return s.save(*s.selected, profile, basename, gmi);
    } catch (const std::filesystem::filesystem_error& failure) {
        return std::unexpected(error(failure.path1(), failure.what()));
    }
}

std::expected<void, CampaignProfileError> ApplicationProfiles::restore_selection(std::optional<std::uint32_t> slot, std::uint64_t expected_generation) {
    if (!slot) {
        clear_selection();
        return {};
    }
    auto& s = *state_;
    if (*slot >= 4) {
        return std::unexpected(error(s.users, "Invalid player recovery identity"));
    }
    try {
        const auto& entry = s.slot(*slot);
        if (!entry.view.present || entry.view.generation != expected_generation || file_platform::directory_identity(entry.directory) != entry.disk_identity) {
            return std::unexpected(error(entry.directory, "Player identity changed during recovery"));
        }
        s.selected = slot;
        s.selected_generation = expected_generation;
        s.staged_activation.reset();
        return {};
    } catch (const std::filesystem::filesystem_error& failure) {
        return std::unexpected(error(failure.path1(), failure.what()));
    }
}

void ApplicationProfiles::clear_selection() {
    state_->selected.reset();
    state_->selected_generation = 0;
    state_->staged_activation.reset();
}

const std::vector<CampaignProfileError>& ApplicationProfiles::diagnostics() const {
    return state_->diagnostics;
}
} //namespace simcoaster
