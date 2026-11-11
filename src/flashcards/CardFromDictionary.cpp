#include "CardFromDictionary.h"

#include "util/Dictionary.h"

namespace flashcards {
namespace {
constexpr size_t MAX_MEANING_BYTES = 480;
}

Card cardFromDefinition(const std::string& headword, const std::string& definition, const bool html) {
  const std::string text = html ? plainField(definition) : definition;
  std::string cleaned;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == DICTIONARY_PLAIN_HEADWORD_MARKER || c == '\r') continue;
    if (c == '\xE2' && i + 2 < text.size() && text[i + 1] == '\x80' &&
        (text[i + 2] == '\xA8' || text[i + 2] == '\xA9')) {  // U+2028 and U+2029 separate senses; fonts lack them
      cleaned += '\n';
      i += 2;
    } else if (c == '\v' || c == '\f') {
      cleaned += '\n';
    } else if (static_cast<unsigned char>(c) < 0x20 && c != '\n') {
      cleaned += ' ';
    } else {
      cleaned += c;
    }
  }
  if (cleaned.compare(0, headword.size(), headword) == 0) {  // the first line repeats the word
    const size_t nl = cleaned.find('\n');
    cleaned = nl == std::string::npos ? std::string() : cleaned.substr(nl + 1);
  }
  while (!cleaned.empty() && (cleaned.front() == '\n' || cleaned.front() == ' ')) cleaned.erase(0, 1);
  if (cleaned.size() > MAX_MEANING_BYTES) {
    size_t cut = MAX_MEANING_BYTES;
    while (cut > 0 && (static_cast<unsigned char>(cleaned[cut]) & 0xC0) == 0x80) --cut;  // not inside a character
    const size_t nl = cleaned.rfind('\n', cut);
    if (nl != std::string::npos && nl > MAX_MEANING_BYTES / 2) cut = nl;
    cleaned = cleaned.substr(0, cut) + " ...";
  }
  Card card;
  card.front = headword;
  card.back = cleaned;
  return card;
}

}  // namespace flashcards
