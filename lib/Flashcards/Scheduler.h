#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace flashcards {

// Spaced repetition in the SM-2 style. Days are whole days since 1970 ("today" comes from the clock).
struct CardState {
  uint32_t key = 0;       // cardKey() of the front
  int32_t due = 0;        // day the card is next due
  uint16_t interval = 0;  // days
  uint16_t ease = 250;    // percent growth per good review, never below 130
  uint16_t reps = 0;      // good reviews in a row
  uint16_t lapses = 0;    // times forgotten
  bool seen = false;      // false: never reviewed
};

enum class Grade : uint8_t { Again, Hard, Good, Easy };

CardState applyGrade(CardState state, Grade grade, int32_t today);
// Days until the next review if graded so (0 = again this session), for the button labels.
int daysUntil(const CardState& state, Grade grade, int32_t today);

bool isDue(const CardState& state, int32_t today);  // reviewed before and due now

// Cards to study now: due reviews first (most overdue first), then up to `newLimit` unseen cards, in deck order.
// `states` is parallel to the deck; the result holds indexes into it.
std::vector<size_t> studyQueue(const std::vector<CardState>& states, int32_t today, size_t newLimit);

// Progress file text: one line per reviewed card, "<hex key> <due> <interval> <ease> <reps> <lapses>".
std::string serializeProgress(const std::vector<CardState>& states);
// Merges saved progress into `states` by key. Cards missing from the deck are dropped.
void applyProgress(std::string_view text, std::vector<CardState>& states);

}  // namespace flashcards
