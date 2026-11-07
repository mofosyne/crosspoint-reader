#include "ContentExtract.h"

#include <cctype>
#include <cstring>
#include <string>

namespace feed {
namespace {

bool nameAt(std::string_view html, size_t pos, const char* name) {  // pos is just after '<'
  const size_t len = strlen(name);
  if (pos + len >= html.size()) return false;
  for (size_t i = 0; i < len; ++i) {
    if (tolower(static_cast<unsigned char>(html[pos + i])) != name[i]) return false;
  }
  const char next = html[pos + len];
  return next == '>' || next == '/' || isspace(static_cast<unsigned char>(next));
}

// Inner HTML of the element opened by the tag starting at `open` (which must name `name`), counting
// nested elements of the same name. Empty when it never closes.
std::string_view innerOf(std::string_view html, size_t open, const char* name) {
  const size_t tagEnd = html.find('>', open);
  if (tagEnd == std::string_view::npos) return {};
  int depth = 1;
  for (size_t i = html.find('<', tagEnd); i != std::string_view::npos; i = html.find('<', i + 1)) {
    if (i + 1 < html.size() && html[i + 1] == '/') {
      if (nameAt(html, i + 2, name) && --depth == 0) return html.substr(tagEnd + 1, i - tagEnd - 1);
    } else if (nameAt(html, i + 1, name)) {
      const size_t end = html.find('>', i);
      if (end != std::string_view::npos && html[end - 1] != '/') ++depth;
    }
  }
  return {};
}

// The largest element with the tag name, among the first `maxTries` occurrences.
std::string_view largest(std::string_view html, const char* name, int maxTries = 8) {
  std::string_view best;
  size_t pos = 0;
  for (int tries = 0; tries < maxTries; ++tries) {
    size_t open = html.find('<', pos);
    while (open != std::string_view::npos && !nameAt(html, open + 1, name)) open = html.find('<', open + 1);
    if (open == std::string_view::npos) break;
    const std::string_view inner = innerOf(html, open, name);
    if (inner.size() > best.size()) best = inner;
    pos = open + 1;
  }
  return best;
}

// Element whose id attribute is `id`, any tag name.
std::string_view byId(std::string_view html, const char* id) {
  const std::string needle1 = std::string("id=\"") + id + "\"";
  const std::string needle2 = std::string("id='") + id + "'";
  size_t at = html.find(needle1);
  if (at == std::string_view::npos) at = html.find(needle2);
  if (at == std::string_view::npos) return {};
  const size_t open = html.rfind('<', at);
  if (open == std::string_view::npos) return {};
  size_t nameEnd = open + 1;
  while (nameEnd < html.size() && isalnum(static_cast<unsigned char>(html[nameEnd]))) ++nameEnd;
  std::string name(html.substr(open + 1, nameEnd - open - 1));
  for (char& c : name) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return innerOf(html, open, name.c_str());
}

}  // namespace

std::string_view extractMainContent(std::string_view html) {
  constexpr size_t MIN_USEFUL = 200;  // a tiny <article> (a teaser card) is not the page
  std::string_view found = largest(html, "article");
  if (found.size() >= MIN_USEFUL) return found;
  found = largest(html, "main", 2);
  if (found.size() >= MIN_USEFUL) return found;
  for (const char* id : {"content", "main", "main-content", "post", "article"}) {
    found = byId(html, id);
    if (found.size() >= MIN_USEFUL) return found;
  }
  found = largest(html, "body", 1);
  return found.empty() ? html : found;
}

}  // namespace feed
