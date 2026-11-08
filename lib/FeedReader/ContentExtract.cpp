#include "ContentExtract.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

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

bool isContainer(std::string_view html, size_t pos) {  // pos is just after '<' or '</'
  for (const char* name : {"div", "section", "article", "main", "aside", "header", "footer", "nav", "ul", "ol", "table",
                           "form", "blockquote", "figure"}) {
    if (nameAt(html, pos, name)) return true;
  }
  return false;
}

// Characters of visible text outside links, scripts and styles: the measure of "this holds the prose".
size_t proseBytes(std::string_view html) {
  size_t count = 0;
  int inLink = 0;
  for (size_t i = 0; i < html.size();) {
    if (html[i] != '<') {
      if (inLink == 0 && !isspace(static_cast<unsigned char>(html[i]))) ++count;
      ++i;
      continue;
    }
    if (html.compare(i, 4, "<!--") == 0) {
      const size_t end = html.find("-->", i + 4);
      i = end == std::string_view::npos ? html.size() : end + 3;
      continue;
    }
    for (const char* raw : {"script", "style", "noscript", "svg"}) {
      if (nameAt(html, i + 1, raw)) {
        const std::string closer = std::string("</") + raw;
        size_t end = html.find(closer, i + 1);
        end = end == std::string_view::npos ? html.size() : html.find('>', end);
        i = end == std::string_view::npos ? html.size() : end;
        break;
      }
    }
    if (nameAt(html, i + 1, "a")) {
      ++inLink;
    } else if (html.compare(i, 3, "</a") == 0 && nameAt(html, i + 2, "a") && inLink > 0) {
      --inLink;
    }
    const size_t end = html.find('>', i);
    i = end == std::string_view::npos ? html.size() : end + 1;
  }
  return count;
}

// The container elements directly inside `html` (their inner HTML), in order.
std::vector<std::string_view> childContainers(std::string_view html) {
  std::vector<std::string_view> out;
  int depth = 0;
  size_t innerStart = 0;
  for (size_t i = html.find('<'); i != std::string_view::npos; i = html.find('<', i + 1)) {
    if (html.compare(i, 4, "<!--") == 0) {
      const size_t end = html.find("-->", i + 4);
      if (end == std::string_view::npos) break;
      i = end + 2;
      continue;
    }
    bool raw = false;
    for (const char* name : {"script", "style", "noscript", "svg"}) {
      if (nameAt(html, i + 1, name)) {
        const size_t end = html.find(std::string("</") + name, i + 1);
        if (end == std::string_view::npos) return out;
        i = html.find('>', end);
        raw = true;
        break;
      }
    }
    if (raw) {
      if (i == std::string_view::npos) break;
      continue;
    }
    const bool closing = i + 1 < html.size() && html[i + 1] == '/';
    if (!isContainer(html, i + (closing ? 2 : 1))) continue;
    const size_t tagEnd = html.find('>', i);
    if (tagEnd == std::string_view::npos) break;
    if (closing) {
      if (depth > 0 && --depth == 0) out.push_back(html.substr(innerStart, i - innerStart));
    } else if (html[tagEnd - 1] != '/') {
      if (depth++ == 0) innerStart = tagEnd + 1;
    }
  }
  return out;
}

// Walks into the container that holds most of the prose for as long as it holds nearly all of it.
std::string_view densestContainer(std::string_view html) {
  constexpr size_t MIN_PROSE = 400;
  std::string_view region = html;
  for (int level = 0; level < 12; ++level) {
    const size_t total = proseBytes(region);
    std::string_view best;
    size_t bestScore = 0;
    for (std::string_view child : childContainers(region)) {
      const size_t score = proseBytes(child);
      if (score > bestScore) {
        bestScore = score;
        best = child;
      }
    }
    if (best.empty() || bestScore < MIN_PROSE || bestScore * 10 < total * 7) break;
    region = best;
  }
  return region;
}

// Letters and digits only, lower case, with character references dropped whole.
std::string titleKey(std::string_view text) {
  std::string key;
  bool inTag = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '<') {
      inTag = true;
    } else if (c == '>') {
      inTag = false;
    } else if (!inTag && c == '&') {
      const size_t semi = text.find(';', i);
      if (semi != std::string_view::npos && semi - i <= 10) i = semi;
    } else if (!inTag && isalnum(static_cast<unsigned char>(c))) {
      key += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
  }
  return key;
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
  if (found.empty()) found = html;
  return densestContainer(found);
}

std::string_view dropLeadingTitle(std::string_view html, std::string_view title) {
  const std::string want = titleKey(title);
  if (want.size() < 8) return html;
  const std::string_view head = html.substr(0, 2000);
  int headings = 0;
  for (size_t i = head.find('<'); i != std::string_view::npos && headings < 3; i = head.find('<', i + 1)) {
    const bool h1 = nameAt(head, i + 1, "h1");
    if (!h1 && !nameAt(head, i + 1, "h2")) continue;
    ++headings;  // a site name often comes first, so a few headings are tried
    const size_t open = head.find('>', i);
    const size_t close = open == std::string_view::npos ? open : html.find(h1 ? "</h1" : "</h2", open);
    if (close == std::string_view::npos) return html;
    const std::string have = titleKey(html.substr(open + 1, close - open - 1));
    const size_t shorter = std::min(have.size(), want.size());
    const bool same = have == want ||
                      (shorter >= 12 && (have.find(want) != std::string::npos || want.find(have) != std::string::npos));
    const size_t after = html.find('>', close);
    if (same && after != std::string_view::npos) return html.substr(after + 1);
  }
  return html;
}

size_t trimTrailingImages(std::string_view text) {
  constexpr std::string_view PLACEHOLDER = "[image]";
  size_t end = text.size();
  for (;;) {
    size_t e = end;
    while (e > 0 && isspace(static_cast<unsigned char>(text[e - 1]))) --e;
    if (e < PLACEHOLDER.size() || text.compare(e - PLACEHOLDER.size(), PLACEHOLDER.size(), PLACEHOLDER) != 0) break;
    const size_t start = e - PLACEHOLDER.size();
    if (start > 0 && text[start - 1] != '\n') break;  // part of a longer line
    end = start;
  }
  size_t e = end;
  while (e > 0 && isspace(static_cast<unsigned char>(text[e - 1]))) --e;
  return end == text.size() ? text.size() : e;
}

}  // namespace feed
