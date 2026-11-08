#include "DashboardActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <ctime>

#include "FeedSyncActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_HEADLINES = 8;
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
  weather_ = feed::Weather{};
  weatherFetched_ = 0;
  std::string json;
  if (feeds::loadWeather(json, weatherFetched_)) feed::parseOpenMeteo(json, weather_);

  sources_.clear();
  feed::SourcesParse parsed;
  if (feeds::loadSources(parsed)) {
    for (const auto& source : parsed.sources) sources_.push_back({source.name, feeds::countUnread(source.id)});
  }
  headlines_.clear();
  for (const auto& item : feeds::listItems("", 60)) {
    if (item.read) continue;
    headlines_.push_back(item.title);
    if (headlines_.size() >= MAX_HEADLINES) break;
  }
}

void DashboardActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    auto sync = makeUniqueNoThrow<FeedSyncActivity>(renderer, mappedInput, /*checkAll=*/false);
    if (sync) startActivityForResult(std::move(sync), [this](const ActivityResult&) { reload(); requestUpdate(); });
  }
}

void DashboardActivity::render(RenderLock&&) {
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
  heading("MELBOURNE");
  if (weather_.days.empty()) {
    renderer.drawText(UI_12_FONT_ID, x, y, "No forecast yet. Press Sync.", true);
    y += lineHeight;
  } else {
    if (weather_.hasNow) {
      const std::string temp = std::to_string(weather_.nowC) + "\xC2\xB0";
      renderer.drawText(NOTOSANS_18_FONT_ID, x, y, temp.c_str(), true, EpdFontFamily::BOLD);
      const int tempWidth = renderer.getTextWidth(NOTOSANS_18_FONT_ID, temp.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, x + tempWidth + 12, y + 6, feed::weatherText(weather_.nowCode), true);
      y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 4;
    }
    for (const auto& day : weather_.days) {
      const std::string name = feed::weekdayName(day.date);
      const std::string range = std::to_string(day.minC) + "-" + std::to_string(day.maxC) + "\xC2\xB0";
      const std::string rain = std::to_string(day.rainPercent) + "%";
      renderer.drawText(UI_12_FONT_ID, x, y, name.c_str(), true);
      renderer.drawText(UI_12_FONT_ID, x + 60, y, renderer.truncatedText(UI_12_FONT_ID, feed::weatherText(day.code), textWidth - 230).c_str(), true);
      renderer.drawText(UI_12_FONT_ID, width - x - 160, y, range.c_str(), true);
      renderer.drawText(UI_12_FONT_ID, width - x - renderer.getTextWidth(UI_12_FONT_ID, rain.c_str()), y, rain.c_str(), true);
      y += lineHeight;
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

  // ---- feeds
  heading("FEEDS");
  if (sources_.empty()) {
    renderer.drawText(UI_12_FONT_ID, x, y, "No sources yet.", true);
    y += lineHeight;
  }
  for (const auto& source : sources_) {
    const std::string count = std::to_string(source.unread);
    renderer.drawText(UI_12_FONT_ID, x, y, renderer.truncatedText(UI_12_FONT_ID, source.name.c_str(), textWidth - 60).c_str(), true);
    renderer.drawText(UI_12_FONT_ID, width - x - renderer.getTextWidth(UI_12_FONT_ID, count.c_str()), y, count.c_str(), true,
                      source.unread > 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += lineHeight;
  }
  y += metrics.verticalSpacing;
  const int limit = renderer.getScreenHeight() - metrics.buttonHintsHeight - lineHeight;
  for (const auto& title : headlines_) {
    if (y > limit) break;
    renderer.drawText(UI_10_FONT_ID, x, y, renderer.truncatedText(UI_10_FONT_ID, title.c_str(), textWidth).c_str(), true);
    y += smallHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Sync", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
