#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Pick one row of a short list (the links on the current article page, or its contents). Writes the chosen row to `*chosen` (or -1 when
// the user goes back) and finishes; the article viewer reads it from its result callback.
class ZimLinkListActivity final : public UiListActivity {
 public:
  ZimLinkListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* title,
                      std::vector<std::string> labels, int* chosen);

 #ifdef CROSSPOINT_SERIAL_INPUT
  std::string debugState() override {
    std::string out = "ZimLinks '" + title_ + "'\n";
    for (size_t i = 0; i < labels_.size(); ++i) out += std::to_string(i) + ": " + labels_[i] + "\n";
    return out;
  }
  bool debugCommand(const std::string& command) override {
    if (command.rfind("ROW:", 0) == 0) {
      activateIndex(atoi(command.c_str() + 4));
      return true;
    }
    if (command == "BACK") {
      onBackButton();
      return true;
    }
    return false;
  }
#endif

 private:
  int listCount() const override { return static_cast<int>(items_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

  std::string title_;
  std::vector<std::string> labels_;
  std::vector<freeink::ui::ListItem> items_;
  int* chosen_;
};
