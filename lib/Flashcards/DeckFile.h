#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace flashcards {

// A deck is a tab-separated text file, /cards/<topic>/cards.tsv, one card per line. The layout is the one
// used by Hector68's CrossPoint fork, so decks can be shared:
//   word <TAB> translation                                   Anki's own Front/Back export
//   word <TAB> transcription <TAB> translation               with a reading (kana, pinyin, ...)
//   word <TAB> transcription <TAB> translation <TAB> context with an example sentence
// and an optional last field naming a .bmp/.png/.jpg picture. Lines starting with '#' are ignored, so an
// Anki "Notes in Plain Text" export loads as it is. Inside a field, <br> or a literal "\n" is a line break;
// other HTML tags are dropped.
struct Card {
  std::string front;    // the word
  std::string reading;  // pronunciation hint, may be empty
  std::string back;     // the translation or meaning
  std::string context;  // an example sentence, may be empty
  std::string image;    // picture file name, may be empty
};

struct DeckParse {
  std::vector<Card> cards;
  std::vector<std::string> errors;  // lines that were skipped
};

// Reads one deck line. False when it has no word or no translation.
bool parseCardLine(std::string_view line, Card& out);

DeckParse parseDeck(std::string_view text);

// One deck line for a card, ending in '\n', in the shortest layout that holds it. Tabs in fields become spaces,
// line breaks become <br>.
std::string serializeCard(const Card& card);

// Plain text of a deck field: <br> and "\n" become line breaks, tags are dropped, entities decoded.
std::string plainField(std::string_view field);

// Stable id of a card, from its front text: editing a card's back or tags keeps its progress.
uint32_t cardKey(std::string_view front);

}  // namespace flashcards
