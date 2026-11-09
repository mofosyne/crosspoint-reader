#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace flashcards {

// A deck is a tab-separated text file, /flashcards/<name>.tsv, one card per line:
//   front <TAB> back <TAB> tags (optional)
// Lines starting with '#' are ignored, so an Anki "Notes in Plain Text" export loads as it is.
// Inside a field, <br> or a literal "\n" is a line break; other HTML tags are dropped.
struct Card {
  std::string front;  // plain text, a line break is '\n'
  std::string back;
  std::string tags;
};

struct DeckParse {
  std::vector<Card> cards;
  std::vector<std::string> errors;  // lines that were skipped
};

DeckParse parseDeck(std::string_view text);

// One deck line for a card, ending in '\n'. Tabs in fields become spaces, line breaks become <br>.
std::string serializeCard(const Card& card);

// Plain text of a deck field: <br> and "\n" become line breaks, tags are dropped, entities decoded.
std::string plainField(std::string_view field);

// Stable id of a card, from its front text: editing a card's back or tags keeps its progress.
uint32_t cardKey(std::string_view front);

}  // namespace flashcards
