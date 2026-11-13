#pragma once

#include <CachedArticle.h>
#include <Calendar.h>
#include <ClimateLog.h>
#include <DashboardFile.h>
#include <HalMemory.h>
#include <SeenSet.h>
#include <SourcesFile.h>

#include <string>
#include <string_view>
#include <vector>

// SD card layout of the feed reader (the sources file is written by the user):
//   /feeds/sources.txt        one source per line, see SourcesFile.h
//   /feeds/state/<id>.bin     per source: when it was last checked and the URLs already delivered
//   /feeds/items/<hash>.cpf   one saved article each (CachedArticle format)
namespace feeds {

constexpr const char* SOURCES_PATH = "/feeds/sources.txt";
constexpr const char* DASHBOARD_PATH = "/feeds/dashboard.txt";
constexpr const char* EVENTS_PATH = "/feeds/events.txt";  // written by hand or on the web page
// iCal feed number `index` of dashboard.txt, saved by a sync
std::string calendarPath(size_t index);

// File name part of an article: 16 hex digits of the URL's hash.
std::string itemName(const std::string& url);
std::string itemPath(const std::string& url);

bool loadSources(feed::SourcesParse& out);  // false when the file is missing or unreadable

// The dashboard settings; the defaults when the file is missing.
feed::DashboardConfig loadDashboardConfig();

// Adds the events of the hand-written file and of the saved iCal feeds that fall in `out`'s window.
// What the last sync did with each iCal feed of dashboard.txt (by position), so the dashboard and the web page
// can say whether it worked. The feed's address is never stored here: it can hold a private token.
struct CalendarStatus {
  int64_t when = 0;  // 0 = never synced
  bool ok = false;
  int events = 0;
  std::string detail;  // why it failed, e.g. "HTTP 404"
};
void saveCalendarStatus(const std::vector<CalendarStatus>& status);
std::vector<CalendarStatus> loadCalendarStatus();

// The climate log, /feeds/climate2.bin (12 byte records, see ClimateLog.h): the room's temperature and humidity and
// the battery, with what the device was doing. takeClimateReading() measures; logClimate() appends a reading only
// when something moved enough since the last one (or on a slow heartbeat), and returns true when it wrote one.
// takeClimateReading() is false when the room sensor does not answer (the battery fields are still filled in).
bool takeClimateReading(uint8_t wake, feed::ClimateSample& out);
bool logClimate(feed::ClimateSample sample);
// Appends already-chosen readings to the log (the ones a sensor wake kept in retained memory).
void appendClimateRecords(const feed::ClimateSample* samples, size_t count);
// Readings from `since` (seconds since 1970) on, oldest first; at most the last 20,000 records are looked at.
std::vector<feed::ClimateSample> loadClimate(uint32_t since);

// With `holidaysOnly`, only the calendars that filter by region (the public holiday feeds) are read.
void loadEvents(feed::EventList& out, bool holidaysOnly = false);

struct SourceState {
  feed::SeenSet seen;
  int64_t lastCheck = 0;
};
bool loadState(const std::string& sourceId, SourceState& out);  // false: never checked (state left empty)
bool saveState(const std::string& sourceId, const SourceState& state);
// A source whose pages refuse the reader (bot challenge, 401/403): content=auto then keeps to the
// feed's own text. The mark lives next to the state and goes away with it.
bool pagesBlocked(const std::string& sourceId);
void setPagesBlocked(const std::string& sourceId);

// The last weather answer for the dashboard, with the time it was fetched.
bool saveWeather(std::string_view json, int64_t fetched);
bool loadWeather(std::string& json, int64_t& fetched);

bool saveArticle(const feed::CachedArticle& article);  // writes through a temp file, then renames

// Header information for lists; reading it costs one small read per file.
struct ItemInfo {
  std::string url;
  std::string title;
  std::string sourceId;
  int64_t published = 0;
  int64_t fetched = 0;
  bool read = false;
  std::string name;  // file name part, see itemName()
};
// Newest first (by published, else fetched). `sourceId` empty = every source. At most `maxItems`.
std::vector<ItemInfo> listItems(const std::string& sourceId, size_t maxItems = 300);
size_t countUnread(const std::string& sourceId);

// Loads a whole article file into PSRAM and parses it (the views point into `buffer`).
struct LoadedArticle {
  HalMemory::PsramBuffer buffer;
  size_t size = 0;
  feed::CachedArticle article;
};
bool loadArticle(const std::string& url, LoadedArticle& out);
bool markRead(const std::string& url);  // rewrites the header's read flag in place

}  // namespace feeds
