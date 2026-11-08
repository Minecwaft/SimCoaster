#pragma once

namespace simcoaster {
struct WorldRuntime;
struct CalendarReportFields;

namespace calendar_ui {
void refresh_calendar_date_label(WorldRuntime&, CalendarReportFields);
void refresh_park_statistics_month(WorldRuntime&);
void refresh_financial_statistics_month(WorldRuntime&);
void refresh_outstanding_loans_month(WorldRuntime&);
} //namespace calendar_ui
} //namespace simcoaster
