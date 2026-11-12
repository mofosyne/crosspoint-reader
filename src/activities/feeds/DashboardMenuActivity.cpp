#include "DashboardMenuActivity.h"

#include <Memory.h>

#include <ctime>

#include "DashboardActivity.h"
#include "FeedSyncActivity.h"
#include "components/UITheme.h"
#include "feeds/FeedStore.h"

namespace fui = freeink::ui;

namespace {
std::string hostOf(const std::string& url) {
  const size_t start = url.find("//");
  const size_t from = start == std::string::npos ? 0 : start + 2;
  const size_t end = url.find('/', from);
  return url.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

std::string when(int64_t stamp) {
  if (stamp < 1700000000) return "";
  struct tm local;
  const time_t t = static_cast<time_t>(stamp);
  localtime_r(&t, &local);
  char buf[32];
  strftime(buf, sizeof(buf), "%a %H:%M", &local);
  return buf;
}
}  // namespace

DashboardMenuActivity::DashboardMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("DashboardMenu", renderer, mappedInput) {}

void DashboardMenuActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void DashboardMenuActivity::reload() {
  RenderLock lock(*this);
  labels_.clear();
  values_.clear();
  rows_.clear();
  const feed::DashboardConfig config = feeds::loadDashboardConfig();
  labels_.push_back("Show the dashboard");
  values_.push_back("");
  labels_.push_back("Refresh weather and calendars");
  std::string weatherValue = config.weather ? config.name : "weather off";
  std::string json;
  int64_t fetched = 0;
  if (config.weather && feeds::loadWeather(json, fetched) && !when(fetched).empty()) weatherValue += ", " + when(fetched);
  values_.push_back(weatherValue);
  const auto status = feeds::loadCalendarStatus();
  for (size_t i = 0; i < config.calendars.size(); ++i) {
    labels_.push_back("Calendar " + std::to_string(i + 1) + ": " + hostOf(config.calendars[i].url));
    std::string value = "not synced yet";
    if (i < status.size() && status[i].when != 0) {
      value = status[i].ok ? std::to_string(status[i].events) + " events, " + when(status[i].when)
                           : "FAILED: " + status[i].detail;
    }
    values_.push_back(value);
  }
  if (config.calendars.empty()) {
    labels_.push_back("No calendars");
    values_.push_back("add them on the web page");
  }
  for (size_t i = 0; i < labels_.size(); ++i) {
    fui::ListItem row;
    row.label = labels_[i].c_str();
    row.value = values_[i].empty() ? nullptr : values_[i].c_str();
    row.actionValue = static_cast<int16_t>(i);
    rows_.push_back(row);
  }
}

void DashboardMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows_.data();
  props.count = static_cast<uint16_t>(rows_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void DashboardMenuActivity::refresh() {
  if (auto sync = makeUniqueNoThrow<FeedSyncActivity>(renderer, mappedInput, /*checkAll=*/false, /*scheduled=*/false,
                                                      /*dashboardOnly=*/true)) {
    startActivityForResult(std::move(sync), [this](const ActivityResult&) {
      reload();
      requestUpdate();
    });
  }
}

void DashboardMenuActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index < 0 || static_cast<size_t>(index) >= rows_.size()) return;
  if (index == 0) {
    if (auto show = makeUniqueNoThrow<DashboardActivity>(renderer, mappedInput)) {
      startActivityForResult(std::move(show), [](const ActivityResult&) {});
    }
    return;
  }
  refresh();  // the refresh row, and a tap on a calendar row, which is the quickest way to try it again
}

#ifdef CROSSPOINT_SERIAL_INPUT
bool DashboardMenuActivity::debugCommand(const std::string& command) {
  if (command.rfind("ROW:", 0) == 0) {
    activateIndex(atoi(command.c_str() + 4));
    return true;
  }
  return false;
}
#endif
