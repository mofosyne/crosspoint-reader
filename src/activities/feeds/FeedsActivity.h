#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "feeds/FeedStore.h"

// Home of the feed reader: "Sync now", one row per source with its unread count, and a row for every
// problem found in the sources file. The sources file itself is edited on a PC (/feeds/sources.txt).
class FeedsActivity final : public UiListActivity {
 public:
  FeedsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
#ifdef CROSSPOINT_SERIAL_INPUT
  std::string debugState() override;
  bool debugCommand(const std::string& command) override;
#endif

 private:
  int listCount() const override { return static_cast<int>(rows_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return "Feeds"; }
  void reload();

  feed::SourcesParse sources_;
  bool haveFile_ = false;
  std::vector<std::string> labels_;
  std::vector<std::string> values_;
  std::vector<freeink::ui::ListItem> rows_;
  size_t sourceRows_ = 0;
};
