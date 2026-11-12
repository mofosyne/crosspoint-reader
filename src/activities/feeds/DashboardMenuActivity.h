#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// The dashboard's home: show it, refresh the weather and calendars, and see what the last sync did with each
// calendar. The calendar addresses themselves are edited on the web page (File Transfer, then Feeds).
class DashboardMenuActivity final : public UiListActivity {
 public:
  DashboardMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
#ifdef CROSSPOINT_SERIAL_INPUT
  bool debugCommand(const std::string& command) override;
#endif

 private:
  int listCount() const override { return static_cast<int>(rows_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return "Dashboard"; }
  void reload();
  void refresh();

  std::vector<std::string> labels_;
  std::vector<std::string> values_;
  std::vector<freeink::ui::ListItem> rows_;
};
