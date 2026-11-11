#include "DashboardFile.h"

#include <algorithm>
#include <cstdlib>

namespace feed {
namespace {

std::vector<std::string> words(std::string_view line) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
    if (i >= line.size()) break;
    std::string word;
    bool quoted = false;
    while (i < line.size() && (quoted || (line[i] != ' ' && line[i] != '\t' && line[i] != '\r'))) {
      if (line[i] == '"') {
        quoted = !quoted;
      } else {
        word += line[i];
      }
      ++i;
    }
    out.push_back(std::move(word));
  }
  return out;
}

bool number(const std::string& text, double lo, double hi, double& out) {
  char* end = nullptr;
  const double v = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || v < lo || v > hi) return false;
  out = v;
  return true;
}

}  // namespace

DashboardConfig parseDashboard(std::string_view text) {
  DashboardConfig config;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    std::string_view line = text.substr(pos, eol - pos);
    pos = eol + 1;
    for (size_t i = 0; i < line.size(); ++i) {  // a '#' starts a comment, unless it is inside a word (a URL)
      if (line[i] == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
        line = line.substr(0, i);
        break;
      }
    }
    const auto w = words(line);
    if (w.empty()) continue;
    const std::string bad = "Cannot read: " + std::string(line.substr(0, 60));
    if (w[0] == "calendar") {
      if (w.size() == 2 && (w[1].rfind("http://", 0) == 0 || w[1].rfind("https://", 0) == 0) &&
          config.calendars.size() < 3) {
        config.calendars.push_back(w[1]);
      } else {
        config.errors.push_back(bad);
      }
      continue;
    }
    if (w[0] != "weather") {
      config.errors.push_back(bad);
      continue;
    }
    if (w.size() == 2 && w[1] == "off") {
      config.weather = false;
      continue;
    }
    double lat = 0, lon = 0;
    if (w.size() < 3 || !number(w[1], -90, 90, lat) || !number(w[2], -180, 180, lon)) {
      config.errors.push_back(bad);
      continue;
    }
    config.weather = true;
    config.latitude = lat;
    config.longitude = lon;
    config.name = "Weather";
    for (size_t i = 3; i < w.size(); ++i) {
      const size_t eq = w[i].find('=');
      const std::string key = w[i].substr(0, eq);
      const std::string value = eq == std::string::npos ? "" : w[i].substr(eq + 1);
      if (key == "name" && !value.empty()) {
        config.name = value;
      } else if (key == "days") {
        config.days = std::clamp(std::atoi(value.c_str()), 1, 7);
      } else {
        config.errors.push_back(bad);
      }
    }
  }
  return config;
}

}  // namespace feed
