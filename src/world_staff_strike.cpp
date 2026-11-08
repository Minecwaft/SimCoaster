#include "world_staff_internal.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_mail.hpp"

namespace simcoaster {
using namespace staff_detail;

StaffStrikeCause evaluate_world_staff_strike_thresholds(StaffWorld& c, const StaffHqRecord& control, StaffType role) {
    if (control.force_strike)
        return StaffStrikeCause::forced;
    if (static_cast<std::uint32_t>(role) >= 6u)
        return StaffStrikeCause::none;
    const auto& b = c.staff.recruitment.balance.Strike;
    const auto below = [&](float StaffRuntimeRecord::* member, std::int32_t limit) {
        std::uint32_t count = 0, sum = 0;
        for (auto id = head(world(c), role); id;) {
            const auto& s = record<StaffRuntimeRecord>(c, id);
            ++count;
            sum += byte(s.*member);
            id = s.next_staff;
        }
        if (count < static_cast<std::uint32_t>(b.MinStaffOfTypeForStrike))
            return false;
        if (!count)
            throw std::domain_error("zero staff strike mean divisor");
        return sum / count <= static_cast<std::uint32_t>(limit);
    };
    if (below(&StaffRuntimeRecord::energy, b.EnergyPercentToStrike))
        return StaffStrikeCause::energy;
    if (below(&StaffRuntimeRecord::happiness_score, b.HappyPercentToStrike))
        return StaffStrikeCause::happiness;
    if (below(&StaffRuntimeRecord::motivation, b.MotivationPercentToStrike))
        return StaffStrikeCause::motivation;
    return StaffStrikeCause::none;
}

void update_world_staff_strike_state(StaffWorld& c, StaffHqRecord& control, StaffType role) {
    const auto elapsed =
        std::bit_cast<std::int64_t>(scale_world_ticks_to_filetime_delta(primary(c).calendar, world(c).saved.game_tick));
    if (fixed::narrow(elapsed / 25920000000000LL) < c.staff.recruitment.balance.Strike.MinMonthsForStrike)
        return;
    const auto cause = evaluate_world_staff_strike_thresholds(c, control, role);
    auto& entry = control.strikes.at(static_cast<std::size_t>(role));
    const auto clear_mail = [&] {
        constexpr std::array<std::int32_t, 6> keys{ 1, 0, 3, 2, 4, 5 };
        (void)mark_tag_messages_by_primary_key_for_deletion(c,
                                                            &record<TagSystemRecord>(c, primary(c).saved.tag_system),
                                                            keys.at(static_cast<std::size_t>(role)));
    };
    if (cause != StaffStrikeCause::none) {
        match (entry.stage) {
            case StaffStrikeStage::none => {
                entry.stage = StaffStrikeStage::warning_issued;
                notify(c, WorldEvent{ StaffStrikeEvent{ role, 2, std::to_underlying(cause) } });
            }
            case StaffStrikeStage::warning_issued or StaffStrikeStage::one_week or StaffStrikeStage::two_weeks => {
                entry.stage = static_cast<StaffStrikeStage>(std::to_underlying(entry.stage) + 1u);
                entry.strike_active = 1;
                clear_mail();
                notify(c, WorldEvent{ StaffStrikeEvent{ role, 2, 0 } });
            }
            case StaffStrikeStage::one_month => {
                entry.stage = StaffStrikeStage::one_week;
                entry.strike_active = 1;
                clear_mail();
            }
            case _ => ;
        }
    } else {
        if (std::to_underlying(entry.stage) >= std::to_underlying(StaffStrikeStage::warning_issued)) {
            clear_mail();
            notify(c, WorldEvent{ StaffStrikeEvent{ role, 3, 0 } });
        }
        entry.stage = StaffStrikeStage::none;
        entry.strike_active = 0;
    }
}

void update_world_staff_strike_states_for_game_tick(StaffWorld& c, StaffHqRecord& control) {
    for (std::uint32_t index = 0; index < 6; ++index) {
        const auto role = static_cast<StaffType>(index);
        if (!head(world(c), role))
            continue;
        auto& entry = control.strikes[index];
        const auto tick = world(c).saved.game_tick;
        if (entry.last_processed_game_tick == tick)
            continue;
        entry.last_processed_game_tick = tick;
        if (entry.strike_active) {
            entry.strike_active = 0;
            notify(c, WorldEvent{ StaffStrikeEvent{ role, 5, 0 } });
        } else
            update_world_staff_strike_state(c, control, role);
    }
}
} //namespace simcoaster
