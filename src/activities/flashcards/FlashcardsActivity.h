#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "flashcards/DeckStore.h"

// The deck list: "Add a word" on top, then each deck with how many cards are due and how many are new.
class FlashcardsActivity final : public UiListActivity {
 public:
  FlashcardsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
#ifdef CROSSPOINT_SERIAL_INPUT
  bool debugCommand(const std::string& command) override;
#endif

 private:
  int listCount() const override { return static_cast<int>(rows_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override { return "Flashcards"; }
  void reload();
  void startAddWord();

  std::vector<flashcards::DeckRef> decks_;
  std::vector<std::string> labels_;
  std::vector<std::string> values_;
  std::vector<freeink::ui::ListItem> rows_;
};
