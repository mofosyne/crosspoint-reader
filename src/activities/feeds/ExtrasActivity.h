#pragma once

#include "activities/UiListActivity.h"

// The one extra Home row for this fork's add-ons: Dashboard and Feeds (more rows can go here without
// growing the Home screen).
class ExtrasActivity final : public UiListActivity {
 public:
  ExtrasActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
#ifdef CROSSPOINT_SERIAL_INPUT
  bool debugCommand(const std::string& command) override;
#endif

 private:
  int listCount() const override { return static_cast<int>(rows_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return "Extras"; }

  std::vector<freeink::ui::ListItem> rows_;
};
