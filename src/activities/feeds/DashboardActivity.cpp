#include "DashboardActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <ctime>

#include "BigDigits.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FeedSyncActivity.h"
#if FREEINK_CAP_TEMP_HUMIDITY
#include <EnvironmentSensor.h>
#endif
#include "HalPowerManager.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "WeatherIcons.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_EVENTS = 9;
constexpr size_t MAX_MONTH_EVENTS = 16;
constexpr int MONTH_DAYS = 31;
constexpr int EVENT_DAYS = 7;
constexpr int HOLIDAY_DAYS = 400;
constexpr time_t CLOCK_SET_AFTER = 1700000000;  // before about 2023 the clock was never set
}  // namespace

DashboardActivity::DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Dashboard", renderer, mappedInput) {}

uint8_t DashboardActivity::pageAfter(const uint8_t page, const int delta) {
  return static_cast<uint8_t>((page + PAGE_COUNT + delta) % PAGE_COUNT);
}

void DashboardActivity::onEnter() {
  Activity::onEnter();
  APP_STATE.dashboardPage = PAGE_MAIN;
  reload();
  requestUpdate();
}

void DashboardActivity::reload() {
  RenderLock lock(*this);
  load();
}

void DashboardActivity::load() {
  config_ = feeds::loadDashboardConfig();
  weather_ = feed::Weather{};
  weatherFetched_ = 0;
  std::string json;
  if (feeds::loadWeather(json, weatherFetched_)) feed::parseOpenMeteo(json, weather_);

  indoorOk_ = false;
#if FREEINK_CAP_TEMP_HUMIDITY
  {
    EnvironmentSensor sensor;
    float c = 0, h = 0;
    if (sensor.begin() && sensor.read(c, h)) {
      indoorOk_ = true;
      indoorC_ = c;
      indoorPct_ = static_cast<int>(h + 0.5f);
      feeds::logClimate(c, h, static_cast<uint8_t>(powerManager.getBatteryPercentage()));  // for the climate graph
    }
  }
#endif

  events_.clear();
  const time_t now = time(nullptr);
  if (now > CLOCK_SET_AFTER) {
    struct tm local;
    localtime_r(&now, &local);
    today_ = feed::daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    feed::EventList list(today_, EVENT_DAYS, MAX_EVENTS);
    feeds::loadEvents(list);
    events_ = list.events();
    calendarNote_.clear();
    if (!config_.calendars.empty()) {  // did the last sync get the calendars?
      const auto status = feeds::loadCalendarStatus();
      if (status.size() != config_.calendars.size()) {
        calendarNote_ =
            status.empty() ? "Calendars not synced yet. Press Refresh." : "Calendars changed. Press Refresh.";
      } else {
        int total = 0;
        int64_t latest = 0;
        for (size_t i = 0; i < status.size() && calendarNote_.empty(); ++i) {
          if (!status[i].ok) calendarNote_ = "! Calendar " + std::to_string(i + 1) + ": " + status[i].detail;
          total += status[i].events;
          latest = std::max(latest, status[i].when);
        }
        if (calendarNote_.empty()) {
          calendarNote_ = "Calendars synced";
          if (latest > CLOCK_SET_AFTER) {
            struct tm at;
            const time_t stamp = static_cast<time_t>(latest);
            localtime_r(&stamp, &at);
            char buf[32];
            strftime(buf, sizeof(buf), " %a %H:%M", &at);
            calendarNote_ += buf;
          }
          calendarNote_ += ", " + std::to_string(total) + " events";
        }
      }
    }
    feed::EventList holidays(today_, HOLIDAY_DAYS, 1);  // the next public holiday, however far off
    feeds::loadEvents(holidays, /*holidaysOnly=*/true);
    hasHoliday_ = !holidays.events().empty();
    if (hasHoliday_) holiday_ = holidays.events().front();
  }
}

void DashboardActivity::loadPageData() {
  if (page_ == PAGE_CALENDAR) {
    month_.clear();
    feed::EventList list(today_, MONTH_DAYS, MAX_MONTH_EVENTS);
    feeds::loadEvents(list);
    month_ = list.events();
  } else if (page_ == PAGE_CLIMATE) {
    const time_t now = time(nullptr);
    const uint32_t span = climateWeek_ ? 7u * 86400u : 86400u;
    climate_ = now > CLOCK_SET_AFTER ? feeds::loadClimate(static_cast<uint32_t>(now) - span)
                                     : std::vector<feed::ClimateSample>{};
  }
}

void DashboardActivity::turnPage(const int delta) {
  {
    RenderLock lock(*this);
    page_ = static_cast<Page>(pageAfter(page_, delta));
    APP_STATE.dashboardPage = page_;  // the sleep screen shows the page you left it on
    loadPageData();
  }
  requestUpdate();
}

void DashboardActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    auto sync = makeUniqueNoThrow<FeedSyncActivity>(renderer, mappedInput, /*checkAll=*/false, /*scheduled=*/false,
                                                    /*dashboardOnly=*/true);
    if (sync)
      startActivityForResult(std::move(sync), [this](const ActivityResult&) {
        reload();
        loadPageData();
        requestUpdate();
      });
    return;
  }
  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    haptic_feedback::touchAction();
    if (page_ == PAGE_CLIMATE && ty > renderer.getScreenHeight() / 3) {  // a tap on the graphs: day or week
      {
        RenderLock lock(*this);
        climateWeek_ = !climateWeek_;
        loadPageData();
      }
      requestUpdate();
      return;
    }
    turnPage(tx < renderer.getScreenWidth() / 3 ? -1 : 1);
    return;
  }
  buttonNavigator.onNext([this] { turnPage(1); });
  buttonNavigator.onPrevious([this] { turnPage(-1); });
}

void DashboardActivity::render(RenderLock&&) {
  drawPage(true);
  renderer.displayBuffer();
}

bool DashboardActivity::wantedOnSleep() {
  return SETTINGS.dashboardOnSleep && APP_STATE.resumeKind == 0 && !APP_STATE.lastSleepFromReader;
}

void DashboardActivity::drawSleepScreen(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  DashboardActivity page(renderer, mappedInput);
  page.load();
  page.page_ = static_cast<Page>(APP_STATE.dashboardPage % PAGE_COUNT);
  page.loadPageData();
  page.drawPage(false);
}

// ---- drawing -----------------------------------------------------------------------------------------

void DashboardActivity::centeredText(const int fontId, const int cx, const int y, const std::string& text,
                                     const bool bold) const {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int w = renderer.getTextWidth(fontId, text.c_str(), style);
  renderer.drawText(fontId, cx - w / 2, y, text.c_str(), true, style);
}

int DashboardActivity::drawTitle(const char* title) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  int y = metrics.topPadding + 6;
  centeredText(NOTOSANS_18_FONT_ID, width / 2, y, title, true);
  y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 6;
  renderer.drawLine(metrics.contentSidePadding, y, width - metrics.contentSidePadding, y);
  return y + metrics.verticalSpacing;
}

void DashboardActivity::drawEventRow(const int y, const feed::Event& e) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const std::string when = e.day == today_ ? "Today" : e.day == today_ + 1 ? "Tomorrow" : feed::dayLabel(e.day);
  const std::string clock = feed::clockText(e.minute);
  renderer.drawText(UI_12_FONT_ID, x, y, when.c_str(), true,
                    e.day == today_ ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  const int titleX = x + 140;
  const std::string line = clock.empty() ? e.title : clock + "  " + e.title;
  renderer.drawText(UI_12_FONT_ID, titleX, y,
                    renderer.truncatedText(UI_12_FONT_ID, line.c_str(), width - x - titleX).c_str(), true);
}

void DashboardActivity::drawPage(const bool withHints) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int x = metrics.contentSidePadding;
  switch (page_) {
    case PAGE_MAIN: {
      // the date, large, where a title bar would be
      std::string date = "Dashboard";
      const time_t now = time(nullptr);
      if (now > CLOCK_SET_AFTER) {
        struct tm local;
        localtime_r(&now, &local);
        char buf[48];
        strftime(buf, sizeof(buf), "%A %d %B", &local);
        date = buf;
        if (renderer.getTextWidth(NOTOSANS_18_FONT_ID, date.c_str(), EpdFontFamily::BOLD) > width - 2 * x) {
          strftime(buf, sizeof(buf), "%a %d %b", &local);
          date = buf;
        }
      }
      int y = metrics.topPadding + 6;
      centeredText(NOTOSANS_18_FONT_ID, width / 2, y, date, true);
      y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 6;
      renderer.drawLine(x, y, width - x, y);
      drawMain(y + metrics.verticalSpacing);
      break;
    }
    case PAGE_FORECAST:
      drawForecast(drawTitle("Forecast"));
      break;
    case PAGE_CLIMATE:
      drawClimate(drawTitle("Climate"));
      break;
    case PAGE_CALENDAR:
      drawCalendar(drawTitle("Calendar"));
      break;
    case PAGE_COUNT:
      break;
  }
  const int dotsY = height - (withHints ? metrics.buttonHintsHeight : 0) - 14;  // which page this is
  for (int i = 0; i < PAGE_COUNT; ++i) {
    const int cx = width / 2 + static_cast<int>((i - (PAGE_COUNT - 1) * 0.5f) * 24);
    if (i == page_) {
      renderer.fillRect(cx - 6, dotsY - 6, 12, 12, true);
    } else {
      renderer.drawRect(cx - 6, dotsY - 6, 12, 12, true);
    }
  }
  if (withHints) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Refresh", "<", ">");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

void DashboardActivity::drawMain(int y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int x = metrics.contentSidePadding;
  const int textWidth = width - 2 * x;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID) + 4;
  const int smallHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;

  if (!config_.weather || weather_.days.empty()) {
    renderer.drawText(UI_12_FONT_ID, x, y, config_.weather ? "No forecast yet. Press Refresh." : "Weather is off.",
                      true);
    y += lineHeight + 8;
  } else {
    const feed::WeatherDay& today = weather_.days[0];
    constexpr int ICON = 120;
    constexpr int NUMBER = 112;
    const int code = weather_.hasNow ? weather_.nowCode : today.code;
    drawWeatherIcon(renderer, feed::weatherIcon(code), x, y + (NUMBER - ICON) / 2,
                    ICON);  // the symbol says the weather
    drawBigTemperature(renderer, weather_.hasNow ? weather_.nowC : today.maxC, x + ICON + 30, y, NUMBER);
    y += NUMBER + 14;

    // what to wear, with the day's lowest and highest temperature beside it
    const feed::Clothing wear = feed::clothingFor(today);
    constexpr int ICON_SIZE = 44;
    const int rangeWidth = 120;
    const int cell = (textWidth - rangeWidth) / 4;
    int slot = 0;
    const auto item = [&](const char* label, auto&& draw) {
      const int cx = x + slot * cell + cell / 2;
      draw(cx - ICON_SIZE / 2);
      centeredText(UI_10_FONT_ID, cx, y + ICON_SIZE + 2, label, false);
      ++slot;
    };
    item(feed::outerName(wear.outer), [&](int ix) { drawOuterIcon(renderer, wear.outer, ix, y, ICON_SIZE); });
    if (wear.layers) item("Layers", [&](int ix) { drawLayersIcon(renderer, ix, y, ICON_SIZE); });
    if (wear.umbrella) item("Umbrella", [&](int ix) { drawUmbrellaIcon(renderer, ix, y, ICON_SIZE); });
    if (wear.sunscreen) item("Sunscreen", [&](int ix) { drawSunscreenIcon(renderer, ix, y, ICON_SIZE); });
    const int rangeX = width - x - rangeWidth / 2;
    centeredText(NOTOSANS_18_FONT_ID, rangeX, y - 2, std::to_string(today.minC) + "-" + std::to_string(today.maxC),
                 true);
    centeredText(UI_10_FONT_ID, rangeX, y + renderer.getLineHeight(NOTOSANS_18_FONT_ID), "low-high", false);
    y += ICON_SIZE + smallHeight + 10;
  }

  if (indoorOk_) {  // the room: the on-board temperature and humidity sensor, in large digits
    renderer.drawLine(x, y, width - x, y);
    y += 8;
    constexpr int DIGITS = 84;
    const int half = textWidth / 2;
    char t[16];
    snprintf(t, sizeof(t), "%.1fo", static_cast<double>(indoorC_));
    const std::string temp = t;
    const std::string humidity = std::to_string(indoorPct_) + "%";
    drawBigGlyphs(renderer, temp, x + (half - bigGlyphsWidth(temp, DIGITS)) / 2, y, DIGITS);
    drawBigGlyphs(renderer, humidity, x + half + (half - bigGlyphsWidth(humidity, DIGITS)) / 2, y, DIGITS);
    y += DIGITS + 4;
    centeredText(UI_10_FONT_ID, x + half / 2, y, "inside", false);
    centeredText(UI_10_FONT_ID, x + half + half / 2, y, "humidity", false);
    y += smallHeight + 6;
  }
  renderer.drawLine(x, y, width - x, y);
  y += metrics.verticalSpacing;

  // ---- events
  renderer.drawText(UI_10_FONT_ID, x, y, "COMING UP", true, EpdFontFamily::BOLD);
  y += smallHeight;
  if (events_.empty()) {
    renderer.drawText(UI_12_FONT_ID, x, y, "Nothing in the next 7 days.", true);
    y += lineHeight;
  }
  // room kept below the events: the holiday line and the calendar note
  const int below = (hasHoliday_ ? lineHeight : 0) + (calendarNote_.empty() ? 0 : smallHeight);
  const int limit = height - 6 - below - lineHeight;
  for (const auto& e : events_) {
    if (y > limit) break;  // what does not fit is left out
    drawEventRow(y, e);
    y += lineHeight;
  }
  if (hasHoliday_) {  // the next public holiday, with how far away it is
    const int away = holiday_.day - today_;
    const std::string when = away == 0 ? "today" : away == 1 ? "tomorrow" : std::to_string(away) + " days";
    int hy, hm, hd;
    feed::civilFromDays(holiday_.day, hy, hm, hd);
    static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::string text = holiday_.title + ", " + std::to_string(hd) + " " + MONTHS[hm - 1];
    renderer.drawText(UI_12_FONT_ID, x, y, when.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, x + 140, y,
                      renderer.truncatedText(UI_12_FONT_ID, text.c_str(), width - x - 140 - x).c_str(), true);
    y += lineHeight;
  }
  if (!calendarNote_.empty()) {
    renderer.drawText(UI_10_FONT_ID, x, y,
                      renderer.truncatedText(UI_10_FONT_ID, calendarNote_.c_str(), textWidth).c_str(), true,
                      calendarNote_[0] == '!' ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
}

void DashboardActivity::drawForecast(int y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const int smallHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;
  if (weather_.days.empty()) {
    renderer.drawText(UI_12_FONT_ID, x, y, "No forecast yet. Press Refresh.", true);
    return;
  }
  constexpr int ICON = 52;
  const int rowHeight = 84;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - 36;
  for (size_t i = 0; i < weather_.days.size() && y + rowHeight < bottom; ++i) {
    const auto& day = weather_.days[i];
    const std::string name = i == 0 ? "Today" : feed::weekdayName(day.date);
    renderer.drawText(UI_12_FONT_ID, x, y + (ICON - 24) / 2, name.c_str(), true, EpdFontFamily::BOLD);
    drawWeatherIcon(renderer, feed::weatherIcon(day.code), x + 100, y, ICON);
    renderer.drawText(UI_10_FONT_ID, x + 168, y + (ICON - smallHeight) / 2,
                      renderer.truncatedText(UI_10_FONT_ID, feed::weatherText(day.code), 130).c_str(), true);
    const std::string range = std::to_string(day.minC) + "-" + std::to_string(day.maxC) + "\xC2\xB0";
    const int rx = width - x - 66;
    centeredText(NOTOSANS_18_FONT_ID, rx, y - 6, range, true);
    centeredText(UI_10_FONT_ID, rx, y + renderer.getLineHeight(NOTOSANS_18_FONT_ID) - 6,
                 std::to_string(day.rainPercent) + "% rain", false);
    y += rowHeight - 16;
    if (i == 0 && day.hasHours) {  // today in three readings
      const std::string line = "08:00 " + std::to_string(day.morningC) + "\xC2\xB0     12:00 " +
                               std::to_string(day.middayC) + "\xC2\xB0     16:00 " + std::to_string(day.afternoonC) +
                               "\xC2\xB0";
      centeredText(UI_12_FONT_ID, width / 2, y, line, false);
      y += 32;
    }
    y += 16;
    if (i + 1 < weather_.days.size()) renderer.drawLine(x, y - 8, width - x, y - 8);
  }
}

void DashboardActivity::drawCalendar(int y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID) + 10;
  const int x = metrics.contentSidePadding;
  if (month_.empty()) renderer.drawText(UI_12_FONT_ID, x, y, "Nothing in the next month.", true);
  const int limit = renderer.getScreenHeight() - metrics.buttonHintsHeight - 70;
  for (const auto& e : month_) {
    if (y > limit) break;
    drawEventRow(y, e);
    y += lineHeight;
  }
  if (hasHoliday_ && holiday_.day >= today_ + MONTH_DAYS) {  // the holiday after the month shown
    int hy, hm, hd;
    feed::civilFromDays(holiday_.day, hy, hm, hd);
    static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    y += 8;
    renderer.drawText(UI_12_FONT_ID, x, y,
                      ("Next holiday: " + holiday_.title + ", " + std::to_string(hd) + " " + MONTHS[hm - 1]).c_str(),
                      true);
    y += lineHeight;
  }
  if (!calendarNote_.empty() && y <= limit + 40) {
    renderer.drawText(
        UI_10_FONT_ID, x, y + 6,
        renderer.truncatedText(UI_10_FONT_ID, calendarNote_.c_str(), renderer.getScreenWidth() - 2 * x).c_str(), true);
  }
}

namespace {
// One graph of `values` (NaN-free; `valid` marks the gaps) in the box, with its range written at the left.
void drawGraph(const GfxRenderer& r, int bx, int by, int bw, int bh, const std::vector<float>& values,
               const std::vector<bool>& valid, const char* unit, float minSpan, float ceiling) {
  float lo = 1e9f, hi = -1e9f;
  for (size_t i = 0; i < values.size(); ++i) {
    if (!valid[i]) continue;
    lo = std::min(lo, values[i]);
    hi = std::max(hi, values[i]);
  }
  if (hi < lo) {
    r.drawText(UI_10_FONT_ID, bx + 8, by + bh / 2, "no readings yet", true);
    r.drawRect(bx, by, bw, bh, true);
    return;
  }
  if (hi - lo < minSpan) {  // do not blow a tiny wobble up to the full height
    const float mid = (hi + lo) / 2;
    lo = mid - minSpan / 2;
    hi = mid + minSpan / 2;
  }
  if (hi > ceiling) {  // a percentage never shows above 100
    lo -= hi - ceiling;
    hi = ceiling;
  }
  r.drawRect(bx, by, bw, bh, true);
  char label[24];
  snprintf(label, sizeof(label), "%.0f%s", static_cast<double>(hi), unit);
  r.drawText(UI_10_FONT_ID, bx + 6, by + 4, label, true);
  snprintf(label, sizeof(label), "%.0f%s", static_cast<double>(lo), unit);
  r.drawText(UI_10_FONT_ID, bx + 6, by + bh - r.getLineHeight(UI_10_FONT_ID) - 4, label, true);
  const int plotX = bx + 52;
  const int plotW = bw - 60;
  // Readings are joined across empty slices (the log is sparse until the sensor wakes fill it in), but not across
  // a gap longer than a quarter of the graph: that is a stretch with no data, not a straight line.
  const size_t maxGap = std::max<size_t>(2, values.size() / 4);
  int px = -1, py = 0;
  size_t previous = 0;
  for (size_t i = 0; i < values.size(); ++i) {
    if (!valid[i]) continue;
    const int cx = plotX + static_cast<int>(static_cast<float>(i) * static_cast<float>(plotW) /
                                            static_cast<float>(values.size() - 1));
    const int cy = by + bh - 8 - static_cast<int>((values[i] - lo) / (hi - lo) * static_cast<float>(bh - 16));
    if (px >= 0 && i - previous <= maxGap) r.drawLine(px, py, cx, cy, 3, true);
    r.fillRect(cx - 2, cy - 2, 5, 5, true);  // every reading is a dot, so a lone one still shows
    px = cx;
    py = cy;
    previous = i;
  }
}
}  // namespace

void DashboardActivity::drawClimate(int y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const int smallHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;
  if (indoorOk_) {  // now, in big digits
    char t[16];
    snprintf(t, sizeof(t), "%.1fo", static_cast<double>(indoorC_));
    const std::string temp = t;
    const std::string humidity = std::to_string(indoorPct_) + "%";
    const int half = (width - 2 * x) / 2;
    drawBigGlyphs(renderer, temp, x + (half - bigGlyphsWidth(temp, 56)) / 2, y, 56);
    drawBigGlyphs(renderer, humidity, x + half + (half - bigGlyphsWidth(humidity, 56)) / 2, y, 56);
    y += 56 + 12;
  }
  const time_t now = time(nullptr);
  const uint32_t span = climateWeek_ ? 7u * 86400u : 86400u;
  const size_t buckets = climateWeek_ ? 112 : 96;
  const auto data = now > CLOCK_SET_AFTER ? feed::bucketClimate(climate_, static_cast<uint32_t>(now) - span,
                                                                static_cast<uint32_t>(now), buckets)
                                          : std::vector<feed::ClimateBucket>(buckets);
  std::vector<float> temps(buckets), hums(buckets), batteries(buckets);
  std::vector<bool> valid(buckets), batteryValid(buckets);
  for (size_t i = 0; i < buckets; ++i) {
    temps[i] = data[i].tempC;
    hums[i] = data[i].humidity;
    batteries[i] = data[i].battery;
    valid[i] = data[i].valid;
    batteryValid[i] = data[i].valid && data[i].hasBattery;
  }
  const int graphHeight =
      (renderer.getScreenHeight() - metrics.buttonHintsHeight - 70 - y - 3 * (smallHeight + 12)) / 3;
  const auto graph = [&](const char* label, const std::vector<float>& values, const std::vector<bool>& ok,
                         const char* unit, float minSpan, float ceiling) {
    renderer.drawText(UI_10_FONT_ID, x, y, label, true, EpdFontFamily::BOLD);
    y += smallHeight;
    drawGraph(renderer, x, y, width - 2 * x, graphHeight, values, ok, unit, minSpan, ceiling);
    y += graphHeight + 12;
  };
  graph("TEMPERATURE", temps, valid, "\xC2\xB0", 2.0f, 1e9f);
  graph("HUMIDITY", hums, valid, "%", 10.0f, 100.0f);
  graph("BATTERY", batteries, batteryValid, "%", 10.0f, 100.0f);
  renderer.drawText(UI_10_FONT_ID, x + 52, y - 6, climateWeek_ ? "7 days ago" : "24 h ago", true);
  centeredText(UI_10_FONT_ID, width / 2 + 30, y - 6, climateWeek_ ? "tap: 24 hours" : "tap: 7 days", false);
  renderer.drawText(UI_10_FONT_ID, width - x - renderer.getTextWidth(UI_10_FONT_ID, "now"), y - 6, "now", true);
}
