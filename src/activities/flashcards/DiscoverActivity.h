#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "flashcards/DeckStore.h"

// Walks a word list (/wordlists/<name>.tsv, easiest words first) and offers the words that are not yet in any
// deck: Add puts the word in a deck named after the list, "Know it" hides it for good, "Not now" leaves it for
// another time. Keys: Confirm = Add, Right = Know it, Left = Not now.
class DiscoverActivity final : public Activity {
 public:
  DiscoverActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string listName);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Choice { Skip, Known, Add };
  void advance();
  void choose(Choice choice);
  bool owned(uint32_t key) const;
  int buttonAt(int x, int y) const;

  std::string list_;
  flashcards::Deck words_;
  std::vector<uint32_t> owned_;  // sorted keys of words that are not offered
  flashcards::Card card_;
  size_t pos_ = 0;
  size_t left_ = 0;
  size_t added_ = 0;
  bool loaded_ = false;
  bool finished_ = false;
};
