#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "flashcards/DeckStore.h"
#include "util/Dictionary.h"

// One study session over a deck: the cards that are due, then a few new ones. Tap or Confirm shows the
// answer; then grade it with the four buttons on screen (touch) or with the keys: Left = Again, Down = Hard,
// Confirm = Good, Right = Easy. A card answered Again comes round again in the same session.
class FlashcardReviewActivity final : public Activity {
 public:
  static constexpr size_t NEW_PER_SESSION = 15;

  FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, flashcards::DeckRef deck);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void startSession();
  void showNext();
  void answer(flashcards::Grade grade);
  void flip();
  void lookupFront();
  int buttonAt(int x, int y) const;
  static std::string intervalText(int days);

  flashcards::DeckRef ref_;
  flashcards::Deck deck_;
  flashcards::Card card_;
  std::vector<size_t> queue_;
  size_t queuePos_ = 0;
  size_t answered_ = 0;
  int32_t today_ = 0;
  bool loaded_ = false;
  bool flipped_ = false;
  bool finished_ = false;
  int sinceSave_ = 0;
  std::unique_ptr<Dictionary> dict_;  // opened the first time a card is looked up
  std::string notice_;                // a short message above the buttons
};
