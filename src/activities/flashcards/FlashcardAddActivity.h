#pragma once

#include <string>

#include "activities/Activity.h"

// Looks a word up in the dictionary chosen in Settings and offers to add it as a card (the word on the
// front, the dictionary's meaning on the back) to the default deck.
class FlashcardAddActivity final : public Activity {
 public:
  FlashcardAddActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string word);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { Searching, Found, NotFound, NoDictionary, Added, Failed };
  void lookup();

  std::string word_;
  std::string headword_;
  std::string meaning_;
  State state_ = State::Searching;
};
