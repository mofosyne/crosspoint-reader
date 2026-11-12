#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace feed {

// Days since 1970-01-01 for a civil date, and back.
int32_t daysFromCivil(int year, int month, int day);
void civilFromDays(int32_t days, int& year, int& month, int& day);

struct Event {
  int32_t day = 0;      // local date, days since 1970
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
  // With a `region`, only public holidays that apply there are kept (national ones and ones that name the
  // region in their description), and observances are dropped. This is how Google's holiday calendars
  // mark them; "(regional holiday)" and "(<region>)" are cut from the titles.
  IcsReader(EventList& out, int utcOffsetMinutes, std::string_view region = {})
      : out_(out), offset_(utcOffsetMinutes), region_(region) {}
  void feed(const char* data, size_t len);
  void finish();

 private:
  void logicalLine(std::string_view line);
  void endEvent();
  void emit(int32_t day);

  EventList& out_;
  int offset_;
  std::string region_;
  std::string description_;
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

// Copies an iCalendar stream down to what the dashboard can use, a piece at a time: the events that start on or
// after `keepFromDay`, and the repeating ones that have not ended. A calendar's whole history is dropped, so a big
// private feed fits on the card. Everything else (time zone blocks, alarms outside events) is left out.
class IcsPrune {
 public:
  using Sink = std::function<bool(const char* data, size_t len)>;
  IcsPrune(int32_t keepFromDay, Sink sink) : keepFrom_(keepFromDay), sink_(std::move(sink)) {}
  void feed(const char* data, size_t len);
  void finish();
  size_t events() const { return events_; }          // events written
  bool sawCalendar() const { return sawCalendar_; }  // the stream really was an iCalendar
  bool failed() const { return failed_; }            // the sink refused data

 private:
  void line(std::string_view text);
  void write(std::string_view text);

  int32_t keepFrom_;
  Sink sink_;
  std::string physical_;
  std::string block_;
  bool inEvent_ = false;
  bool tooBig_ = false;
  bool started_ = false;
  bool sawCalendar_ = false;
  bool failed_ = false;
  bool hasStart_ = false;
  int32_t startDay_ = 0;
  bool repeats_ = false;
  bool hasUntil_ = false;
  int32_t untilDay_ = 0;
  size_t events_ = 0;
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
