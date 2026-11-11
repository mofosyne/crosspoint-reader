#pragma once

#include <DeckFile.h>
#include <HalMemory.h>
#include <Scheduler.h>

#include <string>
#include <string_view>
#include <vector>

// SD card layout of the flashcards (the layout of Hector68's fork, so decks can be shared):
//   /cards/<deck>/cards.tsv       one card per line, see DeckFile.h
//   /cards/<deck>/progress.txt    review progress, written by the device
namespace flashcards {

constexpr const char* CARDS_ROOT = "/cards";
constexpr const char* DEFAULT_DECK = "words";         // where "add a word" puts new cards
constexpr const char* WORDLISTS_ROOT = "/wordlists";  // word lists to discover new words from, <name>.tsv

struct DeckRef {
  std::string name;    // the folder name, shown to the user
  std::string folder;  // "/cards/<name>"
};

// Decks are the folders of /cards that hold a cards.tsv, in name order.
std::vector<DeckRef> listDecks();
// A deck name safe to use as a folder name: letters, digits, space, '-' and '_'. Empty when nothing is left.
std::string cleanDeckName(std::string_view name);
// Appends one card to a deck, creating the deck if it is new.
bool appendCard(std::string_view deckName, const Card& card);
// The days since 1970 in local time, or the visit counter when the clock was never set.
int32_t today(uint32_t visitCounter);
bool clockIsSet();

// The word lists: the .tsv files of /wordlists, in name order (the names without the extension).
std::vector<std::string> listWordLists();
// The cards of the lists the user has marked as known without adding (keys of their words).
std::vector<uint32_t> loadKnownWords(const std::string& listName);
bool addKnownWord(const std::string& listName, uint32_t key);
// Keys of every word already in a deck or marked as known for this list, sorted.
std::vector<uint32_t> ownedWords(const std::string& listName);

// One deck loaded for study. The file text and the card states live in PSRAM, so a deck of several
// thousand cards costs the small heap only a 4 byte index entry per card.
class Deck {
 public:
  static constexpr size_t MAX_BYTES = 3 * 1024 * 1024;
  static constexpr size_t MAX_CARDS = 20000;

  bool open(const DeckRef& ref);
  // Loads any card file (a word list) without progress.
  bool openFile(const std::string& path);
  const DeckRef& ref() const { return ref_; }
  size_t size() const { return lineStart_.size(); }
  bool card(size_t index, Card& out) const;
  flashcards::CardState& state(size_t index) { return states()[index]; }
  const flashcards::CardState& state(size_t index) const { return states()[index]; }
  const flashcards::CardState* stateArray() const { return states(); }
  bool truncated() const { return truncated_; }

  uint32_t visits() const { return visits_; }
  // Counts a study visit (only matters while the clock is not set) and saves it.
  void startVisit();
  void markChanged() { changed_ = true; }
  bool save();  // writes the progress file when something changed

 private:
  flashcards::CardState* states() const { return reinterpret_cast<flashcards::CardState*>(statesBuf_.get()); }

  DeckRef ref_;
  HalMemory::PsramBuffer text_;
  size_t textLen_ = 0;
  HalMemory::PsramBuffer statesBuf_;
  std::vector<uint32_t> lineStart_;  // byte offset of each card's line in text_
  uint32_t visits_ = 0;
  bool changed_ = false;
  bool truncated_ = false;
};

}  // namespace flashcards
