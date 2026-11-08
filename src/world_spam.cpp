#include "simcoaster/numeric_word.hpp"
#include "simcoaster/calendar_runtime.hpp"
#include "simcoaster/world.hpp"
#include "simcoaster/world_fixed_point.hpp"
#include "simcoaster/world_research.hpp"
#include "simcoaster/world_theme.hpp"
#include <algorithm>
#include <limits>

namespace simcoaster {
namespace {
constexpr std::array categories { SpamCategory::joke, SpamCategory::instruction, SpamCategory::controversy, SpamCategory::urban_legend, SpamCategory::brain_teaser };

std::int32_t divide(std::int32_t a, std::int32_t b) {
    return numeric::quotient(a, b, "invalid spam schedule divisor");
}

std::int32_t remainder(std::int32_t a, std::int32_t b) {
    if (!b || (a == std::numeric_limits<std::int32_t>::min() && b == -1)) {
        throw std::domain_error("invalid spam schedule modulus");
    }
    return a % b;
}

void weights(EconomyWorld& c) {
    const auto& p = c.finance.spam_balance;
    auto& s = c.finance.spam_scheduler;
    const std::array values { p.JokeWeighting, p.StupidInstructionWeighting, p.DoorsControversyWeighting, p.UrbanLegendWeighting, p.BrainTeaserWeighting };
    std::int32_t total = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        s.cumulative_weights[i] = total = fixed::add(total, values[i]);
    }
    s.total_weight = total;
}

std::int32_t first_entry(const SpamSchedulerState& s, SpamCategory category) {
    for (std::size_t i = 0; i < s.entries.size(); ++i) {
        if (s.entries[i].category == category) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}
} //namespace

SpamSchedulerState::SpamSchedulerState()
    : entries([]() consteval {
          std::array<SpamScheduleEntry, 17> result {};
          std::size_t out = 0;
          SpamCategory category {};
          for (int source = 0; source < 22; ++source) {
              match (source) {
                  case 0 => {
                      category = SpamCategory::joke;
                      continue;
                  }
                  case 9  => category = SpamCategory::joke_response;
                  case 10 => {
                      category = SpamCategory::instruction;
                      continue;
                  }
                  case 15 => {
                      category = SpamCategory::controversy;
                      continue;
                  }
                  case 20 => {
                      category = SpamCategory::urban_legend;
                      continue;
                  }
                  case 21 => {
                      category = SpamCategory::brain_teaser;
                      continue;
                  }
                  case _ => {
                  }
              } result.at(out++) = { source + 114, category, false };
          }
          return result;
      }()) {}

void initialize_spam_schedule(EconomyWorld& c) {
    auto& s = c.finance.spam_scheduler;
    const auto& p = c.finance.spam_balance;
    s.initialization_attempted = true;
    if (s.shutdown_disabled) {
        return;
    }
    if (!c.advisor.presentation.mode_initialized) {
        c.advisor.presentation.mode_initialized = true;
        initialize_game_mode_selector(c);
    }
    if (c.mode.game == GameMode::code_3 || p.SpamEmailsPerYear <= 0 || (p.TreatJokeResponseDifferently && p.MinDaysBeforeJokeResponse >= p.MaxDaysBeforeJokeResponse)) {
        return;
    }
    s.regular_interval = divide(fixed::subtract(s.annual_days, numeric::product(2, s.first_day)), p.SpamEmailsPerYear);
    s.special_interval = fixed::add(fixed::subtract(p.MaxDaysBeforeJokeResponse, p.MinDaysBeforeJokeResponse), 1);
    weights(c);
    if (s.total_weight > 0) {
        s.enabled = true;
    }
}

std::int32_t disable_spam_category(EconomyWorld& c, SpamCategory category) {
    auto& p = c.finance.spam_balance;
    auto& s = c.finance.spam_scheduler;
    if (category == SpamCategory::instruction || category == SpamCategory::controversy) {
        int result = -1;
        for (auto& entry : s.entries) {
            if (entry.category == category) {
                entry.sent = false;
                result = 0;
            }
        }
        return result;
    }
    match (category) {
        case SpamCategory::joke         => p.JokeWeighting = 0;
        case SpamCategory::urban_legend => p.UrbanLegendWeighting = 0;
        case SpamCategory::brain_teaser => p.BrainTeaserWeighting = 0;
        case _                          => return -1;
    } weights(c);
    return -1;
}

std::int32_t select_unsent_spam_entry(EconomyWorld& c, SpamCategory category) {
    auto& s = c.finance.spam_scheduler;
    for (;;) {
        const auto count = static_cast<std::int32_t>(std::ranges::count_if(s.entries, [&](const auto& entry) {
            return entry.category == category && !entry.sent;
        }));
        if (!count) {
            const auto reset = disable_spam_category(c, category);
            if ((category == SpamCategory::instruction || category == SpamCategory::controversy) && reset >= 0) {
                continue;
            }
            return -1;
        }
        auto selected = next_crt_random(c) % count;
        for (std::size_t i = 0; i < s.entries.size(); ++i) {
            if (s.entries[i].category == category && !s.entries[i].sent && !selected--) {
                return static_cast<std::int32_t>(i);
            }
        }
        return -2;
    }
}

std::int32_t choose_next_spam_entry(EconomyWorld& c) {
    auto& s = c.finance.spam_scheduler;
    auto previous = s.next_send_day ? s.entries.at(static_cast<std::size_t>(s.previous_entry)).category : static_cast<SpamCategory>(7);
    for (;;) {
        SpamCategory category;
        bool response = false;
        if (previous == SpamCategory::joke) {
            previous = static_cast<SpamCategory>(7);
            response = next_crt_random(c) % 4 == 0;
        }
        if (response) {
            category = SpamCategory::joke_response;
        } else {
            const auto roll = remainder(next_crt_random(c), s.total_weight);
            const auto found = std::ranges::find_if(s.cumulative_weights, [&](auto upper) {
                return roll < upper;
            });
            if (found == s.cumulative_weights.end()) {
                return -2;
            }
            category = categories[static_cast<std::size_t>(found - s.cumulative_weights.begin())];
        }
        auto selected = first_entry(s, category);
        if (category != SpamCategory::joke_response && (category != SpamCategory::controversy || selected < 0 || s.entries[static_cast<std::size_t>(selected)].sent)) {
            selected = select_unsent_spam_entry(c, category);
        }
        if (selected >= 0) {
            return selected;
        }
        if (std::ranges::none_of(s.entries, [](const auto& entry) {
                return !entry.sent && entry.category != SpamCategory::joke_response;
            })) {
            return -2;
        }
    }
}

void configure_spam_interval(EconomyWorld& c, std::int32_t days) {
    auto& s = c.finance.spam_scheduler;
    s.regular_interval = s.special_interval = days;
    s.schedule_year = fixed::word(s.year);
    s.next_send_day = s.elapsed_days;
    s.sends_this_year = 0;
    c.finance.spam_balance.SpamEmailsPerYear = s.annual_days;
}

void world_operations::builtin::advance_spam_scheduler_year(WorldRuntime& c) {
    auto& s = c.finance.spam_scheduler;
    ++s.year;
    s.elapsed_days = s.sends_this_year = 0;
}

void world_operations::builtin::process_spam_scheduler_day(WorldRuntime& c) {
    auto& s = c.finance.spam_scheduler;
    const auto& p = c.finance.spam_balance;
    if (s.manual_send) {
        const auto& entry = s.entries[static_cast<std::size_t>(next_crt_random(c)) % s.entries.size()];
        dispatch_spam_email(c, entry.resource, entry.category);
    }
    s.elapsed_days = fixed::add(s.elapsed_days, 1);
    if (!s.enabled) {
        if (!s.initialization_attempted && s.year >= static_cast<std::uint32_t>(p.YearsBeforeStarting)) {
            initialize_spam_schedule(c);
        }
        return;
    }
    if (s.year < static_cast<std::uint32_t>(s.schedule_year) || s.elapsed_days < s.next_send_day) {
        return;
    }
    if (s.next_send_day) {
        auto& entry = s.entries.at(static_cast<std::size_t>(s.current_entry));
        dispatch_spam_email(c, entry.resource, entry.category);
        entry.sent = true;
        s.sends_this_year = fixed::add(s.sends_this_year, 1);
        s.previous_entry = s.current_entry;
    }
    s.current_entry = choose_next_spam_entry(c);
    if (s.current_entry < 0 || s.current_entry > static_cast<int>(s.entries.size())) {
        s.enabled = false;
        return;
    }
    if (s.entries.at(static_cast<std::size_t>(s.current_entry)).category == SpamCategory::joke_response && p.TreatJokeResponseDifferently) {
        s.next_send_day = fixed::add(remainder(next_crt_random(c), s.special_interval), s.elapsed_days);
        if (s.next_send_day > fixed::subtract(s.annual_days, s.first_day)) {
            s.next_send_day = fixed::add(s.first_day, remainder(s.next_send_day, s.annual_days));
            s.schedule_year = fixed::add(s.schedule_year, 1);
        }
    } else if (s.sends_this_year < p.SpamEmailsPerYear) {
        s.next_send_day = fixed::add(fixed::add(s.first_day, numeric::product(s.regular_interval, s.sends_this_year)), remainder(next_crt_random(c), s.regular_interval));
    } else {
        s.schedule_year = fixed::word(s.year + 1u);
        s.next_send_day = fixed::add(s.first_day, remainder(next_crt_random(c), s.regular_interval));
    }
}
} //namespace simcoaster
