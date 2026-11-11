#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// The word lists in /wordlists, each with how many of its words are new to you. Opening one starts Discover.
class WordListsActivity final : public UiListActivity {
 public:
  WordListsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rows_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return "Discover words"; }
  void reload();

  std::vector<std::string> lists_;
  std::vector<std::string> labels_;
  std::vector<std::string> values_;
  std::vector<freeink::ui::ListItem> rows_;
};
