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
  struct SourceLine {
    std::string name;
    size_t unread = 0;
  };

  void load();    // reads the stored data; the caller holds the render lock if one is needed
  void reload();  // load() under the render lock
  void drawPage(bool withHints);

  feed::DashboardConfig config_;
  feed::Weather weather_;
  int64_t weatherFetched_ = 0;
  size_t unreadTotal_ = 0;
  std::vector<SourceLine> sources_;
  std::string latestHeadline_;       // newest unread title
  int32_t today_ = 0;                // local day, days since 1970
  std::vector<feed::Event> events_;  // the next few days
  bool hasHoliday_ = false;
  feed::Event holiday_;  // the next public holiday
};
