#pragma once
#include "world_domains_fwd.hpp"
#include "world.hpp"

namespace simcoaster {
//Native helpers consume only the low WORD of each four-byte stack argument.
//Failed conversion restores the previous FILETIME, then stores status-1.
std::int32_t* convert_system_time_fields_to_filetime(FileTime*,
                                                     std::int32_t* status,
                                                     std::uint16_t year,
                                                     std::uint16_t month,
                                                     std::uint16_t day,
                                                     std::uint16_t hour,
                                                     std::uint16_t minute,
                                                     std::uint16_t second,
                                                     std::uint16_t millisecond);
CalendarState* initialize_world_calendar_state(EconomyWorld&, CalendarState*);
std::uint64_t scale_world_ticks_to_filetime_delta(const CalendarState&, std::uint32_t elapsed_world_ticks);

//Game.exe8229344: status0 on success/-1 on failure; failure leaves every
//component output untouched. The native OS conversion remains the dependency.
std::int32_t* convert_filetime_to_system_time_fields(const FileTime*,
                                                     std::int32_t* status,
                                                     std::uint32_t* year,
                                                     std::uint32_t* month,
                                                     std::uint32_t* day,
                                                     std::uint32_t* weekday,
                                                     std::uint32_t* hour,
                                                     std::uint32_t* minute,
                                                     std::uint32_t* second,
                                                     std::uint32_t* millisecond);
FileTime* calculate_world_calendar_filetime(EconomyWorld&,
                                            CalendarState*,
                                            FileTime*,
                                            std::uint32_t* year = nullptr,
                                            std::uint32_t* month = nullptr,
                                            std::uint32_t* day = nullptr,
                                            std::uint32_t* weekday = nullptr,
                                            std::uint32_t* hour = nullptr,
                                            std::uint32_t* minute = nullptr,
                                            std::uint32_t* second = nullptr,
                                            std::uint32_t* millisecond = nullptr);
//Successful second conversion is required. Native otherwise compares an
//uninitialized stack year; that indeterminate case explicitly throws here.
std::uint32_t process_world_calendar_changes(EconomyWorld&, CalendarState*);
void synchronize_world_calendar_month_year(EconomyWorld&, CalendarState*);
std::int64_t count_world_calendar_elapsed_365_day_periods(EconomyWorld&, const CalendarState*);
} //namespace simcoaster
