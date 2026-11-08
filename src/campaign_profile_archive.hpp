#pragma once
#include "simcoaster/campaign_profile.hpp"
#include <cmath>
#include <limits>

namespace simcoaster::campaign_detail {
struct Failure {
    std::size_t offset;
    std::string reason;
};

struct Reader {
    static constexpr bool reading = true;
    std::span<const std::byte> bytes;
    std::size_t position {};

    [[noreturn]] void fail(std::string reason) const {
        throw Failure { position, std::move(reason) };
    }

    std::span<const std::byte> take(std::size_t count) {
        if (count > bytes.size() - position) {
            fail("Truncated campaign profile");
        }
        const auto result = bytes.subspan(position, count);
        position += count;
        return result;
    }

    template <class T>
    void value(T& out) {
        constexpr auto width = save::value_width<T>;
        static_assert(width > 0);
        const auto raw = take(width);
        save::detail::decode(std::span<const std::byte, width> { raw.data(), width }, out);
    }

    template <class... T>
    void operator()(T&... out) {
        (value(out), ...);
    }

    void marker(std::string_view expected) {
        const auto start = position;
        if (!std::ranges::equal(take(expected.size()), std::as_bytes(std::span { expected.data(), expected.size() }))) {
            throw Failure { start, "Expected campaign section " + std::string { expected } };
        }
    }

    std::size_t count(std::int32_t count, std::size_t width, std::size_t capacity = std::numeric_limits<std::size_t>::max()) {
        if (count < 0 || static_cast<std::size_t>(count) > capacity || static_cast<std::size_t>(count) > (bytes.size() - position) / width) {
            fail("Invalid campaign record count");
        }
        return static_cast<std::size_t>(count);
    }

    std::string key() {
        std::uint32_t length {};
        value(length);
        const auto raw = take(length);
        std::string result { reinterpret_cast<const char*>(raw.data()), raw.size() };
        if (result.find('\0') != result.npos) {
            fail("Embedded NUL in campaign source key");
        }
        return result;
    }
};

struct Writer {
    static constexpr bool reading = false;
    std::vector<std::byte> bytes;

    [[noreturn]] void fail(std::string reason) const {
        throw Failure { bytes.size(), std::move(reason) };
    }

    template <class T>
    void value(const T& in) {
        const auto raw = save::detail::encode(in);
        bytes.insert(bytes.end(), raw.begin(), raw.end());
    }

    template <class... T>
    void operator()(const T&... in) {
        (value(in), ...);
    }

    void marker(std::string_view expected) {
        const auto raw = std::as_bytes(std::span { expected.data(), expected.size() });
        bytes.insert(bytes.end(), raw.begin(), raw.end());
    }

    void key(std::string_view text) {
        if (text.size() > std::numeric_limits<std::uint32_t>::max() || text.find('\0') != text.npos) {
            fail("Invalid campaign source key");
        }
        value(static_cast<std::uint32_t>(text.size()));
        marker(text);
    }

    void count(std::size_t count) {
        if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            fail("Too many campaign records");
        }
        value(static_cast<std::int32_t>(count));
    }
};

template <class A, class T>
void cdk(A& a, T& v) {
    a(v.object_id, v.theme_category, v.exclude_from_coaster_source_picker, v.source_picker_amount);
}

template <class A, class T>
void objective(A& a, T& v) {
    a(v.state, v.objective_kind, v.target_value, v.deadline_days, v.start_filetime_low, v.start_filetime_high, v.current_value, v.elapsed_days, v.source_filter_ordinal, v.percentage_baseline_value, v.opaque_tail);
}

template <class A, class T>
void theme(A& a, T& v) {
    a(v.opaque_bytes);
    for (std::size_t i = 0; i < v.metadata_valid.size(); ++i) {
        a(v.metadata_valid[i], v.metadata_values[i]);
    }
    for (std::size_t i = 0; i < v.award_winner_flags.size(); ++i) {
        a(v.award_winner_flags[i], v.award_scores[i]);
    }
    for (std::size_t i = 0; i < v.sign_name_a.size(); ++i) {
        a(v.sign_name_a[i], v.sign_name_b[i]);
    }
    a(v.is_available, v.is_open, v.source_ordinal, v.name_changed, v.all_research_completed, v.challenges_done, v.all_challenges_done, v.current_tech_level);
}

template <class A, class T>
void history(A& a, T& v) {
    a(v.last_played_world_tick, v.last_response_variant_index, v.is_active, v.original_padding, v.cancel_count, v.play_count);
}

template <class A, class T>
void options(A& a, T& v) {
    for (auto* volume : { &v.sfx_volume, &v.music_volume, &v.speech_volume, &v.movie_volume }) {
        a(volume->enabled, volume->reserved, volume->value);
    }
    a(v.advisor_on, v.tutorial_on, v.tooltips_on, v.confirm_delete_on, v.rmb_scroll_on, v.rmb_cancel_on, v.isometric_on, v.animate_menus, v.game_speed_percent, v.totd, v.minimum_zoom, v.maximum_zoom, v.fps_clip, v.music_enabled);
    if (!std::isfinite(v.minimum_zoom) || !std::isfinite(v.maximum_zoom) || !std::isfinite(v.fps_clip)) {
        a.fail("Non-finite campaign camera/frame settings");
    }
}

template <class A, class T>
void report(A& a, T& v) {
    a(v.report_year, v.history_difference, v.history_sum, v.history_average, v.history_sum_a, v.history_sum_b);
}

template <class A, class T>
void prefix(A& a, T& m) {
    a(m.golden_ticket_balance, m.global_park_award_flags);
    for (auto& v : m.cdk_registrations) {
        cdk(a, v);
    }
    a(m.cdk_registration_count, m.cdk_registration_cursor, m.language_word_filters_enabled, m.manager_startup_action_pending, m.mission_world_event_latch, m.advisor_eligibility_flags, m.research_lab_action_532_eligible, m.mission_number, m.current_promotion_index, m.challenge_failures_remaining, m.theme_ability_activation_flags, m.mission_obstacle_research_complete, m.obstacle_removed_message_received);
    for (auto& v : m.objective_entries) {
        objective(a, v);
    }
    a.marker("TATS");
    a(m.lobby_area_available_flags);
    a.marker("AMTA");
    if constexpr (A::reading) {
        std::int32_t size {};
        a(size);
        const auto count = a.count(size, 4 + 215);
        for (std::size_t i = 0; i < count; ++i) {
            auto key = a.key();
            auto [entry, inserted] = m.theme_info_records.try_emplace(key);
            if (!inserted) {
                a.fail("Duplicate campaign theme source key");
            }
            entry->second.source_name = std::move(key);
            theme(a, entry->second);
        }
    } else {
        a.count(m.theme_info_records.size());
        for (auto& [key, value] : m.theme_info_records) {
            a.key(key);
            theme(a, value);
        }
    }
    a.marker("MEHT");
}

template <class A, class T>
void shares(A& a, T& s) {
    for (auto& slot : s.record_slots) {
        std::uint32_t present = slot ? 1u : 0u;
        a(present);
        ShareFinanceSavedRecord empty {};
        if constexpr (A::reading) {
            report(a, empty); //Absent slots still occupy the full36-byte record.
            if (present) {
                slot = std::make_unique<ShareFinanceSavedRecord>(empty);
            }
        } else if (slot) {
            report(a, *slot);
        } else {
            report(a, empty);
        }
    }
    if constexpr (A::reading) {
        s.ownership_context = std::make_unique<SharesOwnershipContext>();
        s.share_price_context = std::make_unique<SharePriceContext>();
    }
    if (!s.ownership_context || !s.share_price_context) {
        a.fail("Missing campaign shares contexts");
    }
    a(s.ownership_context->shares_owned, s.ownership_context->ownership_percentage);
    std::array<char16_t, 31> title {};
    if constexpr (!A::reading) {
        const auto& text = s.share_price_context->company_title;
        std::copy_n(text.begin(), std::min(text.size(), title.size()), title.begin());
    }
    a(title);
    if constexpr (A::reading) {
        s.share_price_context->company_title.assign(title.begin(), std::ranges::find(title, char16_t {}));
    }
    a(s.share_price_context->current_share_price);
    a.marker("RAHS");
}

template <class A, class P>
void profile(A& a, P& p) {
    if (!p.theme) {
        a.fail("Missing campaign manager");
    }
    if constexpr (A::reading) {
        a(p.selected_source, p.version);
        if (p.version < 12) {
            a.fail("Unsupported campaign profile version below12");
        }
    } else {
        a(p.selected_source, std::uint32_t { 12 });
    }
    auto& m = *p.theme;
    prefix(a, m);
    options(a, p.options);
    a(m.use_gmi_suffix, m.end_of_game_event_processed, p.first_hour_time);
    for (auto& v : m.history) {
        history(a, v);
    }
    a(p.auxiliary.tips.count, p.auxiliary.tips.seen);
    a.marker("DTOT");
    shares(a, p.shares);
    auto& spam = p.auxiliary.spam;
    if constexpr (A::reading) {
        std::int32_t count {};
        a(count);
        spam.sent.resize(a.count(count, 1, 22));
    } else {
        a.count(spam.sent.size());
    }
    for (auto& v : spam.sent) {
        a(v);
    }
    a(spam.special_text, spam.year, spam.elapsed_days, spam.sends_this_year);
    a.marker("MAPS");
    auto& ambient = p.auxiliary.ambient;
    if constexpr (A::reading) {
        std::int32_t count {};
        a(count);
        ambient.resize(a.count(count, 16));
    } else {
        a.count(ambient.size());
    }
    for (auto& v : ambient) {
        a(v.event_id, v.sound_handle, v.resource_source_index, v.legacy_link_word);
    }
}
} //namespace simcoaster::campaign_detail
