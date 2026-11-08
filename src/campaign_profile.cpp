#include "campaign_profile_archive.hpp"
#include "campaign_profile_io.hpp"
#include "simcoaster/world_theme.hpp"
#include <fstream>
#include <meta>
#include <stdexcept>
#include <system_error>

namespace simcoaster {
namespace {
using campaign_detail::io_error;

void copy_saved_manager(ThemeGlobalInfoManager& out, const ThemeGlobalInfoManager& in) {
    //Copy every persisted owner field through the real declarations. These six
    //excluded members are process state; the wire routines never transfer them.
    constexpr std::array<std::string_view, 6> transient { "swear_word_list", "allowed_word_list", "selected_theme_info", "mission_research", "save_file_basename", "reset_zero_reserved_dword" };
    constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(^^ThemeGlobalInfoManager, std::meta::access_context::current()));
    template for (constexpr auto member : members) {
        if constexpr (std::ranges::find(transient, std::meta::identifier_of(member)) == transient.end()) {
            out.[:member:] = in.[:member:];
        }
    }
    out.selected_theme_info = nullptr;
    for (auto& [source, value] : out.theme_info_records) {
        value.source_name = source;
        value.global_balance_fields.reset(); //Rebind lazily to this session's SAM.
    }
}

SharesFinanceState copy_shares(const SharesFinanceState& in) {
    SharesFinanceState out;
    out.ownership_context = in.ownership_context ? std::make_unique<SharesOwnershipContext>(*in.ownership_context) : std::make_unique<SharesOwnershipContext>();
    out.share_price_context = in.share_price_context ? std::make_unique<SharePriceContext>(*in.share_price_context) : std::make_unique<SharePriceContext>();
    for (std::size_t i = 0; i < in.record_slots.size(); ++i) {
        if (in.record_slots[i]) {
            out.record_slots[i] = std::make_unique<ShareFinanceSavedRecord>(*in.record_slots[i]);
        }
    }
    return out;
}
} //namespace

std::expected<CampaignProfile, CampaignProfileError> decode_campaign_profile(std::span<const std::byte> bytes, UiPreferences defaults) {
    CampaignProfile result;
    result.options = defaults;
    campaign_detail::Reader input { bytes };
    try {
        campaign_detail::profile(input, result);
        if (input.position != bytes.size()) {
            input.fail("Trailing bytes after native campaign profile");
        }
    } catch (const campaign_detail::Failure& failure) {
        return std::unexpected(CampaignProfileError { {}, failure.offset, failure.reason });
    }
    return result;
}

std::expected<std::vector<std::byte>, CampaignProfileError> encode_campaign_profile(const CampaignProfile& profile) {
    campaign_detail::Writer output;
    try {
        campaign_detail::profile(output, profile);
    } catch (const campaign_detail::Failure& failure) {
        return std::unexpected(CampaignProfileError { {}, failure.offset, failure.reason });
    }
    return std::move(output.bytes);
}

std::expected<CampaignProfile, CampaignProfileError> load_campaign_profile(const std::filesystem::path& path, UiPreferences defaults) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(io_error(path, "Cannot open campaign profile"));
    }
    const auto length = file.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return std::unexpected(io_error(path, "Invalid campaign profile size"));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return std::unexpected(io_error(path, "Cannot read complete campaign profile"));
    }
    auto result = decode_campaign_profile(bytes, defaults);
    if (!result) {
        result.error().path = path;
    }
    return result;
}

void apply_campaign_profile_to_lobby(WorldRuntime& c, const CampaignProfile& profile) {
    if (!profile.theme) {
        throw std::invalid_argument("Campaign import has no manager");
    }
    auto& manager = get_or_create_theme_global_info_manager(c);
    copy_saved_manager(manager, *profile.theme);
    manager.save_file_basename = profile.theme->save_file_basename;
    const auto tracking = manager.objective_entries;
    //Native activation ignores a missing mission file. Completed campaigns
    //use mission-1 without a Miss-1.sam; imported tracking still follows.
    (void)load_mission_balance_data(c, &manager);
    manager.objective_entries = tracking;
    //776C40 expands the actual registered sources after loading a profile.
    //Decoding alone must retain an empty/partial map byte-for-byte; activation
    //is where missing records acquire their genuine global.sam defaults.
    if (c.mode.game != GameMode::code_3) {
        for (std::size_t i = 0; i + 1 < theme_global_source_names.size(); ++i) {
            if (!get_or_create_theme_global_info_for_source(c, manager, theme_global_source_names[i])) {
                throw std::runtime_error("Cannot initialize campaign theme " + std::string { theme_global_source_names[i] });
            }
        }
    }
    const auto selected = manager.theme_info_records.find(c.source.selected_resource_source_name);
    if (selected != manager.theme_info_records.end()) {
        manager.selected_theme_info = &selected->second;
    }
    c.finance.shares_finance = copy_shares(profile.shares);
    c.timing.first_hour_time = profile.first_hour_time;
    const auto& saved = profile.auxiliary.spam;
    auto& spam = c.finance.spam_scheduler;
    for (std::size_t i = 0; i < spam.entries.size(); ++i) {
        spam.entries[i].sent = i < saved.sent.size() && saved.sent[i] != 0;
    }
    spam.special_text_flag = saved.special_text != 0;
    spam.year = std::bit_cast<std::uint32_t>(saved.year);
    spam.elapsed_days = saved.elapsed_days;
    spam.sends_this_year = saved.sends_this_year;
}

void apply_campaign_profile(WorldRuntime& c, const CampaignProfile& profile) {
    if (c.entities.active_head) {
        throw std::logic_error("Campaign import must precede World restoration");
    }
    apply_campaign_profile_to_lobby(c, profile);
}

CampaignProfile create_fresh_campaign_profile(WorldRuntime& host, const UiPreferences& options, CampaignAuxiliaryState auxiliary) {
    WorldRuntime fresh { host.ports };
    fresh.source.data_directory = host.source.data_directory;
    fresh.missions.missions_directory = host.missions.missions_directory;
    fresh.source.selected_resource_source_name = "wIntro";
    (void)get_or_create_theme_global_info_manager(fresh);
    //7765D0/777B80 write a reset manager with an EMPTY ThemeInfo map and
    //-1 shares sentinels. Slot summary ShareInfo defaults are separate; actual
    //per-source records are created only when776C40 activates that profile.
    return capture_campaign_profile(fresh, options, 3, std::move(auxiliary));
}

CampaignProfile capture_campaign_profile(const WorldRuntime& c, const UiPreferences& options, std::int32_t selected_source, CampaignAuxiliaryState auxiliary) {
    if (!c.missions.theme_global_info_manager) {
        throw std::logic_error("Campaign capture has no live manager");
    }
    CampaignProfile result;
    result.selected_source = selected_source;
    copy_saved_manager(*result.theme, *c.missions.theme_global_info_manager);
    result.theme->save_file_basename = c.missions.theme_global_info_manager->save_file_basename;
    result.shares = copy_shares(c.finance.shares_finance);
    result.options = options;
    result.first_hour_time = c.timing.first_hour_time;
    result.auxiliary = std::move(auxiliary);
    auto& saved = result.auxiliary.spam;
    const auto& spam = c.finance.spam_scheduler;
    //Preserve any imported unused native slots while updating every compiled
    //installed spam entry. Native storage permits22; installed content
    //defines17.
    saved.sent.resize(std::max(saved.sent.size(), spam.entries.size()));
    for (std::size_t i = 0; i < spam.entries.size(); ++i) {
        saved.sent[i] = spam.entries[i].sent;
    }
    saved.special_text = spam.special_text_flag;
    saved.year = std::bit_cast<std::int32_t>(spam.year);
    saved.elapsed_days = spam.elapsed_days;
    saved.sends_this_year = spam.sends_this_year;
    return result;
}
} //namespace simcoaster
