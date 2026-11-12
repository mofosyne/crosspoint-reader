#include "Calendar.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace feed {
namespace {

constexpr size_t MAX_TITLE = 60;

bool digits(std::string_view s, size_t at, size_t n, int& out) {
  if (at + n > s.size()) return false;
  int v = 0;
  for (size_t i = 0; i < n; ++i) {
    const char c = s[at + i];
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return true;
}

bool validDate(int y, int m, int d) {
  if (y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  int ry, rm, rd;
  civilFromDays(daysFromCivil(y, m, d), ry, rm, rd);
  return ry == y && rm == m && rd == d;  // rejects 02-30
}

bool startsWithNoCase(std::string_view s, size_t at, std::string_view word) {
  if (at + word.size() > s.size()) return false;
  for (size_t i = 0; i < word.size(); ++i) {
    const char c = s[at + i];
    if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != word[i]) return false;
  }
  return true;
}

int weekdayOf(int32_t day) { return ((day + 4) % 7 + 7) % 7; }  // 0 = Sunday; 1970-01-01 was a Thursday

// The nth (1-4) or last (5) given weekday of a month.
int32_t nthWeekday(int year, int month, int weekday, int nth) {
  if (nth < 5) {
    const int32_t first = daysFromCivil(year, month, 1);
    return first + (weekday - weekdayOf(first) + 7) % 7 + 7 * (nth - 1);
  }
  const int32_t last = (month == 12 ? daysFromCivil(year + 1, 1, 1) : daysFromCivil(year, month + 1, 1)) - 1;
  return last - (weekdayOf(last) - weekday + 7) % 7;
}

std::string unescape(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[++i];
      out += (n == 'n' || n == 'N') ? ' ' : n;  // a line break becomes a space
    } else {
      out += s[i];
    }
  }
  return out;
}

// "20261120" or "20261120T090000[Z]" -> local day and minute.
bool parseStamp(std::string_view v, int offsetMinutes, int32_t& day, int& minute, bool& allDay) {
  int y, m, d;
  if (!digits(v, 0, 4, y) || !digits(v, 4, 2, m) || !digits(v, 6, 2, d) || !validDate(y, m, d)) return false;
  day = daysFromCivil(y, m, d);
  if (v.size() < 15 || v[8] != 'T') {
    allDay = true;
    minute = -1;
    return true;
  }
  int hh, mm;
  if (!digits(v, 9, 2, hh) || !digits(v, 11, 2, mm)) return false;
  allDay = false;
  int total = hh * 60 + mm;
  if (v.back() == 'Z') total += offsetMinutes;
  while (total < 0) {
    total += 1440;
    --day;
  }
  while (total >= 1440) {
    total -= 1440;
    ++day;
  }
  minute = total;
  return true;
}

}  // namespace

int32_t daysFromCivil(int y, int m, int d) {  // Howard Hinnant's algorithm
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

void civilFromDays(int32_t z, int& year, int& month, int& day) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int y = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  year = y + (month <= 2);
}

std::string dayLabel(int32_t day) {
  static const char* const NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int y, m, d;
  civilFromDays(day, y, m, d);
  const int dow = ((day + 4) % 7 + 7) % 7;  // 1970-01-01 was a Thursday
  char buf[16];
  snprintf(buf, sizeof(buf), "%s %d", NAMES[dow], d);
  return buf;
}

std::string clockText(int minute) {
  if (minute < 0) return "";
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", (minute / 60) % 24, minute % 60);
  return buf;
}

void EventList::add(int32_t day, int minute, std::string_view title) {
  if (!inWindow(day)) return;
  Event e;
  e.day = day;
  e.minute = static_cast<int16_t>(minute);
  e.title = std::string(title.substr(0, MAX_TITLE));
  const auto before = [](const Event& a, const Event& b) {
    return a.day != b.day ? a.day < b.day : a.minute < b.minute;  // all-day (-1) first
  };
  events_.insert(std::upper_bound(events_.begin(), events_.end(), e, before), std::move(e));
  if (events_.size() > keep_) events_.pop_back();
}

// ---- iCalendar --------------------------------------------------------------------------------

void IcsReader::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];
    if (c == '\n') {
      if (!physical_.empty() && physical_.back() == '\r') physical_.pop_back();
      if (!physical_.empty() && (physical_[0] == ' ' || physical_[0] == '\t')) {
        logical_.append(physical_, 1, std::string::npos);  // a folded continuation
      } else {
        if (!logical_.empty()) logicalLine(logical_);
        logical_ = physical_;
      }
      physical_.clear();
    } else if (physical_.size() < 2048) {
      physical_ += c;
    }
  }
}

void IcsReader::finish() {
  feed("\n", 1);
  if (!logical_.empty()) logicalLine(logical_);
  logical_.clear();
}

void IcsReader::logicalLine(std::string_view line) {
  if (line == "BEGIN:VEVENT") {
    inEvent_ = true;
    allDay_ = haveStart_ = hasUntil_ = false;
    startMinute_ = -1;
    title_.clear();
    description_.clear();
    freq_.clear();
    interval_ = 1;
    exdates_.clear();
    return;
  }
  if (line == "END:VEVENT") {
    if (inEvent_) endEvent();
    inEvent_ = false;
    return;
  }
  if (!inEvent_) return;
  const size_t colon = line.find(':');
  if (colon == std::string_view::npos) return;
  const std::string_view name = line.substr(0, std::min(colon, line.find(';')));
  const std::string_view value = line.substr(colon + 1);
  if (name == "DTSTART") {
    haveStart_ = parseStamp(value, offset_, startDay_, startMinute_, allDay_);
  } else if (name == "SUMMARY") {
    title_ = unescape(value);
  } else if (name == "DESCRIPTION") {
    description_ = unescape(value);
  } else if (name == "RRULE") {
    size_t pos = 0;
    while (pos < value.size()) {
      size_t semi = value.find(';', pos);
      if (semi == std::string_view::npos) semi = value.size();
      const std::string_view part = value.substr(pos, semi - pos);
      pos = semi + 1;
      const size_t eq = part.find('=');
      if (eq == std::string_view::npos) continue;
      const std::string_view key = part.substr(0, eq);
      const std::string_view val = part.substr(eq + 1);
      if (key == "FREQ") {
        freq_ = std::string(val);
      } else if (key == "INTERVAL") {
        interval_ = std::max(1, std::atoi(std::string(val).c_str()));
      } else if (key == "UNTIL") {
        int m;
        bool ad;
        int32_t d;
        if (parseStamp(val, 0, d, m, ad)) {
          hasUntil_ = true;
          untilDay_ = d;
        }
      }
    }
  } else if (name == "EXDATE") {
    size_t pos = 0;
    while (pos < value.size()) {
      size_t comma = value.find(',', pos);
      if (comma == std::string_view::npos) comma = value.size();
      int m;
      bool ad;
      int32_t d;
      if (parseStamp(value.substr(pos, comma - pos), offset_, d, m, ad)) exdates_.push_back(d);
      pos = comma + 1;
    }
  }
}

void IcsReader::emit(int32_t day) {
  if (!out_.inWindow(day)) return;
  if (std::find(exdates_.begin(), exdates_.end(), day) != exdates_.end()) return;
  out_.add(day, startMinute_, title_.empty() ? "(no title)" : title_);
}

void IcsReader::endEvent() {
  if (!haveStart_) return;
  if (!region_.empty()) {
    if (description_.rfind("Observance", 0) == 0) return;
    const bool regional = description_.find("Public holiday in ") != std::string::npos;
    if (regional && description_.find(region_) == std::string::npos) return;  // another region's holiday
    const std::string suffixes[] = {" (regional holiday)", " (" + region_ + ")"};
    for (const auto& suffix : suffixes) {
      if (title_.size() > suffix.size() && title_.compare(title_.size() - suffix.size(), suffix.size(), suffix) == 0) {
        title_.erase(title_.size() - suffix.size());
      }
    }
  }
  if (freq_.empty()) {
    emit(startDay_);
    return;
  }
  const int32_t last = hasUntil_ ? std::min(untilDay_, out_.to() - 1) : out_.to() - 1;
  if (freq_ == "DAILY" || freq_ == "WEEKLY") {
    const int step = interval_ * (freq_ == "WEEKLY" ? 7 : 1);
    int32_t day = startDay_;
    if (day < out_.from()) day += ((out_.from() - day + step - 1) / step) * step;  // first one in the window
    for (; day <= last; day += step) emit(day);
    return;
  }
  int sy, sm, sd;
  civilFromDays(startDay_, sy, sm, sd);
  const int stepMonths = freq_ == "YEARLY" ? 12 * interval_ : interval_;
  if (freq_ != "MONTHLY" && freq_ != "YEARLY") {
    emit(startDay_);  // a repeat rule we do not know: show the first one
    return;
  }
  for (int k = 0, guard = 0; guard < 2400; ++guard, k += stepMonths) {
    const int total = sy * 12 + (sm - 1) + k;
    const int y = total / 12;
    const int m = total % 12 + 1;
    if (!validDate(y, m, sd)) continue;  // the 31st in a short month
    const int32_t day = daysFromCivil(y, m, sd);
    if (day > last) break;
    emit(day);
  }
}

// ---- pruning ----------------------------------------------------------------------------------

namespace {
constexpr size_t MAX_EVENT_BLOCK = 8192;  // a longer event (huge description) is dropped
}

void IcsPrune::write(std::string_view text) {
  if (!failed_ && !sink_(text.data(), text.size())) failed_ = true;
}

void IcsPrune::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];
    if (c == '\n') {
      if (!physical_.empty() && physical_.back() == '\r') physical_.pop_back();
      line(physical_);
      physical_.clear();
    } else if (physical_.size() < 4096) {
      physical_ += c;
    }
  }
}

void IcsPrune::finish() {
  if (!physical_.empty()) line(physical_);
  physical_.clear();
  if (started_) write("END:VCALENDAR\r\n");
}

void IcsPrune::line(std::string_view text) {
  if (text == "BEGIN:VCALENDAR") {
    sawCalendar_ = true;
    if (!started_) {
      started_ = true;
      write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n");
    }
    return;
  }
  if (text == "BEGIN:VEVENT") {
    inEvent_ = true;
    tooBig_ = hasStart_ = repeats_ = hasUntil_ = false;
    block_.assign("BEGIN:VEVENT\r\n");
    return;
  }
  if (!inEvent_) return;
  if (text == "END:VEVENT") {
    inEvent_ = false;
    const bool keep =
        !tooBig_ && hasStart_ && (repeats_ ? (!hasUntil_ || untilDay_ >= keepFrom_) : startDay_ >= keepFrom_);
    if (keep) {
      block_ += "END:VEVENT\r\n";
      write(block_);
      ++events_;
    }
    block_.clear();
    return;
  }
  if (block_.size() + text.size() + 2 > MAX_EVENT_BLOCK) {
    tooBig_ = true;
    return;
  }
  block_.append(text);
  block_ += "\r\n";
  const size_t colon = text.find(':');
  if (colon == std::string_view::npos) return;
  const std::string_view name = text.substr(0, std::min(colon, text.find(';')));
  const std::string_view value = text.substr(colon + 1);
  if (name == "DTSTART") {
    int minute;
    bool allDay;
    hasStart_ = parseStamp(value, 0, startDay_, minute, allDay);
  } else if (name == "RRULE") {
    repeats_ = true;
    const size_t until = value.find("UNTIL=");
    if (until != std::string_view::npos) {
      int minute;
      bool allDay;
      hasUntil_ = parseStamp(value.substr(until + 6, 16), 0, untilDay_, minute, allDay);
    }
  }
}

// ---- manual events ---------------------------------------------------------------------------

void parseManualEvents(std::string_view text, EventList& out, std::vector<std::string>* errors) {
  int fromYear, fm, fd;
  civilFromDays(out.from(), fromYear, fm, fd);
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    std::string_view line = text.substr(pos, eol - pos);
    pos = eol + 1;
    if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t')) line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    if (line.empty()) continue;

    int y = 0, m = 0, d = 0;
    size_t at = 0;
    bool yearly = false;
    int weekday = -1;  // for "XXXX-11-TUE1": the nth weekday of a month, every year
    int nth = 0;       // 1-4, or 5 for the last one
    const bool wildcard =
        line.size() > 5 && (line.substr(0, 4) == "XXXX" || line.substr(0, 4) == "xxxx") && line[4] == '-';
    if (wildcard && digits(line, 5, 2, m) && line.size() > 8 && line[7] == '-' && digits(line, 8, 2, d)) {
      yearly = true;  // XXXX-MM-DD
      at = 10;
    } else if (wildcard && digits(line, 5, 2, m) && line.size() > 11 && line[7] == '-') {
      static const char* const DAYS[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
      for (int i = 0; i < 7; ++i) {
        if (startsWithNoCase(line, 8, DAYS[i])) weekday = i;
      }
      const char n = line[11];
      nth = n >= '1' && n <= '4' ? n - '0' : (n == 'L' || n == 'l') ? 5 : 0;
      if (weekday < 0 || nth == 0 || m < 1 || m > 12) {
        if (errors) errors->push_back("Cannot read: " + std::string(line.substr(0, 60)));
        continue;
      }
      yearly = true;
      at = 12;
    } else if (digits(line, 0, 4, y) && line.size() > 4 && line[4] == '-' && digits(line, 5, 2, m) && line.size() > 7 &&
               line[7] == '-' && digits(line, 8, 2, d)) {
      at = 10;
    } else if (digits(line, 0, 2, m) && line.size() > 2 && line[2] == '-' && digits(line, 3, 2, d)) {
      yearly = true;  // MM-DD, the older form of XXXX-MM-DD
      at = 5;
    } else {
      if (errors) errors->push_back("Cannot read: " + std::string(line.substr(0, 60)));
      continue;
    }
    while (at < line.size() && line[at] == ' ') ++at;
    int minute = -1;
    int hh, mm;
    if (digits(line, at, 2, hh) && at + 2 < line.size() && line[at + 2] == ':' && digits(line, at + 3, 2, mm) &&
        hh < 24 && mm < 60) {
      minute = hh * 60 + mm;
      at += 5;
      while (at < line.size() && line[at] == ' ') ++at;
    }
    const std::string_view title = line.substr(at);
    if (title.empty()) {
      if (errors) errors->push_back("No title: " + std::string(line.substr(0, 60)));
      continue;
    }
    if (yearly && weekday < 0 && !validDate(2024, m, d)) {  // a leap year, so 02-29 is allowed
      if (errors) errors->push_back("Not a date: " + std::string(line.substr(0, 60)));
      continue;
    }
    if (yearly) {
      int toYear, tm, td;
      civilFromDays(out.to() - 1, toYear, tm, td);
      for (int year = fromYear; year <= toYear; ++year) {
        if (weekday >= 0) {
          out.add(nthWeekday(year, m, weekday, nth), minute, title);
        } else if (validDate(year, m, d)) {
          out.add(daysFromCivil(year, m, d), minute, title);
        }
      }
    } else if (validDate(y, m, d)) {
      out.add(daysFromCivil(y, m, d), minute, title);
    } else if (errors) {
      errors->push_back("Not a date: " + std::string(line.substr(0, 60)));
    }
  }
}

}  // namespace feed
