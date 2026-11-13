#pragma once

#include <Calendar.h>
#include <ClimateLog.h>
#include <DashboardFile.h>
#include <Weather.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "feeds/FeedStore.h"
#include "util/ButtonNavigator.h"

// The dashboard, in four pages that the page buttons (or a tap on either side) step through:
//   Main      the date, outdoor temperature and weather symbol, what to wear, the room's temperature and
//             humidity, and the next events and holiday. This is the page left on the panel while asleep.
//   Forecast  the days ahead and the day's morning, midday and afternoon temperatures
//   Climate   a graph of the logged indoor temperature and humidity
//   Calendar  the next month of events
// It reads only what the last sync stored, so it opens at once and works offline. Confirm refreshes the
// weather and calendars.
class DashboardActivity final : public Activity {
 public:
  DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // True when sleeping now should leave the dashboard: the setting is on and no book or article is open.
  static bool wantedOnSleep();
  // The page `delta` steps from `page`, wrapping round (the page buttons use it while the device sleeps).
  static uint8_t pageAfter(uint8_t page, int delta);
  // Draws the main page as the panel's sleep screen (no button hints), from what the last sync stored.
  static void drawSleepScreen(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  enum Page : uint8_t { PAGE_MAIN, PAGE_FORECAST, PAGE_CLIMATE, PAGE_CALENDAR, PAGE_COUNT };

  void load();    // reads the stored data; the caller holds the render lock if one is needed
  void reload();  // load() under the render lock
  void loadPageData();
  void turnPage(int delta);
  void drawPage(bool withHints);
  void drawMain(int y);
  void drawForecast(int y);
  void drawClimate(int y);
  void drawCalendar(int y);
  int drawTitle(const char* title);  // the title of a page after the main one; returns the next y
  void centeredText(int fontId, int cx, int y, const std::string& text, bool bold) const;
  void drawEventRow(int y, const feed::Event& e) const;

  Page page_ = PAGE_MAIN;
  ButtonNavigator buttonNavigator;
  feed::DashboardConfig config_;
  feed::Weather weather_;
  int64_t weatherFetched_ = 0;
  bool indoorOk_ = false;
  float indoorC_ = 0;
  int indoorPct_ = 0;
  int32_t today_ = 0;                // local day, days since 1970
  std::vector<feed::Event> events_;  // the next week, for the main page
  std::vector<feed::Event> month_;   // the next month, for the calendar page
  bool hasHoliday_ = false;
  feed::Event holiday_;       // the next public holiday
  std::string calendarNote_;  // when the calendars last synced, or what went wrong
  std::vector<feed::ClimateSample> climate_;
  bool climateWeek_ = false;  // the climate page shows a week instead of a day
};
