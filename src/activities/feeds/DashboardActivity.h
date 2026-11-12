#pragma once

#include <Calendar.h>
#include <DashboardFile.h>
#include <Weather.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "feeds/FeedStore.h"

// One page for a glance: the date, the saved weather forecast, and what is new in the feeds.
// It reads only what the last sync stored, so it opens at once and works offline.
// Confirm runs a sync, which also refreshes the forecast.
class DashboardActivity final : public Activity {
 public:
  DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Draws the dashboard as the panel's sleep screen (no button hints), from what the last sync stored.
  // True when sleeping now should leave the dashboard: the setting is on and no book or article is open.
  static bool wantedOnSleep();
  static void drawSleepScreen(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  void load();    // reads the stored data; the caller holds the render lock if one is needed
  void reload();  // load() under the render lock
  void drawPage(bool withHints);

  feed::DashboardConfig config_;
  feed::Weather weather_;
  int64_t weatherFetched_ = 0;
  bool indoorOk_ = false;
  float indoorC_ = 0;
  int indoorPct_ = 0;
  int32_t today_ = 0;                // local day, days since 1970
  std::vector<feed::Event> events_;  // the next few days
  bool hasHoliday_ = false;
  std::string calendarNote_;  // when the calendars last synced, or what went wrong
  feed::Event holiday_;       // the next public holiday
};
