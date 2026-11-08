#include "HtmlToPlainText.h"

#include <Epub/htmlEntities.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {

bool isTagStart(std::string_view input, size_t pos) {
  if (pos + 1 >= input.size()) return false;
  const unsigned char next = input[pos + 1];
  return next == '/' || next == '!' || next == '?' || std::isalpha(next);
}

enum class TagBreak : uint8_t { None, Line, Paragraph, HeadingOpen, HeadingClose, HeaderCellClose, DataCellClose };

TagBreak tagBreak(std::string_view input, size_t start, size_t end) {
  // A closing tag is marked by the '/' the name scan below skips.
  bool closing = false;
  while (start < end && (input[start] == '/' || std::isspace(static_cast<unsigned char>(input[start])))) {
    if (input[start] == '/') closing = true;
    start++;
  }
  const size_t nameStart = start;
  // Alphanumeric, not alphabetic: a name scan that stops at the first digit
  // reads "h1" as "h", so none of the heading names below could ever match and
  // a definition's headings ran into the text that followed them.
  while (start < end && std::isalnum(static_cast<unsigned char>(input[start]))) start++;
  const size_t len = start - nameStart;
  if (len == 0) return TagBreak::None;

  const auto equals = [&](const char* name) {
    const size_t nameLen = strlen(name);
    if (len != nameLen) return false;
    for (size_t i = 0; i < len; i++) {
      if (std::tolower(static_cast<unsigned char>(input[nameStart + i])) != name[i]) return false;
    }
    return true;
  };

  if (equals("h1") || equals("h2") || equals("h3") || equals("h4") || equals("h5") || equals("h6")) {
    // A heading keeps its role in text: it is a label line of its own, drawn in
    // bold by the plain-text path.
    return closing ? TagBreak::HeadingClose : TagBreak::HeadingOpen;
  }
  if (closing && equals("th")) return TagBreak::HeaderCellClose;
  if (closing && equals("td")) return TagBreak::DataCellClose;
  if (equals("p") || equals("hr")) {
    return TagBreak::Paragraph;
  }
  if (equals("br") || equals("div") || equals("li") || equals("tr")) return TagBreak::Line;
  return TagBreak::None;
}

// Elements whose content is not reading text: the document head (title, inline CSS), styles
// and scripts. Returns the lower-case name of an opening tag, or nullptr for any other tag.
const char* hiddenElementName(std::string_view input, size_t start, size_t end) {
  if (start >= end || input[start] == '/' || input[end - 1] == '/') return nullptr;
  size_t nameEnd = start;
  while (nameEnd < end && std::isalnum(static_cast<unsigned char>(input[nameEnd]))) nameEnd++;
  const size_t len = nameEnd - start;
  for (const char* name : {"head", "style", "script"}) {
    if (strlen(name) != len) continue;
    size_t i = 0;
    while (i < len && std::tolower(static_cast<unsigned char>(input[start + i])) == name[i]) i++;
    if (i == len) return name;
  }
  return nullptr;
}

// Index just past the closing tag of `name` that follows `from`, or input.size() when absent.
size_t skipPastClosingTag(std::string_view input, size_t from, const char* name) {
  const size_t len = strlen(name);
  for (size_t i = input.find("</", from); i != std::string::npos; i = input.find("</", i + 2)) {
    size_t j = 0;
    while (j < len && i + 2 + j < input.size() &&
           std::tolower(static_cast<unsigned char>(input[i + 2 + j])) == name[j]) {
      j++;
    }
    if (j != len) continue;
    const size_t close = input.find('>', i + 2 + len);
    return close == std::string::npos ? input.size() : close + 1;
  }
  return input.size();
}

// True when the opening tag whose text spans [start, end) is named `name` (case-insensitive).
bool isOpeningTag(std::string_view input, size_t start, size_t end, const char* name) {
  const size_t len = strlen(name);
  if (start + len > end) return false;
  for (size_t i = 0; i < len; i++) {
    if (std::tolower(static_cast<unsigned char>(input[start + i])) != name[i]) return false;
  }
  return start + len == end || !std::isalnum(static_cast<unsigned char>(input[start + len]));
}

// Value of attribute `name` inside the tag text [start, end), or an empty view. Handles quoted
// and bare values. A longer name that only ends in `name` (data-alt) does not match.
std::string_view attributeValue(std::string_view input, size_t start, size_t end, const char* name) {
  const size_t nameLen = strlen(name);
  size_t pos = start;
  while (pos < end && std::isalnum(static_cast<unsigned char>(input[pos]))) pos++;  // skip the tag name
  while (pos < end) {
    while (pos < end && (std::isspace(static_cast<unsigned char>(input[pos])) || input[pos] == '/')) pos++;
    const size_t attrStart = pos;
    while (pos < end && !std::isspace(static_cast<unsigned char>(input[pos])) && input[pos] != '=' &&
           input[pos] != '/') {
      pos++;
    }
    const size_t attrLen = pos - attrStart;
    if (pos >= end || input[pos] != '=') continue;
    pos++;
    size_t valueStart = pos;
    size_t valueEnd;
    if (pos < end && (input[pos] == '"' || input[pos] == '\'')) {
      const char quote = input[pos++];
      valueStart = pos;
      while (pos < end && input[pos] != quote) pos++;
      valueEnd = pos;
      if (pos < end) pos++;
    } else {
      while (pos < end && !std::isspace(static_cast<unsigned char>(input[pos]))) pos++;
      valueEnd = pos;
    }
    if (attrLen != nameLen) continue;
    bool same = true;
    for (size_t i = 0; i < nameLen && same; i++) {
      same = std::tolower(static_cast<unsigned char>(input[attrStart + i])) == name[i];
    }
    if (same) return std::string_view(input.data() + valueStart, valueEnd - valueStart);
  }
  return {};
}

// Footnote markers and "citation needed" tags are reading noise in plain text.
bool isNoiseSuperscript(std::string_view input, size_t start, size_t end) {
  const std::string_view cls = attributeValue(input, start, end, "class");
  return cls.find("reference") != std::string_view::npos || cls.find("noprint") != std::string_view::npos;
}

// Images at or under this size (icons, bullets) get no placeholder when they have no alt text.
constexpr unsigned long TINY_IMAGE_PIXELS = 24;

bool isTinyImage(std::string_view input, size_t start, size_t end) {
  for (const char* name : {"width", "height"}) {
    const std::string_view value = attributeValue(input, start, end, name);
    if (value.empty()) continue;
    char* stop = nullptr;
    const std::string copy(value);
    const unsigned long pixels = strtoul(copy.c_str(), &stop, 10);
    if (stop != copy.c_str() && pixels <= TINY_IMAGE_PIXELS) return true;
  }
  return false;
}

size_t utf8CodepointCount(const std::string& text) {
  size_t count = 0;
  for (const unsigned char c : text) count += (c & 0xC0) != 0x80;
  return count;
}

// Alt text longer than this is cut to a plain placeholder: it is almost never a useful caption.
constexpr size_t MAX_ALT_BYTES = 300;
constexpr size_t MAX_HREF_BYTES = 400;

// Placeholder for an <img> tag spanning [tagStart, tagEnd]. Returns the text to write, or an
// empty string for none. The caller checks it fits in the bytes the tag occupied.
std::string imagePlaceholder(std::string_view input, size_t tagStart, size_t tagEnd, char headingMarker) {
  const std::string_view alt = attributeValue(input, tagStart + 1, tagEnd, "alt");
  if (alt.empty() || alt.size() > MAX_ALT_BYTES) {
    return isTinyImage(input, tagStart + 1, tagEnd) ? std::string() : std::string("[image]");
  }
  std::string text = htmlToPlainText(std::string(alt), headingMarker);  // decodes entities
  while (!text.empty() && text.back() == ' ') text.pop_back();
  if (text.empty() || text.find('\n') != std::string::npos) return "[image]";
  constexpr char MATH_PREFIX[] = "{\\displaystyle";
  if (text.compare(0, sizeof(MATH_PREFIX) - 1, MATH_PREFIX) == 0) {
    size_t from = sizeof(MATH_PREFIX) - 1;
    while (from < text.size() && text[from] == ' ') from++;
    size_t to = text.size();
    if (to > from && text[to - 1] == '}') to--;
    return "[formula: " + text.substr(from, to - from) + "]";
  }
  if (utf8CodepointCount(text) == 1) return text;  // a symbol image such as a moon sign
  return "[image: " + text + "]";
}

// Index just past the closing tag that matches the element opened before `from`, counting nested
// elements of the same name. input.size() when it never closes.
size_t skipElement(std::string_view input, size_t from, const char* name) {
  const size_t len = strlen(name);
  int depth = 1;
  for (size_t i = input.find('<', from); i != std::string_view::npos; i = input.find('<', i + 1)) {
    const bool closing = i + 1 < input.size() && input[i + 1] == '/';
    const size_t nameStart = i + (closing ? 2 : 1);
    if (nameStart + len > input.size()) break;
    bool same = true;
    for (size_t k = 0; k < len && same; k++) {
      same = std::tolower(static_cast<unsigned char>(input[nameStart + k])) == name[k];
    }
    if (!same || (nameStart + len < input.size() && std::isalnum(static_cast<unsigned char>(input[nameStart + len])))) {
      continue;
    }
    const size_t close = input.find('>', nameStart + len);
    if (close == std::string_view::npos) break;
    if (closing) {
      if (--depth == 0) return close + 1;
    } else if (input[close - 1] != '/') {
      depth++;
    }
  }
  return input.size();
}

// Elements that are navigation or page furniture rather than reading text, found by class:
// navigation boxes, reference lists, edit links and inline footnote markers.
const char* noiseElementName(std::string_view input, size_t start, size_t end, bool dropPageChrome) {
  if (dropPageChrome) {
    for (const char* name : {"nav", "aside", "footer", "form", "button", "svg", "noscript", "iframe"}) {
      if (isOpeningTag(input, start, end, name)) return name;
    }
  }
  for (const char* name : {"div", "table", "span", "ul", "section", "nav", "aside"}) {
    if (!isOpeningTag(input, start, end, name)) continue;
    const std::string_view cls = attributeValue(input, start, end, "class");
    for (const char* token : {"navbox", "reflist", "mw-editsection", "mw-ref"}) {
      if (cls.find(token) != std::string_view::npos) return name;
    }
    return nullptr;
  }
  return nullptr;
}

// True when `<sup>` at [from, ...) holds some text. Some archives keep only an empty link there
// (the exponent was a style), and a lone "^" would be misleading.
bool superscriptHasText(std::string_view input, size_t from) {
  constexpr size_t SCAN_LIMIT = 200;
  const size_t stop = std::min(input.size(), from + SCAN_LIMIT);
  bool inTag = false;
  for (size_t i = from; i < stop; i++) {
    const char c = input[i];
    if (c == '<') {
      if (input.compare(i, 5, "</sup") == 0) return false;
      inTag = true;
    } else if (c == '>') {
      inTag = false;
    } else if (!inTag && !std::isspace(static_cast<unsigned char>(c))) {
      return true;
    }
  }
  return true;  // unknown: keep the caret
}

// First non-space byte at or after `pos` begins an opening <td> or <th>.
bool nextIsCell(std::string_view input, size_t pos, const char* name) {
  while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos]))) pos++;
  return pos < input.size() && input[pos] == '<' && isOpeningTag(input, pos + 1, input.size(), name);
}

void appendCodepoint(char* output, size_t& written, uint32_t codepoint) {
  if (codepoint == 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return;
  if (codepoint <= 0x7F) {
    output[written++] = static_cast<char>(codepoint);
  } else if (codepoint <= 0x7FF) {
    output[written++] = static_cast<char>(0xC0 | (codepoint >> 6));
    output[written++] = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    output[written++] = static_cast<char>(0xE0 | (codepoint >> 12));
    output[written++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    output[written++] = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    output[written++] = static_cast<char>(0xF0 | (codepoint >> 18));
    output[written++] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    output[written++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    output[written++] = static_cast<char>(0x80 | (codepoint & 0x3F));
  }
}

bool appendNumericEntity(char* output, size_t& written, const char* entity, size_t len) {
  if (len < 4 || entity[0] != '&' || entity[1] != '#' || entity[len - 1] != ';') return false;
  size_t pos = 2;
  uint32_t base = 10;
  if (entity[pos] == 'x' || entity[pos] == 'X') {
    base = 16;
    pos++;
  }
  if (pos == len - 1) return false;

  uint32_t value = 0;
  for (; pos < len - 1; pos++) {
    const unsigned char c = entity[pos];
    uint32_t digit;
    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (base == 16 && c >= 'a' && c <= 'f')
      digit = c - 'a' + 10;
    else if (base == 16 && c >= 'A' && c <= 'F')
      digit = c - 'A' + 10;
    else
      return false;
    if (value > (0x10FFFF - digit) / base) return false;
    value = value * base + digit;
  }
  appendCodepoint(output, written, value);
  return true;
}

void appendBreak(char* output, size_t& written, size_t count = 1) {
  while (written > 0 && output[written - 1] == ' ') written--;
  if (written == 0) return;
  size_t existing = 0;
  while (existing < written && output[written - existing - 1] == '\n') existing++;
  while (existing++ < count) output[written++] = '\n';
}

}  // namespace

size_t htmlToPlainTextInPlace(char* buffer, const size_t size, const char headingMarker, LinkSink* links,
                              const bool dropPageChrome) {
  // Compact into consumed input bytes; replacements never expand the source. `html` reads ahead
  // of `written`, and both view the same buffer.
  const std::string_view html(buffer, size);
  size_t written = 0;
  std::string linkHref;  // copied at the opening tag: the text it came from is overwritten later
  size_t linkStart = 0;
  bool linkOpen = false;
  int preDepth = 0;  // inside <pre>, where web pages keep their line breaks and spacing

  for (size_t i = 0; i < html.size();) {
    if (html[i] == '<' && isTagStart(html, i)) {
      const size_t close = html.find('>', i + 1);
      if (close == std::string::npos) {
        buffer[written++] = html[i++];
        continue;
      }
      if (const char* hidden = hiddenElementName(html, i + 1, close)) {
        i = skipPastClosingTag(html, close + 1, hidden);
        continue;
      }
      if (isOpeningTag(html, i + 1, close, "pre")) {
        ++preDepth;
      } else if (close > i + 1 && html[i + 1] == '/' && isOpeningTag(html, i + 2, close, "pre") && preDepth > 0) {
        --preDepth;
      }
      if (links && isOpeningTag(html, i + 1, close, "a")) {
        const std::string_view href = attributeValue(html, i + 1, close, "href");
        linkOpen = !href.empty() && href.size() <= MAX_HREF_BYTES;
        if (linkOpen) {
          linkHref.assign(href);
          linkStart = written;
        }
      } else if (links && linkOpen && close > i + 1 && html[i + 1] == '/' && isOpeningTag(html, i + 2, close, "a")) {
        links->onLink(std::min(linkStart, written), written, linkHref);
        linkOpen = false;
      }
      if (const char* noise = noiseElementName(html, i + 1, close, dropPageChrome)) {
        i = skipElement(html, close + 1, noise);
        continue;
      }
      if (isOpeningTag(html, i + 1, close, "img")) {
        std::string text = imagePlaceholder(html, i, close, headingMarker);
        if (text.size() > close + 1 - written) text = close + 1 - written >= 7 ? "[image]" : std::string();
        memcpy(buffer + written, text.data(), text.size());
        written += text.size();
        i = close + 1;
        continue;
      }
      if (isOpeningTag(html, i + 1, close, "sup")) {
        if (isNoiseSuperscript(html, i + 1, close)) {
          i = skipPastClosingTag(html, close + 1, "sup");
        } else {
          if (superscriptHasText(html, close + 1)) buffer[written++] = '^';
          i = close + 1;
        }
        continue;
      }
      const TagBreak separator = tagBreak(html, i + 1, close);
      if (separator == TagBreak::Line) {
        appendBreak(buffer, written);
      } else if (separator == TagBreak::Paragraph) {
        appendBreak(buffer, written, 2);
      } else if (separator == TagBreak::HeadingOpen) {
        appendBreak(buffer, written);
        buffer[written++] = headingMarker;
      } else if (separator == TagBreak::HeadingClose) {
        appendBreak(buffer, written);
      } else if (separator == TagBreak::HeaderCellClose || separator == TagBreak::DataCellClose) {
        // A label cell followed by a value cell reads "Label: value"; other cells are set apart.
        const char* text = nullptr;
        if (separator == TagBreak::HeaderCellClose) {
          text = nextIsCell(html, close + 1, "td") ? ": " : " ";
        } else if (nextIsCell(html, close + 1, "td") || nextIsCell(html, close + 1, "th")) {
          text = " | ";
        }
        // A block inside the label cell (<div>, <p>) already ended its line; pull the value back up.
        if (separator == TagBreak::HeaderCellClose && text && text[0] == ':') {
          while (written > 0 && (buffer[written - 1] == '\n' || buffer[written - 1] == ' ')) written--;
        }
        if (text && written > 0 && buffer[written - 1] != '\n') {
          const size_t n = strlen(text);
          if (written + n <= close + 1) {
            memcpy(buffer + written, text, n);
            written += n;
          }
        }
      }
      i = close + 1;
      continue;
    }

    if (html[i] == '&') {
      const size_t semicolon = html.find(';', i + 1);
      if (semicolon != std::string::npos && semicolon - i <= 16) {
        const size_t len = semicolon - i + 1;
        const char* value = lookupHtmlEntity(html.data() + i, len);
        const size_t valueLen = value ? strlen(value) : 0;
        if (value != nullptr && valueLen <= len) {
          memcpy(buffer + written, value, valueLen);
          written += valueLen;
          i = semicolon + 1;
          continue;
        }
        if (appendNumericEntity(buffer, written, html.data() + i, len)) {
          i = semicolon + 1;
          continue;
        }
      }
    }

    const unsigned char c = html[i++];
    if (dropPageChrome && preDepth == 0 && (c == '\r' || c == '\t' || c == '\n' || c == ' ')) {
      // Web pages follow the HTML rule: any run of whitespace is one space, and none starts a line.
      if (written > 0 && buffer[written - 1] != ' ' && buffer[written - 1] != '\n' &&
          buffer[written - 1] != headingMarker) {
        buffer[written++] = ' ';
      }
    } else if (c == '\r' || c == '\t') {
      if (written > 0 && buffer[written - 1] != ' ' && buffer[written - 1] != '\n') buffer[written++] = ' ';
    } else if (c == '\n') {
      appendBreak(buffer, written);
    } else {
      buffer[written++] = static_cast<char>(c);
    }
  }

  while (written > 0 && (buffer[written - 1] == ' ' || buffer[written - 1] == '\n')) written--;
  return written;
}

std::string htmlToPlainText(std::string html, const char headingMarker) {
  html.resize(htmlToPlainTextInPlace(html.data(), html.size(), headingMarker, nullptr));
  return html;
}
