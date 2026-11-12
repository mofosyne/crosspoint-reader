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
#include "MappedInputManager.h"
#include "WeatherIcons.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_EVENTS = 9;
constexpr int EVENT_DAYS = 7;
constexpr int HOLIDAY_DAYS = 400;
constexpr time_t CLOCK_SET_AFTER = 1700000000;  // before about 2023 the clock was never set
}  // namespace

DashboardActivity::DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Dashboard", renderer, mappedInput) {}

void DashboardActivity::onEnter() {
  Activity::onEnter();
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
        requestUpdate();
      });
  }
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
  page.drawPage(false);
}

void DashboardActivity::drawPage(const bool withHints) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const int textWidth = width - 2 * x;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID) + 4;
  const int smallHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;
  const int bigFontHeight = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  int y = metrics.topPadding + 6;
  const auto rule = [&] {
    renderer.drawLine(x, y, width - x, y);
    y += metrics.verticalSpacing;
  };
  const auto heading = [&](const char* text) {
    renderer.drawText(UI_10_FONT_ID, x, y, text, true, EpdFontFamily::BOLD);
    y += smallHeight;
  };
  const auto centered = [&](int fontId, int cx, int ty, const std::string& text, EpdFontFamily::Style style) {
    const int w = renderer.getTextWidth(fontId, text.c_str(), style);
    renderer.drawText(fontId, cx - w / 2, ty, text.c_str(), true, style);
  };

  // ---- the date, large, where the title bar would be
  std::string date = "Dashboard";
  const time_t now = time(nullptr);
  if (now > CLOCK_SET_AFTER) {
    struct tm local;
    localtime_r(&now, &local);
    char buf[48];
    strftime(buf, sizeof(buf), "%A %d %B", &local);
    date = buf;
    if (renderer.getTextWidth(NOTOSANS_18_FONT_ID, date.c_str(), EpdFontFamily::BOLD) > textWidth) {
      strftime(buf, sizeof(buf), "%a %d %b", &local);
      date = buf;
    }
  }
  centered(NOTOSANS_18_FONT_ID, width / 2, y, date, EpdFontFamily::BOLD);
  y += bigFontHeight + 6;
  rule();

  // ---- weather
  if (config_.weather) {
    if (weather_.days.empty()) {
      renderer.drawText(UI_12_FONT_ID, x, y, "No forecast yet. Press Refresh.", true);
      y += lineHeight;
    } else {
      const feed::WeatherDay& today = weather_.days[0];
      constexpr int ICON = 112;
      constexpr int NUMBER = 92;
      const int block = NUMBER + 4 + lineHeight;
      const int code = weather_.hasNow ? weather_.nowCode : today.code;
      drawWeatherIcon(renderer, feed::weatherIcon(code), x, y + (block - ICON) / 2, ICON);
      const int textX = x + ICON + 24;
      drawBigTemperature(renderer, weather_.hasNow ? weather_.nowC : today.maxC, textX, y, NUMBER);
      renderer.drawText(
          UI_12_FONT_ID, textX, y + NUMBER + 4,
          renderer
              .truncatedText(UI_12_FONT_ID, (std::string(feed::weatherText(code)) + "  " + config_.name).c_str(),
                             width - x - textX)
              .c_str(),
          true);
      y += block + 8;

      if (today.hasHours) {  // the day in three readings
        const int cell = textWidth / 3;
        const char* const labels[3] = {"Morning", "Midday", "Afternoon"};
        const int temps[3] = {today.morningC, today.middayC, today.afternoonC};
        for (int i = 0; i < 3; ++i) {
          const int cx = x + i * cell + cell / 2;
          centered(UI_10_FONT_ID, cx, y, labels[i], EpdFontFamily::REGULAR);
          centered(NOTOSANS_18_FONT_ID, cx, y + smallHeight, std::to_string(temps[i]) + "\xC2\xB0",
                   EpdFontFamily::BOLD);
        }
        y += smallHeight + bigFontHeight + 10;
      }

      {  // what to wear and bring, as symbols with a word under each
        const feed::Clothing wear = feed::clothingFor(today);
        constexpr int ICON_SIZE = 44;
        const int cell = textWidth / 4;
        int slot = 0;
        const auto item = [&](const char* label, auto&& draw) {
          const int cx = x + slot * cell + cell / 2;
          draw(cx - ICON_SIZE / 2);
          centered(UI_10_FONT_ID, cx, y + ICON_SIZE + 2, label, EpdFontFamily::REGULAR);
          ++slot;
        };
        item(feed::outerName(wear.outer), [&](int ix) { drawOuterIcon(renderer, wear.outer, ix, y, ICON_SIZE); });
        if (wear.layers) item("Layers", [&](int ix) { drawLayersIcon(renderer, ix, y, ICON_SIZE); });
        if (wear.umbrella) item("Umbrella", [&](int ix) { drawUmbrellaIcon(renderer, ix, y, ICON_SIZE); });
        if (wear.sunscreen) item("Sunscreen", [&](int ix) { drawSunscreenIcon(renderer, ix, y, ICON_SIZE); });
        y += ICON_SIZE + smallHeight + 8;
      }

      if (indoorOk_) {  // the room: the on-board temperature and humidity sensor
        char inside[48];
        snprintf(inside, sizeof(inside), "Inside  %.1f\xC2\xB0   %d%% humidity", static_cast<double>(indoorC_),
                 indoorPct_);
        centered(UI_12_FONT_ID, width / 2, y, inside, EpdFontFamily::REGULAR);
        y += lineHeight + 6;
      }
    }
    y += metrics.verticalSpacing / 2;
    rule();
  }

  // ---- events
  heading("COMING UP");
  if (events_.empty()) {
    renderer.drawText(UI_12_FONT_ID, x, y, "Nothing in the next 7 days.", true);
    y += lineHeight;
  }
  // room kept below the events: the holiday line and the calendar note
  const int feedsRoom = (hasHoliday_ ? lineHeight : 0) + (calendarNote_.empty() ? 0 : smallHeight);
  const int limit = renderer.getScreenHeight() - (withHints ? metrics.buttonHintsHeight : 0) - feedsRoom - lineHeight;
  for (const auto& e : events_) {
    if (y > limit) break;  // what does not fit is left out
    std::string when = e.day == today_ ? "Today" : e.day == today_ + 1 ? "Tomorrow" : feed::dayLabel(e.day);
    const std::string clock = feed::clockText(e.minute);
    renderer.drawText(UI_12_FONT_ID, x, y, when.c_str(), true,
                      e.day == today_ ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    const int titleX = x + 140;
    std::string line = clock.empty() ? e.title : clock + "  " + e.title;
    renderer.drawText(UI_12_FONT_ID, titleX, y,
                      renderer.truncatedText(UI_12_FONT_ID, line.c_str(), width - x - titleX).c_str(), true);
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
    const int titleX = x + 110;
    renderer.drawText(UI_12_FONT_ID, x, y, when.c_str(), true,
                      EpdFontFamily::BOLD);  // how far away, where "Today" would be
    renderer.drawText(UI_12_FONT_ID, titleX, y,
                      renderer.truncatedText(UI_12_FONT_ID, text.c_str(), width - x - titleX).c_str(), true);
    y += lineHeight;
  }
  if (!calendarNote_.empty()) {
    renderer.drawText(UI_10_FONT_ID, x, y,
                      renderer.truncatedText(UI_10_FONT_ID, calendarNote_.c_str(), textWidth).c_str(), true,
                      calendarNote_[0] == '!' ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += smallHeight;
  }
  y += metrics.verticalSpacing;
  rule();

  if (withHints) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Refresh", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}
