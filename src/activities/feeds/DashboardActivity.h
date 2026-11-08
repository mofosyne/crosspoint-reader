#pragma once

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

 private:
  struct SourceLine {
    std::string name;
    size_t unread = 0;
  };

  void reload();

  feed::DashboardConfig config_;
  feed::Weather weather_;
  int64_t weatherFetched_ = 0;
  std::vector<SourceLine> sources_;
  std::vector<std::string> headlines_;  // newest unread, at most MAX_HEADLINES
};
