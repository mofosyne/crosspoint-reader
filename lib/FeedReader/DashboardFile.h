#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace feed {

// /feeds/dashboard.txt, one setting per line ('#' starts a comment):
//   weather -37.81 144.96 name=Melbourne days=4     forecast for a place (the default: Melbourne)
//   weather off                                     no forecast
//   calendar https://example.com/me.ics             an iCal feed for the events list (up to 5)
//   calendar https://.../holidays.ics region=Victoria   only the public holidays that apply to a region
struct CalendarRef {
  std::string url;
  std::string region;  // empty: every event; else only the public holidays of this region
};

struct DashboardConfig {
  bool weather = true;
  double latitude = -37.81;
  double longitude = 144.96;
  std::string name = "Melbourne";
  int days = 4;                        // 1 to 7
  std::vector<CalendarRef> calendars;  // iCal feeds, fetched by a sync
  std::vector<std::string> errors;     // lines that could not be understood
};

DashboardConfig parseDashboard(std::string_view text);

}  // namespace feed
