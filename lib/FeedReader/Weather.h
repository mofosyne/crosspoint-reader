#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace feed {

// A small forecast from the Open-Meteo API (https://open-meteo.com, free, no key).
struct WeatherDay {
  std::string date;  // "2026-11-08"
  int code = 0;      // WMO weather code
  int minC = 0;
  int maxC = 0;
  int rainPercent = 0;
};

struct Weather {
  bool hasNow = false;
  int nowC = 0;
  int nowCode = 0;
  std::string nowTime;  // "2026-11-08T20:00" local time of the reading
  std::vector<WeatherDay> days;
};

// Request for the current reading and `days` daily forecasts at a location, in its own time zone.
std::string openMeteoUrl(double latitude, double longitude, int days);

// Reads the JSON answer. False when it holds no daily forecast.
bool parseOpenMeteo(std::string_view json, Weather& out);

const char* weatherText(int wmoCode);  // "Light rain"
// "Sun".."Sat" for a "YYYY-MM-DD" date; empty when the date is malformed.
std::string weekdayName(std::string_view date);

}  // namespace feed
