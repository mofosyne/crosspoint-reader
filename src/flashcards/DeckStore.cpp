#include "DeckStore.h"

#include <Calendar.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace flashcards {
namespace {

constexpr const char* TAG = "CARDS";

std::string cardsPath(const std::string& folder) { return folder + "/cards.tsv"; }
std::string progressPath(const std::string& folder) { return folder + "/progress.txt"; }

}  // namespace

std::vector<std::string> listWordLists() {
  std::vector<std::string> names;
  HalFile root = Storage.open(WORDLISTS_ROOT);
  if (!root || !root.isDirectory()) return names;
  char name[96];
  for (HalFile f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    f.getName(name, sizeof(name));
    const std::string file = name;
    if (file.size() > 4 && file.compare(file.size() - 4, 4, ".tsv") == 0 && file[0] != '.') {
      names.push_back(file.substr(0, file.size() - 4));
    }
    if (names.size() >= 40) break;
  }
  std::sort(names.begin(), names.end());
  return names;
}

static std::string knownPath(const std::string& list) { return std::string(WORDLISTS_ROOT) + "/" + list + ".known"; }

std::vector<uint32_t> loadKnownWords(const std::string& listName) {
  std::vector<uint32_t> keys;
  std::string text;
  if (!Storage.readFileToString(TAG, knownPath(listName), 512 * 1024, text)) return keys;
  for (size_t pos = 0; pos < text.size();) {
    keys.push_back(static_cast<uint32_t>(strtoul(text.c_str() + pos, nullptr, 16)));
    const size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  return keys;
}

bool addKnownWord(const std::string& listName, uint32_t key) {
  HalFile f = Storage.open(knownPath(listName).c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!f) return false;
  char line[16];
  const int n = snprintf(line, sizeof(line), "%08x\n", static_cast<unsigned>(key));
  const bool ok = f.write(line, static_cast<size_t>(n)) == static_cast<size_t>(n);
  f.close();
  return ok;
}

std::vector<uint32_t> ownedWords(const std::string& listName) {
  std::vector<uint32_t> keys = loadKnownWords(listName);
  for (const auto& ref : listDecks()) {
    Deck deck;
    if (!deck.open(ref)) continue;
    for (size_t i = 0; i < deck.size(); ++i) keys.push_back(deck.state(i).key);
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  return keys;
}

std::vector<DeckRef> listDecks() {
  std::vector<DeckRef> decks;
  HalFile root = Storage.open(CARDS_ROOT);
  if (!root || !root.isDirectory()) return decks;
  char name[64];
  for (HalFile f = root.openNextFile(); f; f = root.openNextFile()) {
    if (!f.isDirectory()) continue;
    f.getName(name, sizeof(name));
    if (name[0] == '.') continue;
    DeckRef ref{name, std::string(CARDS_ROOT) + "/" + name};
    if (Storage.exists(cardsPath(ref.folder).c_str())) decks.push_back(std::move(ref));
    if (decks.size() >= 32) break;
  }
  std::sort(decks.begin(), decks.end(), [](const DeckRef& a, const DeckRef& b) { return a.name < b.name; });
  return decks;
}

std::string cleanDeckName(std::string_view name) {
  std::string out;
  for (const char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || (c == ' ' && !out.empty())) out += c;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out.substr(0, 40);
}

bool appendCard(std::string_view deckName, const Card& card) {
  const std::string name = cleanDeckName(deckName);
  if (name.empty() || card.front.empty() || card.back.empty()) return false;
  const std::string folder = std::string(CARDS_ROOT) + "/" + name;
  Storage.ensureDirectoryExists(CARDS_ROOT);
  Storage.ensureDirectoryExists(folder.c_str());
  HalFile f = Storage.open(cardsPath(folder).c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!f) return false;
  const std::string line = serializeCard(card);
  const bool ok = f.write(line.data(), line.size()) == line.size();
  f.close();
  return ok;
}

bool clockIsSet() { return time(nullptr) > 1700000000; }

int32_t today(uint32_t visitCounter) {
  const time_t now = time(nullptr);
  if (now <= 1700000000) return static_cast<int32_t>(visitCounter);
  struct tm local;
  localtime_r(&now, &local);
  return feed::daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

bool Deck::open(const DeckRef& ref) {
  ref_ = ref;
  if (!openFile(cardsPath(ref.folder))) return false;
  std::string progress;
  if (Storage.readFileToString(TAG, progressPath(ref.folder), 4 * 1024 * 1024, progress)) {
    applyProgress(progress, states(), lineStart_.size());
    unsigned visits = 0;
    if (progress.rfind("visits ", 0) == 0 && sscanf(progress.c_str(), "visits %u", &visits) == 1) visits_ = visits;
  }
  return true;
}

bool Deck::openFile(const std::string& path) {
  lineStart_.clear();
  changed_ = false;
  truncated_ = false;
  visits_ = 0;
  HalFile f;
  if (!Storage.openFileForRead(TAG, path, f)) return false;
  size_t size = f.fileSize();
  if (size > MAX_BYTES) {
    size = MAX_BYTES;
    truncated_ = true;
  }
  text_ = HalMemory::allocatePsram(size + 1);
  if (!text_) {
    LOG_ERR(TAG, "PSRAM short for a %u byte deck", static_cast<unsigned>(size));
    return false;
  }
  textLen_ = 0;
  while (textLen_ < size) {
    const int n = f.read(text_.get() + textLen_, std::min<size_t>(4096, size - textLen_));
    if (n <= 0) break;
    textLen_ += static_cast<size_t>(n);
  }
  f.close();
  text_[textLen_] = '\0';

  // Index the card lines (the others are comments and blanks) and give every card its key.
  std::vector<uint32_t> keys;
  const char* text = reinterpret_cast<const char*>(text_.get());
  for (size_t pos = 0; pos < textLen_ && lineStart_.size() < MAX_CARDS;) {
    const char* nl = static_cast<const char*>(memchr(text + pos, '\n', textLen_ - pos));
    const size_t end = nl ? static_cast<size_t>(nl - text) : textLen_;
    Card c;
    if (end > pos && text[pos] != '#' && parseCardLine(std::string_view(text + pos, end - pos), c)) {
      lineStart_.push_back(static_cast<uint32_t>(pos));
      keys.push_back(cardKey(c.front));
    }
    pos = end + 1;
  }
  if (lineStart_.size() >= MAX_CARDS) truncated_ = true;

  statesBuf_ = HalMemory::allocatePsram(std::max<size_t>(1, keys.size()) * sizeof(CardState));
  if (!statesBuf_) return false;
  for (size_t i = 0; i < keys.size(); ++i) {
    new (&states()[i]) CardState();
    states()[i].key = keys[i];
  }
  return true;
}

bool Deck::card(size_t index, Card& out) const {
  if (index >= lineStart_.size()) return false;
  const char* text = reinterpret_cast<const char*>(text_.get());
  const size_t start = lineStart_[index];
  const char* nl = static_cast<const char*>(memchr(text + start, '\n', textLen_ - start));
  const size_t end = nl ? static_cast<size_t>(nl - text) : textLen_;
  return parseCardLine(std::string_view(text + start, end - start), out);
}

void Deck::startVisit() {
  ++visits_;
  changed_ = true;
}

bool Deck::save() {
  if (!changed_) return true;
  const std::string body = "visits " + std::to_string(visits_) + "\n" + serializeProgress(states(), lineStart_.size());
  const std::string path = progressPath(ref_.folder);
  const std::string tmp = path + ".tmp";
  HalFile f;
  if (!Storage.openFileForWrite(TAG, tmp, f)) return false;
  const bool ok = f.write(body.data(), body.size()) == body.size();
  f.close();
  if (!ok || !Storage.replaceFile(tmp.c_str(), path.c_str())) {
    Storage.remove(tmp.c_str());
    return false;
  }
  changed_ = false;
  return true;
}

}  // namespace flashcards
