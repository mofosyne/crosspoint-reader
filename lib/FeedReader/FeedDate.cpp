#include "FeedDate.h"

#include <strings.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace feed {
namespace {

// Days from 1970-01-01 to the given civil date (proleptic Gregorian).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool validDate(int y, int mo, int d, int h, int mi, int s) {
  return y >= 1970 && y <= 2200 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h >= 0 && h <= 23 && mi >= 0 &&
         mi <= 59 && s >= 0 && s <= 60;
}

int64_t toEpoch(int y, int mo, int d, int h, int mi, int s, int offsetMinutes) {
  if (!validDate(y, mo, d, h, mi, s)) return 0;
  const int64_t days = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
  return days * 86400 + h * 3600 + mi * 60 + s - offsetMinutes * 60;
}

int monthFromName(const char* s) {
  static const char* const NAMES[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                      "jul", "aug", "sep", "oct", "nov", "dec"};
  for (int i = 0; i < 12; ++i) {
    if (strncasecmp(s, NAMES[i], 3) == 0) return i + 1;
  }
  return 0;
}

// "+0100", "-05:00", "GMT", "UT", "Z", "EST" ... in minutes east of UTC. False when unreadable.
bool parseZone(const char*& p, int& minutes) {
  while (*p == ' ') ++p;
  minutes = 0;
  if (*p == '+' || *p == '-') {
    const int sign = *p == '-' ? -1 : 1;
    ++p;
    int hh = 0;
    int mm = 0;
    if (!isdigit(static_cast<unsigned char>(p[0])) || !isdigit(static_cast<unsigned char>(p[1]))) return false;
    hh = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    if (*p == ':') ++p;
    if (isdigit(static_cast<unsigned char>(p[0])) && isdigit(static_cast<unsigned char>(p[1]))) {
      mm = (p[0] - '0') * 10 + (p[1] - '0');
      p += 2;
    }
    minutes = sign * (hh * 60 + mm);
    return true;
  }
  struct Zone {
    const char* name;
    int hours;
  };
  static const Zone ZONES[] = {{"GMT", 0}, {"UTC", 0}, {"UT", 0}, {"Z", 0},  {"EST", -5}, {"EDT", -4},
                               {"CST", -6}, {"CDT", -5}, {"MST", -7}, {"MDT", -6}, {"PST", -8}, {"PDT", -7}};
  for (const Zone& z : ZONES) {
    const size_t n = strlen(z.name);
    if (strncasecmp(p, z.name, n) == 0 && !isalpha(static_cast<unsigned char>(p[n]))) {
      minutes = z.hours * 60;
      p += n;
      return true;
    }
  }
  return *p == '\0';  // no zone given: treat as UTC
}

int64_t parseIso(const char* p) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  if (!(isdigit(static_cast<unsigned char>(p[0])) && isdigit(static_cast<unsigned char>(p[3])) && p[4] == '-')) return 0;
  y = atoi(p);
  mo = atoi(p + 5);
  d = atoi(p + 8);
  p += 10;
  if (*p == 'T' || *p == 't' || *p == ' ') {
    ++p;
    h = atoi(p);
    mi = atoi(p + 3);
    p += 5;
    if (*p == ':') {
      s = atoi(p + 1);
      p += 3;
    }
    if (*p == '.') {  // fractional seconds
      ++p;
      while (isdigit(static_cast<unsigned char>(*p))) ++p;
    }
  }
  int offset = 0;
  if (!parseZone(p, offset)) return 0;
  return toEpoch(y, mo, d, h, mi, s, offset);
}

int64_t parseRfc822(const char* p) {
  while (*p == ' ') ++p;
  if (isalpha(static_cast<unsigned char>(*p))) {  // optional weekday, "Fri,"
    while (*p && *p != ',' && *p != ' ') ++p;
    if (*p == ',') ++p;
    while (*p == ' ') ++p;
  }
  if (!isdigit(static_cast<unsigned char>(*p))) return 0;
  const int d = atoi(p);
  while (isdigit(static_cast<unsigned char>(*p))) ++p;
  while (*p == ' ' || *p == '-') ++p;
  const int mo = monthFromName(p);
  if (!mo) return 0;
  while (isalpha(static_cast<unsigned char>(*p))) ++p;
  while (*p == ' ' || *p == '-') ++p;
  int y = atoi(p);
  const char* yearStart = p;
  while (isdigit(static_cast<unsigned char>(*p))) ++p;
  if (p - yearStart == 2) y += y < 70 ? 2000 : 1900;
  while (*p == ' ') ++p;
  int h = 0, mi = 0, s = 0;
  if (isdigit(static_cast<unsigned char>(*p))) {
    h = atoi(p);
    p += 2;
    if (*p == ':') {
      mi = atoi(p + 1);
      p += 3;
    }
    if (*p == ':') {
      s = atoi(p + 1);
      p += 3;
    }
  }
  int offset = 0;
  if (!parseZone(p, offset)) return 0;
  return toEpoch(y, mo, d, h, mi, s, offset);
}

}  // namespace

int64_t parseFeedDate(std::string_view text) {
  while (!text.empty() && isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
  while (!text.empty() && isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
  if (text.size() < 8 || text.size() > 64) return 0;
  const std::string copy(text);
  if (isdigit(static_cast<unsigned char>(copy[0])) && copy.size() >= 10 && copy[4] == '-') return parseIso(copy.c_str());
  return parseRfc822(copy.c_str());
}

}  // namespace feed
