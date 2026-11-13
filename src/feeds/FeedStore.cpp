#include "FeedStore.h"

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#if FREEINK_CAP_TEMP_HUMIDITY
#include <EnvironmentSensor.h>
#endif
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace feeds {
namespace {

constexpr const char* TAG = "FEED";
constexpr const char* STATE_DIR = "/feeds/state";
constexpr const char* ITEMS_DIR = "/feeds/items";
constexpr size_t MAX_SOURCES_BYTES = 32 * 1024;
constexpr size_t HEADER_PEEK = 1024;

std::string statePath(const std::string& id) { return std::string(STATE_DIR) + "/" + id + ".bin"; }

}  // namespace

std::string itemName(const std::string& url) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(feed::SeenSet::hashUrl(url)));
  return buf;
}

std::string itemPath(const std::string& url) { return std::string(ITEMS_DIR) + "/" + itemName(url) + ".cpf"; }

bool loadSources(feed::SourcesParse& out) {
  std::string text;
  if (!Storage.readFileToString(TAG, SOURCES_PATH, MAX_SOURCES_BYTES, text)) return false;
  out = feed::parseSources(text);
  return true;
}

feed::DashboardConfig loadDashboardConfig() {
  std::string text;
  if (!Storage.readFileToString(TAG, DASHBOARD_PATH, 4096, text)) return feed::DashboardConfig{};
  return feed::parseDashboard(text);
}

void saveCalendarStatus(const std::vector<CalendarStatus>& status) {
  std::string text;
  for (const auto& s : status) {
    text +=
        std::to_string(s.when) + "\t" + (s.ok ? "1" : "0") + "\t" + std::to_string(s.events) + "\t" + s.detail + "\n";
  }
  Storage.ensureDirectoryExists("/feeds");
  Storage.writeFile("/feeds/calendars.status", String(text.c_str()));
}

std::vector<CalendarStatus> loadCalendarStatus() {
  std::vector<CalendarStatus> out;
  std::string text;
  if (!Storage.readFileToString(TAG, "/feeds/calendars.status", 8 * 1024, text)) return out;
  for (size_t pos = 0; pos < text.size();) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    const std::string line = text.substr(pos, nl - pos);
    pos = nl + 1;
    CalendarStatus s;
    const size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1),
                 t3 = t2 == std::string::npos ? t2 : line.find('\t', t2 + 1);
    if (t3 == std::string::npos) continue;
    s.when = std::strtoll(line.c_str(), nullptr, 10);
    s.ok = line[t1 + 1] == '1';
    s.events = std::atoi(line.c_str() + t2 + 1);
    s.detail = line.substr(t3 + 1);
    out.push_back(std::move(s));
  }
  return out;
}

namespace {
constexpr const char* CLIMATE_PATH = "/feeds/climate2.bin";
constexpr const char* OLD_CLIMATE_PATH = "/feeds/climate.bin";  // 8 byte records, before the battery voltage and flags
constexpr size_t CLIMATE_MAX_RECORDS = 20000;
}  // namespace

bool takeClimateReading(uint8_t wake, feed::ClimateSample& out) {
  out = feed::ClimateSample{};
  bool roomOk = false;
#if FREEINK_CAP_TEMP_HUMIDITY
  {
    EnvironmentSensor sensor;
    float c = 0, h = 0;
    if (sensor.begin() && sensor.read(c, h)) {
      roomOk = true;
      out.tempTenths = static_cast<int16_t>(c * 10.0f + (c < 0 ? -0.5f : 0.5f));
      out.humidity = static_cast<uint8_t>(h < 0 ? 0 : h > 100 ? 100 : h + 0.5f);
    }
  }
#endif
  const BatteryMonitor::Status battery = BatteryMonitor().readStatus();
  if (battery.percentageKnown) out.battery = static_cast<uint8_t>(battery.percentage > 100 ? 100 : battery.percentage);
  if (battery.millivoltsKnown) out.millivolts = battery.millivolts;
  out.flags = static_cast<uint8_t>((battery.charging ? feed::CLIMATE_CHARGING : 0) |
                                   (battery.externalPower ? feed::CLIMATE_EXTERNAL_POWER : 0) | (wake << 4));
  return roomOk;
}

bool logClimate(feed::ClimateSample sample) {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;  // a reading with no date is no use in a graph
  sample.when = static_cast<uint32_t>(now);

  feed::ClimateSample last;  // the newest record, if there is one
  {
    HalFile f;
    if (Storage.openFileForRead(TAG, CLIMATE_PATH, f)) {
      const size_t size = f.fileSize();
      uint8_t bytes[feed::CLIMATE_RECORD_BYTES];
      if (size >= sizeof(bytes) && f.seek(size - sizeof(bytes)) &&
          f.read(bytes, sizeof(bytes)) == static_cast<int>(sizeof(bytes))) {
        last = feed::decodeClimate(bytes);
      }
      f.close();
    }
  }
  if (!feed::worthLogging(last, sample)) return false;
  Storage.ensureDirectoryExists("/feeds");
  HalFile f = Storage.open(CLIMATE_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) return false;
  uint8_t bytes[feed::CLIMATE_RECORD_BYTES];
  feed::encodeClimate(sample, bytes);
  const bool ok = f.write(bytes, sizeof(bytes)) == sizeof(bytes);
  f.close();
  if (ok && Storage.exists(OLD_CLIMATE_PATH)) Storage.remove(OLD_CLIMATE_PATH);  // replaced by the new log
  return ok;
}

void appendClimateRecords(const feed::ClimateSample* samples, size_t count) {
  if (count == 0) return;
  Storage.ensureDirectoryExists("/feeds");
  HalFile f = Storage.open(CLIMATE_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) return;
  for (size_t i = 0; i < count; ++i) {
    uint8_t bytes[feed::CLIMATE_RECORD_BYTES];
    feed::encodeClimate(samples[i], bytes);
    f.write(bytes, sizeof(bytes));
  }
  f.close();
}

std::vector<feed::ClimateSample> loadClimate(uint32_t since) {
  std::vector<feed::ClimateSample> out;
  HalFile f;
  if (!Storage.openFileForRead(TAG, CLIMATE_PATH, f)) return out;
  const size_t size = f.fileSize();
  const size_t records = size / feed::CLIMATE_RECORD_BYTES;
  const size_t first = records > CLIMATE_MAX_RECORDS ? records - CLIMATE_MAX_RECORDS : 0;
  if (!f.seek(first * feed::CLIMATE_RECORD_BYTES)) return out;
  uint8_t chunk[feed::CLIMATE_RECORD_BYTES * 64];
  for (int n = f.read(chunk, sizeof(chunk)); n >= static_cast<int>(feed::CLIMATE_RECORD_BYTES);
       n = f.read(chunk, sizeof(chunk))) {
    for (size_t at = 0; at + feed::CLIMATE_RECORD_BYTES <= static_cast<size_t>(n); at += feed::CLIMATE_RECORD_BYTES) {
      const feed::ClimateSample s = feed::decodeClimate(chunk + at);
      if (s.when >= since) out.push_back(s);
    }
  }
  f.close();
  return out;
}

std::string calendarPath(size_t index) { return "/feeds/cal" + std::to_string(index) + ".ics"; }

void loadEvents(feed::EventList& out, bool holidaysOnly) {
  std::string manual;
  if (!holidaysOnly && Storage.readFileToString(TAG, EVENTS_PATH, 16 * 1024, manual)) {
    feed::parseManualEvents(manual, out);
  }

  // Times in an iCal feed are often UTC: shift them by the offset the clock is using now.
  const time_t now = time(nullptr);
  struct tm local, utc;
  localtime_r(&now, &local);
  gmtime_r(&now, &utc);
  utc.tm_isdst = -1;
  local.tm_isdst = -1;
  const int offsetMinutes = static_cast<int>((mktime(&local) - mktime(&utc)) / 60);
  const feed::DashboardConfig config = loadDashboardConfig();
  for (size_t i = 0; i < config.calendars.size(); ++i) {
    if (holidaysOnly && config.calendars[i].region.empty()) continue;
    HalFile f;
    if (!Storage.openFileForRead(TAG, calendarPath(i), f)) continue;
    feed::IcsReader reader(out, offsetMinutes, config.calendars[i].region);
    char chunk[512];
    for (int n = f.read(chunk, sizeof(chunk)); n > 0; n = f.read(chunk, sizeof(chunk))) {
      reader.feed(chunk, static_cast<size_t>(n));
    }
    reader.finish();
    f.close();
  }
}

bool loadState(const std::string& sourceId, SourceState& out) {
  out = SourceState{};
  std::string blob;
  if (!Storage.readFileToString(TAG, statePath(sourceId), 64 * 1024, blob)) return false;
  return out.seen.parse(blob, out.lastCheck);
}

bool saveState(const std::string& sourceId, const SourceState& state) {
  Storage.ensureDirectoryExists(STATE_DIR);
  const std::string blob = state.seen.serialize(state.lastCheck);
  HalFile f;
  const std::string tmp = statePath(sourceId) + ".tmp";
  if (!Storage.openFileForWrite(TAG, tmp, f)) return false;
  const bool ok = f.write(blob.data(), blob.size()) == blob.size();
  f.close();
  return ok && Storage.replaceFile(tmp.c_str(), statePath(sourceId).c_str());
}

namespace {
constexpr const char* WEATHER_PATH = "/feeds/weather.json";
}

bool saveWeather(std::string_view json, int64_t fetched) {
  Storage.ensureDirectoryExists("/feeds");
  const std::string blob = std::to_string(fetched) + "\n" + std::string(json);
  const std::string tmp = std::string(WEATHER_PATH) + ".tmp";
  HalFile f;
  if (!Storage.openFileForWrite(TAG, tmp, f)) return false;
  const bool ok = f.write(blob.data(), blob.size()) == blob.size();
  f.close();
  return ok && Storage.replaceFile(tmp.c_str(), WEATHER_PATH);
}

bool loadWeather(std::string& json, int64_t& fetched) {
  std::string blob;
  if (!Storage.readFileToString(TAG, WEATHER_PATH, 16 * 1024, blob)) return false;
  const size_t nl = blob.find('\n');
  if (nl == std::string::npos) return false;
  fetched = std::strtoll(blob.c_str(), nullptr, 10);
  json = blob.substr(nl + 1);
  return true;
}

bool pagesBlocked(const std::string& sourceId) {
  return Storage.exists((std::string(STATE_DIR) + "/" + sourceId + ".blocked").c_str());
}

void setPagesBlocked(const std::string& sourceId) {
  Storage.ensureDirectoryExists(STATE_DIR);
  HalFile f;
  if (!Storage.openFileForWrite(TAG, std::string(STATE_DIR) + "/" + sourceId + ".blocked", f)) return;
  f.write("1", 1);
  f.close();
}

bool saveArticle(const feed::CachedArticle& article) {
  Storage.ensureDirectoryExists("/feeds");
  Storage.ensureDirectoryExists(ITEMS_DIR);
  const std::string path = itemPath(article.url);
  const std::string tmp = path + ".tmp";
  HalFile f;
  if (!Storage.openFileForWrite(TAG, tmp, f)) return false;
  const bool ok = feed::serializeArticle(article, [&](const char* d, size_t n) { return f.write(d, n) == n; });
  f.close();
  if (!ok) {
    Storage.remove(tmp.c_str());
    return false;
  }
  return Storage.replaceFile(tmp.c_str(), path.c_str());
}

std::vector<ItemInfo> listItems(const std::string& sourceId, size_t maxItems) {
  std::vector<ItemInfo> items;
  HalFile dir = Storage.open(ITEMS_DIR);
  if (!dir || !dir.isDirectory()) return items;
  char name[64];
  std::string head(HEADER_PEEK, '\0');
  for (HalFile f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    f.getName(name, sizeof(name));
    const size_t len = strlen(name);
    if (len < 5 || strcmp(name + len - 4, ".cpf") != 0) continue;
    const int n = f.read(head.data(), head.size());
    f.close();
    if (n <= 0) continue;
    feed::CachedArticle a;
    if (!feed::parseArticleHeader(std::string_view(head.data(), static_cast<size_t>(n)), a)) continue;
    if (!sourceId.empty() && a.sourceId != sourceId) continue;
    items.push_back({a.url, a.title, a.sourceId, a.published, a.fetched, a.read, std::string(name, len - 4)});
  }
  std::sort(items.begin(), items.end(), [](const ItemInfo& x, const ItemInfo& y) {
    const int64_t kx = x.published ? x.published : x.fetched;
    const int64_t ky = y.published ? y.published : y.fetched;
    return kx > ky;
  });
  if (items.size() > maxItems) items.resize(maxItems);
  return items;
}

size_t countUnread(const std::string& sourceId) {
  size_t unread = 0;
  for (const auto& item : listItems(sourceId, 1000)) unread += item.read ? 0 : 1;
  return unread;
}

bool loadArticle(const std::string& url, LoadedArticle& out) {
  HalFile f;
  if (!Storage.openFileForRead(TAG, itemPath(url), f)) return false;
  const size_t size = f.fileSize();
  out.buffer = HalMemory::allocatePsram(size + 1);
  if (!out.buffer) {
    LOG_ERR(TAG, "PSRAM short for a %u byte article", static_cast<unsigned>(size));
    return false;
  }
  const int n = f.read(out.buffer.get(), size);
  f.close();
  if (n != static_cast<int>(size)) return false;
  out.size = size;
  return feed::parseArticle(std::string_view(reinterpret_cast<const char*>(out.buffer.get()), size), out.article);
}

bool markRead(const std::string& url) {
  // The flag is the single byte after "read: " in the header, so flipping it needs no rewrite.
  HalFile f = Storage.open(itemPath(url).c_str(), O_RDWR);
  if (!f) return false;
  std::string head(HEADER_PEEK, '\0');
  const int n = f.read(head.data(), head.size());
  const size_t at = n > 0 ? head.find("\nread: ") : std::string::npos;
  bool ok = false;
  if (at != std::string::npos && at + 7 < static_cast<size_t>(n) && f.seek(at + 7)) {
    const uint8_t one = '1';
    ok = f.write(&one, 1) == 1;
  }
  f.close();
  return ok;
}

}  // namespace feeds
