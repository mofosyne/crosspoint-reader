#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "feeds/FeedStore.h"

// The saved articles of one source (or all sources), newest first; unread ones are marked.
class FeedItemListActivity final : public UiListActivity {
 public:
  FeedItemListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string sourceId, std::string title);

  void onEnter() override;
#ifdef CROSSPOINT_SERIAL_INPUT
  std::string debugState() override;
  bool debugCommand(const std::string& command) override;
#endif

 private:
  int listCount() const override { return static_cast<int>(rows_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return title_.c_str(); }
  void reload();

  std::string sourceId_;
  std::string title_;
  std::vector<feeds::ItemInfo> items_;
  std::vector<std::string> labels_;
  std::vector<std::string> values_;
  std::vector<freeink::ui::ListItem> rows_;
};
