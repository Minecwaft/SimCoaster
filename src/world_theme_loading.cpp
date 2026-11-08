#include "simcoaster/world_theme.hpp"
#include "simcoaster/sam_integer.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace simcoaster {
namespace {
std::uint32_t ordinal_for_source(std::string_view name) {
    const auto same_letter = [](unsigned char left, unsigned char right) {
        const auto lower = [](unsigned char letter) {
            return letter >= 'A' && letter <= 'Z' ? static_cast<unsigned char>(letter + ('a' - 'A')) : letter;
        };
        return lower(left) == lower(right);
    };
    //Native0x74FC80 compares the first four names without case sensitivity;
    //the last slot is the fallback for every other source name.
    for (std::uint32_t ordinal = 0; ordinal + 1 < theme_source_count; ++ordinal)
        if (std::ranges::equal(name, theme_global_source_names[ordinal], same_letter))
            return ordinal;
    return theme_source_count - 1;
}
} //namespace

bool load_theme_global_balance_data(MissionWorld& c, ThemeGlobalSaveInfo* info) {
    if (!info)
        throw std::invalid_argument("null theme info");
    if (info->global_balance_fields)
        return true;
    //Native0x750A27 publishes the receiver before file I/O. A reentrant load
    //sees that receiver and returns immediately, even before parsing finishes.
    info->global_balance_fields.emplace();
    try {
        const auto path = std::string{ "data/levels/" } + info->source_name + "/global.sam";
        const auto text = c.ports.assets.read_balance_asset(c, path);
        //Native0x419B5E returns success for a zero-byte file before parser/reset.
        if (text && (text->empty() || sam::parse_integer_record(*info->global_balance_fields, *text)))
            return true;
    } catch (...) {
        info->global_balance_fields.reset();
        throw;
    }
    info->global_balance_fields.reset();
    return false;
}

ThemeGlobalSaveInfo*
initialize_theme_global_save_info(MissionWorld& c, ThemeGlobalSaveInfo* info, std::string_view source_name) {
    if (!info)
        throw std::invalid_argument("null theme info");
    info->global_balance_fields.reset();
    info->opaque_bytes.fill(0);
    info->metadata_valid.fill(0);
    info->award_winner_flags.fill(0);
    info->source_name.assign(source_name);
    info->source_ordinal = ordinal_for_source(info->source_name);
    info->name_changed = 0;
    info->all_research_completed = 0;
    info->sign_name_a[0] = 0;
    info->sign_name_b[0] = 0;
    const bool loaded = load_theme_global_balance_data(c, info);
    info->is_open = 0;
    //The original dereferences a null receiver on this failure path. Make the
    //failed construction explicit before the caller publishes a map entry.
    if (!loaded)
        throw std::runtime_error("initial theme global.sam load failed");
    info->is_available = info->global_balance_fields->available_from_start == 1;
    info->challenges_done = 0;
    info->all_challenges_done = 0;
    info->current_tech_level = 0;
    //Metadata values, award scores and both sign-name tails retain supplied
    //initialized values. New runtime records are value-initialized by callers.
    return info;
}

ThemeGlobalSaveInfo*
get_or_create_theme_global_info_for_ordinal(MissionWorld& c, ThemeGlobalInfoManager& manager, std::uint32_t ordinal) {
    return get_or_create_theme_global_info_for_source(c, manager, theme_global_source_names.at(ordinal));
}

ThemeGlobalSaveInfo*
get_or_create_theme_global_info_for_source(MissionWorld& c, ThemeGlobalInfoManager& manager, std::string_view source) {
    if (auto entry = manager.theme_info_records.find(source); entry != manager.theme_info_records.end())
        return load_theme_global_balance_data(c, &entry->second) ? &entry->second : nullptr;

    ThemeGlobalSaveInfo created{};
    initialize_theme_global_save_info(c, &created, source);
    auto [entry, inserted] = manager.theme_info_records.try_emplace(std::string{ source }, std::move(created));
    //The map owns the returned record. If file I/O reentrantly published the
    //same key, reuse that owned entry rather than leaking a second allocation.
    (void)inserted;
    return load_theme_global_balance_data(c, &entry->second) ? &entry->second : nullptr;
}
} //namespace simcoaster
