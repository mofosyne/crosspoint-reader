#include "DashboardActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <ctime>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FeedSyncActivity.h"
#include "MappedInputManager.h"
#include "WeatherIcons.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_EVENTS = 6;
constexpr int EVENT_DAYS = 7;
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

  sources_.clear();
  feed::SourcesParse parsed;
  if (feeds::loadSources(parsed)) {
    for (const auto& source : parsed.sources) sources_.push_back({source.name, feeds::countUnread(source.id)});
  }
  unreadTotal_ = 0;
  for (const auto& source : sources_) unreadTotal_ += source.unread;
  latestHeadline_.clear();
  for (const auto& item : feeds::listItems("", 20)) {
    if (item.read) continue;
    latestHeadline_ = item.title;
    break;
  }

  events_.clear();
  const time_t now = time(nullptr);
  if (now > CLOCK_SET_AFTER) {
    struct tm local;
    localtime_r(&now, &local);
    today_ = feed::daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    feed::EventList list(today_, EVENT_DAYS, MAX_EVENTS);
    feeds::loadEvents(list);
    events_ = list.events();
  }
}

void DashboardActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    auto sync = makeUniqueNoThrow<FeedSyncActivity>(renderer, mappedInput, /*checkAll=*/false);
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

  char date[48] = "Dashboard";
  const time_t now = time(nullptr);
  if (now > CLOCK_SET_AFTER) {
    struct tm local;
    localtime_r(&now, &local);
    strftime(date, sizeof(date), "%a %d %b %Y", &local);
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, date);

  const int x = metrics.contentSidePadding;
  const int textWidth = width - 2 * x;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID) + 4;
  const int smallHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const auto rule = [&] {
    renderer.drawLine(x, y, width - x, y);
    y += metrics.verticalSpacing;
  };
  const auto heading = [&](const char* text) {
    renderer.drawText(UI_10_FONT_ID, x, y, text, true, EpdFontFamily::BOLD);
    y += smallHeight;
  };

  // ---- weather
  if (config_.weather) {
    std::string place = config_.name;
    for (auto& c : place) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    heading(place.c_str());
    if (weather_.days.empty()) {
      renderer.drawText(UI_12_FONT_ID, x, y, "No forecast yet. Press Sync.", true);
      y += lineHeight;
    } else {
      constexpr int BIG = 64;
      constexpr int SMALL = 28;
      const int bigHeight = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
      if (weather_.hasNow) {  // the big symbol, the temperature and the words
        drawWeatherIcon(renderer, feed::weatherIcon(weather_.nowCode), x, y, BIG);
        const std::string temp = std::to_string(weather_.nowC) + "\xC2\xB0";
        const int textX = x + BIG + 16;
        renderer.drawText(NOTOSANS_18_FONT_ID, textX, y + (BIG - bigHeight) / 2 - 8, temp.c_str(), true,
                          EpdFontFamily::BOLD);
        renderer.drawText(UI_12_FONT_ID, textX, y + (BIG - bigHeight) / 2 - 8 + bigHeight,
                          feed::weatherText(weather_.nowCode), true);
        y += BIG + 10;
      }
      {  // what to wear and bring, as symbols with a word under each
        const feed::Clothing wear = feed::clothingFor(weather_.days[0]);
        constexpr int ICON = 44;
        const int cell = textWidth / 4;
        int slot = 0;
        const auto item = [&](const char* label, auto&& draw) {
          const int cx = x + slot * cell + cell / 2;
          draw(cx - ICON / 2);
          const int w = renderer.getTextWidth(UI_10_FONT_ID, label);
          renderer.drawText(UI_10_FONT_ID, cx - w / 2, y + ICON + 2, label, true);
          ++slot;
        };
        item(feed::outerName(wear.outer), [&](int ix) { drawOuterIcon(renderer, wear.outer, ix, y, ICON); });
        if (wear.layers) item("Layers", [&](int ix) { drawLayersIcon(renderer, ix, y, ICON); });
        if (wear.umbrella) item("Umbrella", [&](int ix) { drawUmbrellaIcon(renderer, ix, y, ICON); });
        if (wear.sunscreen) item("Sunscreen", [&](int ix) { drawSunscreenIcon(renderer, ix, y, ICON); });
        y += ICON + smallHeight + 6;
      }
      for (const auto& day : weather_.days) {  // one row a day: the weekday, a small symbol, the range, the rain
        const std::string name = feed::weekdayName(day.date);
        const std::string range = std::to_string(day.minC) + "-" + std::to_string(day.maxC) + "\xC2\xB0";
        const std::string rain = std::to_string(day.rainPercent) + "%";
        renderer.drawText(UI_12_FONT_ID, x, y, name.c_str(), true);
        drawWeatherIcon(renderer, feed::weatherIcon(day.code), x + 70, y - 2, SMALL);
        renderer.drawText(UI_12_FONT_ID, width - x - 160, y, range.c_str(), true);
        renderer.drawText(UI_12_FONT_ID, width - x - renderer.getTextWidth(UI_12_FONT_ID, rain.c_str()), y,
                          rain.c_str(), true);
        y += SMALL + 4;
      }
      if (weatherFetched_ > CLOCK_SET_AFTER) {
        struct tm local;
        const time_t fetched = static_cast<time_t>(weatherFetched_);
        localtime_r(&fetched, &local);
        char stamp[32];
        strftime(stamp, sizeof(stamp), "Updated %a %H:%M", &local);
        renderer.drawText(UI_10_FONT_ID, x, y, stamp, true);
        y += smallHeight;
      }
    }
    y += metrics.verticalSpacing;
    rule();
  }

  // ---- events
  heading("COMING UP");
  if (events_.empty()) {
    renderer.drawText(UI_12_FONT_ID, x, y, "Nothing in the next 7 days.", true);
    y += lineHeight;
  }
  for (const auto& e : events_) {
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
  y += metrics.verticalSpacing;
  rule();

  // ---- feeds: one short line
  if (!sources_.empty()) {
    const std::string unread = std::to_string(unreadTotal_) + " unread";
    renderer.drawText(UI_10_FONT_ID, x, y, "FEEDS", true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, width - x - renderer.getTextWidth(UI_10_FONT_ID, unread.c_str()), y,
                      unread.c_str(), true);
    y += smallHeight;
    if (!latestHeadline_.empty()) {
      renderer.drawText(UI_10_FONT_ID, x, y,
                        renderer.truncatedText(UI_10_FONT_ID, latestHeadline_.c_str(), textWidth).c_str(), true);
      y += smallHeight;
    }
  }

  if (withHints) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Sync", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}
