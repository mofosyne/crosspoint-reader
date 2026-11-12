#include "Weather.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace feed {
namespace {

// The text between the brackets of `"key":[...]` that follows `from`, empty when absent.
std::string_view arrayOf(std::string_view json, size_t from, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":[";
  const size_t at = json.find(needle, from);
  if (at == std::string_view::npos) return {};
  const size_t start = at + needle.size();
  const size_t end = json.find(']', start);
  if (end == std::string_view::npos) return {};
  return json.substr(start, end - start);
}

// The number after `"key":` following `from`.
bool numberOf(std::string_view json, size_t from, std::string_view key, double& out) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const size_t at = json.find(needle, from);
  if (at == std::string_view::npos) return false;
  const std::string text(json.substr(at + needle.size(), 16));
  char* end = nullptr;
  out = std::strtod(text.c_str(), &end);
  return end != text.c_str();
}

std::vector<std::string> items(std::string_view list) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= list.size() && !list.empty()) {
    size_t comma = list.find(',', pos);
    if (comma == std::string_view::npos) comma = list.size();
    std::string item(list.substr(pos, comma - pos));
    if (item.size() >= 2 && item.front() == '"') item = item.substr(1, item.size() - 2);
    out.push_back(std::move(item));
    pos = comma + 1;
  }
  return out;
}

int rounded(const std::string& text) { return static_cast<int>(std::lround(std::strtod(text.c_str(), nullptr))); }

}  // namespace

std::string openMeteoUrl(double latitude, double longitude, int days) {
  char buf[320];
  snprintf(buf, sizeof(buf),
           "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&current=temperature_2m,weather_code"
           "&hourly=temperature_2m&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_"
           "max,uv_index_max"
           "&timezone=auto&forecast_days=%d",
           latitude, longitude, days);
  return buf;
}

bool parseOpenMeteo(std::string_view json, Weather& out) {
  out = Weather{};
  const size_t current = json.find("\"current\":{");
  double value = 0;
  if (current != std::string_view::npos && numberOf(json, current, "temperature_2m", value)) {
    out.hasNow = true;
    out.nowC = static_cast<int>(std::lround(value));
    if (numberOf(json, current, "weather_code", value)) out.nowCode = static_cast<int>(value);
    const std::string needle = "\"time\":\"";
    const size_t t = json.find(needle, current);
    if (t != std::string_view::npos) {
      const size_t end = json.find('"', t + needle.size());
      if (end != std::string_view::npos)
        out.nowTime = std::string(json.substr(t + needle.size(), end - t - needle.size()));
    }
  }
  const size_t daily = json.find("\"daily\":{");
  if (daily == std::string_view::npos) return false;
  const auto dates = items(arrayOf(json, daily, "time"));
  const auto codes = items(arrayOf(json, daily, "weather_code"));
  const auto maxes = items(arrayOf(json, daily, "temperature_2m_max"));
  const auto mins = items(arrayOf(json, daily, "temperature_2m_min"));
  const auto rain = items(arrayOf(json, daily, "precipitation_probability_max"));
  const auto uv = items(arrayOf(json, daily, "uv_index_max"));
  for (size_t i = 0; i < dates.size(); ++i) {
    WeatherDay day;
    day.date = dates[i];
    if (i < codes.size()) day.code = rounded(codes[i]);
    if (i < maxes.size()) day.maxC = rounded(maxes[i]);
    if (i < mins.size()) day.minC = rounded(mins[i]);
    if (i < rain.size()) day.rainPercent = rounded(rain[i]);  // "null" reads as 0
    if (i < uv.size()) day.uv = rounded(uv[i]);
    out.days.push_back(std::move(day));
  }
  // Temperatures at 08:00, 12:00 and 16:00 of each day, from the hourly list.
  const size_t hourly = json.find("\"hourly\":{");
  if (hourly != std::string_view::npos) {
    const auto times = items(arrayOf(json, hourly, "time"));
    const auto temps = items(arrayOf(json, hourly, "temperature_2m"));
    for (size_t i = 0; i < times.size() && i < temps.size(); ++i) {
      if (times[i].size() < 13) continue;
      const std::string date = times[i].substr(0, 10);
      const int hour = std::atoi(times[i].substr(11, 2).c_str());
      for (auto& day : out.days) {
        if (day.date != date) continue;
        if (hour == 8) day.morningC = rounded(temps[i]), day.hasHours = true;
        if (hour == 12) day.middayC = rounded(temps[i]);
        if (hour == 16) day.afternoonC = rounded(temps[i]);
      }
    }
  }
  return !out.days.empty();
}

Clothing clothingFor(const WeatherDay& day) {
  Clothing c;
  c.outer = day.maxC < 10   ? Outer::WarmCoat
            : day.maxC < 15 ? Outer::Coat
            : day.maxC < 20 ? Outer::Jacket
            : day.maxC < 25 ? Outer::LightLayer
                            : Outer::TShirt;
  c.layers = day.minC < 15 && day.maxC >= 18 && day.maxC - day.minC >= 8;  // cold start, warm afternoon
  c.umbrella = day.rainPercent >= 40;
  c.sunscreen = day.uv >= 6;
  return c;
}

const char* outerName(Outer outer) {
  switch (outer) {
    case Outer::WarmCoat:
      return "Warm coat";
    case Outer::Coat:
      return "Coat";
    case Outer::Jacket:
      return "Jacket";
    case Outer::LightLayer:
      return "Cardigan";
    case Outer::TShirt:
      return "T-shirt";
  }
  return "";
}

std::string clothingAdvice(const WeatherDay& day) {
  const Clothing c = clothingFor(day);
  std::string out = outerName(c.outer);
  if (c.layers) out += ", layers";
  if (c.umbrella) out += ", umbrella";
  if (c.sunscreen) out += ", sunscreen";
  return out;
}

WeatherIcon weatherIcon(int code) {
  switch (code) {
    case 0:
    case 1:
      return WeatherIcon::Sun;
    case 2:
      return WeatherIcon::PartlyCloudy;
    case 3:
      return WeatherIcon::Cloud;
    case 45:
    case 48:
      return WeatherIcon::Fog;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return WeatherIcon::Snow;
    case 95:
    case 96:
    case 99:
      return WeatherIcon::Storm;
    default:
      return code >= 51 ? WeatherIcon::Rain : WeatherIcon::Cloud;
  }
}

const char* weatherText(int code) {
  switch (code) {
    case 0:
      return "Clear";
    case 1:
      return "Mostly clear";
    case 2:
      return "Partly cloudy";
    case 3:
      return "Cloudy";
    case 45:
    case 48:
      return "Fog";
    case 51:
    case 53:
    case 55:
      return "Drizzle";
    case 56:
    case 57:
      return "Freezing drizzle";
    case 61:
      return "Light rain";
    case 63:
      return "Rain";
    case 65:
      return "Heavy rain";
    case 66:
    case 67:
      return "Freezing rain";
    case 71:
    case 73:
    case 75:
    case 77:
      return "Snow";
    case 80:
    case 81:
      return "Showers";
    case 82:
      return "Heavy showers";
    case 85:
    case 86:
      return "Snow showers";
    case 95:
      return "Thunderstorm";
    case 96:
    case 99:
      return "Thunderstorm, hail";
    default:
      return "";
  }
}

std::string weekdayName(std::string_view date) {
  int y = 0, m = 0, d = 0;
  const std::string text(date);
  if (sscanf(text.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return "";
  static const int offset[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};  // Sakamoto's method
  if (m < 3) --y;
  const int dow = (y + y / 4 - y / 100 + y / 400 + offset[m - 1] + d) % 7;  // 0 = Sunday
  static const char* const NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return NAMES[dow];
}

}  // namespace feed
