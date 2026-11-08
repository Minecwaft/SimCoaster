#include "simcoaster/numeric_word.hpp"
#include "simcoaster/world_calendar.hpp"
#include "simcoaster/world_events.hpp"
#include <bit>
#include <stdexcept>

namespace simcoaster {
namespace {
FileTime current_filetime(EconomyWorld& c, const CalendarState* calendar) {
    //Zero-extended factors, signed64 division, then low64 multiplication/addition.
    const auto delta = scale_world_ticks_to_filetime_delta(*calendar, c.simulation.current->saved.game_tick);
    const auto base =
        std::uint64_t{ calendar->funny_time_start.low } | (std::uint64_t{ calendar->funny_time_start.high } << 32);
    const auto time = base + delta;
    return { static_cast<std::uint32_t>(time), static_cast<std::uint32_t>(time >> 32) };
}

void notify(EconomyWorld& c, WorldEvent* event) {
    dispatch_world_event_to_subscribers(c,
                                        get_world_event_subscription_registry(c),
                                        event,
                                        deliver_world_record_event,
                                        nullptr);
}
} //namespace

std::uint64_t scale_world_ticks_to_filetime_delta(const CalendarState& calendar, std::uint32_t elapsed) {
    const auto product = std::uint64_t{ elapsed } * calendar.funny_seconds_per_real_second;
    return static_cast<std::uint64_t>(std::bit_cast<std::int64_t>(product) / 4) * 10000000u;
}

//Game.exe6323856. This constructor does not synchronize saved year/day markers.
CalendarState* initialize_world_calendar_state(EconomyWorld& c, CalendarState* calendar) {
    calendar->funny_time_start = { 0, 0 };
    calendar->session_start = { 0, 0 };
    calendar->funny_seconds_per_real_second = 15000;
    std::int32_t status;
    convert_system_time_fields_to_filetime(&calendar->funny_time_start, &status, 2000, 1, 1, 0, 0, 0, 0);
    calendar->month_at_last_update = 0;
    calendar->day_at_last_update = 4294967295u;
    calendar->year_at_last_update = 0;
    SystemTimeFields local_time;
    c.ports.clock.get_local_time(&local_time);
    //Local wall-clock fields go straight into SystemTimeToFileTime. There is no
    //local-to-UTC adjustment. Failed session conversion leaves its cleared value.
    convert_system_time_fields_to_filetime(&calendar->session_start,
                                           &status,
                                           local_time.year,
                                           local_time.month,
                                           local_time.day,
                                           local_time.hour,
                                           local_time.minute,
                                           local_time.second,
                                           local_time.millisecond);
    return calendar;
}

FileTime* calculate_world_calendar_filetime(EconomyWorld& c,
                                            CalendarState* calendar,
                                            FileTime* out,
                                            std::uint32_t* year,
                                            std::uint32_t* month,
                                            std::uint32_t* day,
                                            std::uint32_t* weekday,
                                            std::uint32_t* hour,
                                            std::uint32_t* minute,
                                            std::uint32_t* second,
                                            std::uint32_t* millisecond) {
    const auto time = current_filetime(c, calendar);
    std::int32_t status;
    convert_filetime_to_system_time_fields(&time,
                                           &status,
                                           year,
                                           month,
                                           day,
                                           weekday,
                                           hour,
                                           minute,
                                           second,
                                           millisecond);
    *out = time;
    return out; //Conversion status is deliberately not propagated.
}

//Game.exe6325200. Shared report fields are live across every callback.
std::uint32_t process_world_calendar_changes(EconomyWorld& c, CalendarState* calendar) {
    auto time = current_filetime(c, calendar);
    std::int32_t status;
    auto& report = c.finance.calendar_report_fields;
    convert_filetime_to_system_time_fields(&time,
                                           &status,
                                           reinterpret_cast<std::uint32_t*>(&report.year_since_1900),
                                           reinterpret_cast<std::uint32_t*>(&report.zero_based_month),
                                           reinterpret_cast<std::uint32_t*>(&report.day_of_month),
                                           reinterpret_cast<std::uint32_t*>(&report.day_of_week),
                                           reinterpret_cast<std::uint32_t*>(&report.hour),
                                           reinterpret_cast<std::uint32_t*>(&report.minute),
                                           reinterpret_cast<std::uint32_t*>(&report.second),
                                           nullptr);
    report.year_since_1900 = std::bit_cast<std::int32_t>(numeric::bits(report.year_since_1900) - 1900u);
    report.zero_based_month = std::bit_cast<std::int32_t>(numeric::bits(report.zero_based_month) - 1u);
    if (numeric::bits(report.day_of_month) != calendar->day_at_last_update) {
        {
            WorldEvent event{ DayChangedEvent{} };
            notify(c, &event);
        }
        //Snapshot AFTER subscribers, passed by value. Later callbacks can mutate
        //the shared report independently of these nine copied DWORDs.
        c.ports.ui.refresh_calendar_date_label(c, report);
        world_operations::process_spam_scheduler_day(c);
    }
    if (numeric::bits(report.zero_based_month) != calendar->month_at_last_update) {
        {
            WorldEvent event{ MonthChangedEvent{} };
            notify(c, &event);
        }
        c.ports.ui.refresh_park_statistics_month(c);
        c.ports.ui.refresh_financial_statistics_month(c);
        c.ports.ui.refresh_outstanding_loans_month(c);
        world_operations::mark_shares_panel_refresh(c);
    }
    //Reload tick, time base and scale after day/month callbacks. The original
    //does not reuse the first conversion and does not refresh the report block.
    time = current_filetime(c, calendar);
    std::uint32_t year;
    convert_filetime_to_system_time_fields(&time,
                                           &status,
                                           &year,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr);
    if (status)
        throw std::runtime_error("calendar: native second conversion leaves an indeterminate stack year");
    if (year != calendar->year_at_last_update) {
        {
            WorldEvent event{ YearChangedEvent{} };
            notify(c, &event);
        }
        const auto source = c.ports.assets.selected_resource_source(c);
        world_operations::send_annual_shares_report(c, calendar->year_at_last_update, source, 12);
        world_operations::advance_spam_scheduler_year(c);
    }
    calendar->month_at_last_update = numeric::bits(report.zero_based_month);
    calendar->year_at_last_update = year;
    calendar->day_at_last_update = numeric::bits(report.day_of_month);
    return calendar->day_at_last_update;
}

std::int64_t count_world_calendar_elapsed_365_day_periods(EconomyWorld& c, const CalendarState* calendar) {
    const auto product =
        std::uint64_t{ c.simulation.current->saved.game_tick } * calendar->funny_seconds_per_real_second;
    const auto delta = static_cast<std::uint64_t>(std::bit_cast<std::int64_t>(product) / 4) * 10000000u;
    return std::bit_cast<std::int64_t>(delta) / 315360000000000LL;
}

//Game.exe6326368: no events, report-block writes or day-marker adjustment.
void synchronize_world_calendar_month_year(EconomyWorld& c, CalendarState* calendar) {
    auto time = current_filetime(c, calendar);
    std::int32_t status;
    std::uint32_t month;
    convert_filetime_to_system_time_fields(&time,
                                           &status,
                                           nullptr,
                                           &month,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr);
    if (status)
        throw std::runtime_error("calendar: native month synchronization leaves an indeterminate stack month");
    calendar->month_at_last_update = month - 1;
    time = current_filetime(c, calendar);
    convert_filetime_to_system_time_fields(&time,
                                           &status,
                                           &calendar->year_at_last_update,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr);
}
} //namespace simcoaster
