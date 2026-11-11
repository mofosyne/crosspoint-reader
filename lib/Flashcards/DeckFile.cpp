#include "DeckFile.h"

#include <cctype>

namespace flashcards {
namespace {

bool startsWithNoCase(std::string_view s, size_t at, std::string_view word) {
  if (at + word.size() > s.size()) return false;
  for (size_t i = 0; i < word.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s[at + i])) != word[i]) return false;
  }
  return true;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// "amp;" style entity starting after '&'; returns the bytes consumed, 0 when it is not one.
size_t entity(std::string_view s, size_t at, std::string& out) {
  const size_t semi = s.find(';', at);
  if (semi == std::string_view::npos || semi - at > 8) return 0;
  const std::string_view name = s.substr(at, semi - at);
  if (name == "amp")
    out += '&';
  else if (name == "lt")
    out += '<';
  else if (name == "gt")
    out += '>';
  else if (name == "quot")
    out += '"';
  else if (name == "apos")
    out += '\'';
  else if (name == "nbsp")
    out += ' ';
  else if (name.size() > 1 && name[0] == '#') {
    const bool hex = name[1] == 'x' || name[1] == 'X';
    uint32_t cp = 0;
    for (size_t i = hex ? 2 : 1; i < name.size(); ++i) {
      const char c = name[i];
      const int d = std::isdigit(static_cast<unsigned char>(c))           ? c - '0'
                    : hex && std::isxdigit(static_cast<unsigned char>(c)) ? std::tolower(c) - 'a' + 10
                                                                          : -1;
      if (d < 0) return 0;
      cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(d);
    }
    if (cp == 0 || cp > 0x10FFFF) return 0;
    appendUtf8(out, cp);
  } else {
    return 0;
  }
  return semi - at + 1;
}

std::string trimmed(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\n')) ++i;
  return s.substr(i);
}

}  // namespace

std::string plainField(std::string_view in) {
  std::string out;
  for (size_t i = 0; i < in.size();) {
    const char c = in[i];
    if (c == '<') {
      const bool br = startsWithNoCase(in, i + 1, "br");
      const bool block = startsWithNoCase(in, i + 1, "div") || startsWithNoCase(in, i + 1, "/div") ||
                         startsWithNoCase(in, i + 1, "p>") || startsWithNoCase(in, i + 1, "/p>");
      if (br || (block && !out.empty() && out.back() != '\n')) out += '\n';
      const size_t close = in.find('>', i);
      if (close == std::string_view::npos) break;
      i = close + 1;
    } else if (c == '&') {
      const size_t used = entity(in, i + 1, out);
      if (used == 0) {
        out += c;
        ++i;
      } else {
        i += 1 + used;
      }
    } else if (c == '\\' && i + 1 < in.size() && in[i + 1] == 'n') {
      out += '\n';
      i += 2;
    } else if (c == '\r') {
      ++i;
    } else {
      out += c;
      ++i;
    }
  }
  return trimmed(std::move(out));
}

namespace {

bool looksLikeImage(const std::string& s) {
  const auto ends = [&](const char* ext) {
    const size_t n = std::string_view(ext).size();
    if (s.size() <= n) return false;
    for (size_t i = 0; i < n; ++i) {
      const char c = s[s.size() - n + i];
      if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != ext[i]) return false;
    }
    return s.find(' ') == std::string::npos && s.find('\n') == std::string::npos;
  };
  return ends(".bmp") || ends(".png") || ends(".jpg") || ends(".jpeg");
}

}  // namespace

bool parseCardLine(std::string_view line, Card& out) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  std::vector<std::string> fields;
  size_t pos = 0;
  while (true) {
    const size_t tab = line.find('\t', pos);
    fields.push_back(plainField(line.substr(pos, tab == std::string_view::npos ? std::string_view::npos : tab - pos)));
    if (tab == std::string_view::npos) break;
    pos = tab + 1;
  }
  out = Card{};
  if (fields.size() >= 3 && looksLikeImage(fields.back())) {  // the last field is a picture
    out.image = std::move(fields.back());
    fields.pop_back();
  } else if (fields.size() == 2 && looksLikeImage(fields.back())) {
    return false;  // a word with only a picture
  }
  switch (fields.size()) {
    case 0:
    case 1:
      return false;
    case 2:
      out.front = fields[0];
      out.back = fields[1];
      break;
    case 3:
      out.front = fields[0];
      out.reading = fields[1];
      out.back = fields[2];
      break;
    default:
      out.front = fields[0];
      out.reading = fields[1];
      out.back = fields[2];
      out.context = fields[3];
      break;
  }
  return !out.front.empty() && !out.back.empty();
}

DeckParse parseDeck(std::string_view text) {
  DeckParse deck;
  size_t pos = 0;
  size_t lineNo = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    const std::string_view line = text.substr(pos, eol - pos);
    pos = eol + 1;
    ++lineNo;
    if (line.empty() || line.front() == '#' || (line.size() == 1 && line[0] == '\r')) continue;
    Card card;
    if (parseCardLine(line, card)) {
      deck.cards.push_back(std::move(card));
    } else {
      deck.errors.push_back("Line " + std::to_string(lineNo) + ": needs a word and a translation separated by a tab");
    }
  }
  return deck;
}

std::string serializeCard(const Card& card) {
  const auto field = [](const std::string& s) {
    std::string out;
    for (char c : s) {
      if (c == '\t')
        out += ' ';
      else if (c == '\n')
        out += "<br>";
      else if (c != '\r')
        out += c;
    }
    return out;
  };
  std::string line = field(card.front);
  if (card.reading.empty() && card.context.empty()) {
    line += "\t" + field(card.back);
  } else {
    line += "\t" + field(card.reading) + "\t" + field(card.back);
    if (!card.context.empty()) line += "\t" + field(card.context);
  }
  if (!card.image.empty()) line += "\t" + field(card.image);
  line += '\n';
  return line;
}

uint32_t cardKey(std::string_view front) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (const char c : front) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  return h;
}

}  // namespace flashcards
