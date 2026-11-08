#include "simcoaster/world_calendar.hpp"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

namespace simcoaster {
void ClockPort::get_local_time(SystemTimeFields* out) {
    SYSTEMTIME native_time;
    GetLocalTime(&native_time);
    *out = { native_time.wYear, native_time.wMonth,  native_time.wDayOfWeek, native_time.wDay,
             native_time.wHour, native_time.wMinute, native_time.wSecond,    native_time.wMilliseconds };
}

std::int32_t* convert_system_time_fields_to_filetime(FileTime* out,
                                                     std::int32_t* status,
                                                     std::uint16_t year,
                                                     std::uint16_t month,
                                                     std::uint16_t day,
                                                     std::uint16_t hour,
                                                     std::uint16_t minute,
                                                     std::uint16_t second,
                                                     std::uint16_t millisecond) {
    const auto saved = *out;
    //Native leaves wDayOfWeek uninitialized; SystemTimeToFileTime ignores it.
    const SYSTEMTIME parts{ year, month, 0, day, hour, minute, second, millisecond };
    FILETIME converted{ out->low, out->high };
    if (SystemTimeToFileTime(&parts, &converted) && converted.dwHighDateTime < 2147483648u) {
        *out = { converted.dwLowDateTime, converted.dwHighDateTime };
        *status = 0;
    } else {
        *out = saved;
        *status = -1;
    }
    return status;
}

std::int32_t* convert_filetime_to_system_time_fields(const FileTime* time,
                                                     std::int32_t* status,
                                                     std::uint32_t* year,
                                                     std::uint32_t* month,
                                                     std::uint32_t* day,
                                                     std::uint32_t* weekday,
                                                     std::uint32_t* hour,
                                                     std::uint32_t* minute,
                                                     std::uint32_t* second,
                                                     std::uint32_t* millisecond) {
    const FILETIME native_time{ time->low, time->high };
    SYSTEMTIME parts;
    if (!FileTimeToSystemTime(&native_time, &parts)) {
        *status = -1;
        return status;
    }
    if (year)
        *year = parts.wYear;
    if (month)
        *month = parts.wMonth;
    if (day)
        *day = parts.wDay;
    if (weekday)
        *weekday = parts.wDayOfWeek;
    if (hour)
        *hour = parts.wHour;
    if (minute)
        *minute = parts.wMinute;
    if (second)
        *second = parts.wSecond;
    if (millisecond)
        *millisecond = parts.wMilliseconds;
    *status = 0;
    return status;
}
} //namespace simcoaster
