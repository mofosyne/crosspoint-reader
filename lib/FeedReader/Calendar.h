#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace feed {

// Days since 1970-01-01 for a civil date, and back.
int32_t daysFromCivil(int year, int month, int day);
void civilFromDays(int32_t days, int& year, int& month, int& day);

struct Event {
  int32_t day = 0;     // local date, days since 1970
  int16_t minute = -1;  // minutes after midnight; -1 = all day
  std::string title;
};

// The events inside a window of days, in time order, at most `keep` of them (the latest ones are dropped).
class EventList {
 public:
  EventList(int32_t fromDay, int days, size_t keep) : from_(fromDay), to_(fromDay + days), keep_(keep) {}
  void add(int32_t day, int minute, std::string_view title);
  bool inWindow(int32_t day) const { return day >= from_ && day < to_; }
  int32_t from() const { return from_; }
  int32_t to() const { return to_; }
  const std::vector<Event>& events() const { return events_; }

 private:
  int32_t from_;
  int32_t to_;
  size_t keep_;
  std::vector<Event> events_;
};

// Reads an iCalendar (.ics) stream a piece at a time, so a large file never has to fit in memory.
// Handles all-day and timed events, UTC times (shifted by `utcOffsetMinutes`), line folding, and simple
// repeats (RRULE with FREQ DAILY/WEEKLY/MONTHLY/YEARLY, INTERVAL and UNTIL; COUNT and BYDAY are ignored)
// and EXDATE. Events that do not fall in the window are dropped as they are read.
class IcsReader {
 public:
  IcsReader(EventList& out, int utcOffsetMinutes) : out_(out), offset_(utcOffsetMinutes) {}
  void feed(const char* data, size_t len);
  void finish();

 private:
  void logicalLine(std::string_view line);
  void endEvent();
  void emit(int32_t day);

  EventList& out_;
  int offset_;
  std::string physical_;
  std::string logical_;
  bool inEvent_ = false;
  bool allDay_ = false;
  bool haveStart_ = false;
  int32_t startDay_ = 0;
  int startMinute_ = -1;
  std::string title_;
  std::string freq_;
  int interval_ = 1;
  bool hasUntil_ = false;
  int32_t untilDay_ = 0;
  std::vector<int32_t> exdates_;
};

// Reads the hand-written events file, one per line ('#' starts a comment):
//   2026-11-20 09:00 Dentist       a dated event (the time is optional)
//   11-20 Mum's birthday           every year
// Lines that cannot be read are described in `errors`.
void parseManualEvents(std::string_view text, EventList& out, std::vector<std::string>* errors = nullptr);

// "Thu 12" for a day.
std::string dayLabel(int32_t day);

// "09:00", or "" for an all-day event.
std::string clockText(int minute);

}  // namespace feed
