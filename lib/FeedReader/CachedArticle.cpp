#include "CachedArticle.h"

#include <cstdio>
#include <cstdlib>

namespace feed {
namespace {

constexpr std::string_view MAGIC = "CPFEED1";

std::string oneLine(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c == '\n' || c == '\r') c = ' ';
  }
  return out;
}

bool put(const ByteSink& sink, std::string_view s) { return sink(s.data(), s.size()); }

}  // namespace

bool serializeArticle(const CachedArticle& a, const ByteSink& sink) {
  std::string header;
  header += MAGIC;
  header += '\n';
  header += "url: " + oneLine(a.url) + '\n';
  header += "title: " + oneLine(a.title) + '\n';
  header += "source: " + oneLine(a.sourceId) + '\n';
  header += "comments: " + oneLine(a.commentsUrl) + '\n';
  header += "published: " + std::to_string(a.published) + '\n';
  header += "fetched: " + std::to_string(a.fetched) + '\n';
  header += std::string("read: ") + (a.read ? "1" : "0") + '\n';
  header += "textlen: " + std::to_string(a.text.size()) + '\n';
  header += "links: " + std::to_string(a.links.size()) + '\n';
  header += '\n';
  if (!put(sink, header) || !put(sink, a.text)) return false;
  for (const auto& link : a.links) {
    if (link.end <= link.start || link.end > a.text.size()) continue;  // a stale range would mislead
    char numbers[48];
    const int n = snprintf(numbers, sizeof(numbers), "\n%u %u ", link.start, link.end);
    if (!sink(numbers, static_cast<size_t>(n)) || !put(sink, oneLine(link.url))) return false;
  }
  return sink("\n", 1);
}

namespace {

// "key: value" lines up to the blank line. Returns the offset just past the blank line, or npos.
size_t parseHeader(std::string_view data, CachedArticle& out, size_t& textLen, size_t& linkCount) {
  if (data.substr(0, MAGIC.size()) != MAGIC || data.size() <= MAGIC.size() || data[MAGIC.size()] != '\n') {
    return std::string_view::npos;
  }
  size_t pos = MAGIC.size() + 1;
  textLen = 0;
  linkCount = 0;
  bool sawLen = false;
  while (pos < data.size()) {
    const size_t eol = data.find('\n', pos);
    if (eol == std::string_view::npos) return std::string_view::npos;
    const std::string_view line = data.substr(pos, eol - pos);
    pos = eol + 1;
    if (line.empty()) return sawLen ? pos : std::string_view::npos;
    const size_t colon = line.find(": ");
    if (colon == std::string_view::npos) continue;  // tolerate unknown lines
    const std::string_view key = line.substr(0, colon);
    const std::string value(line.substr(colon + 2));
    if (key == "url") out.url = value;
    else if (key == "title") out.title = value;
    else if (key == "source") out.sourceId = value;
    else if (key == "comments") out.commentsUrl = value;
    else if (key == "published") out.published = strtoll(value.c_str(), nullptr, 10);
    else if (key == "fetched") out.fetched = strtoll(value.c_str(), nullptr, 10);
    else if (key == "read") out.read = value == "1";
    else if (key == "textlen") {
      textLen = strtoull(value.c_str(), nullptr, 10);
      sawLen = true;
    } else if (key == "links") linkCount = strtoull(value.c_str(), nullptr, 10);
  }
  return std::string_view::npos;
}

}  // namespace

bool parseArticleHeader(std::string_view data, CachedArticle& out) {
  out = CachedArticle{};
  size_t textLen = 0;
  size_t linkCount = 0;
  return parseHeader(data, out, textLen, linkCount) != std::string_view::npos;
}

bool parseArticle(std::string_view data, CachedArticle& out) {
  out = CachedArticle{};
  size_t textLen = 0;
  size_t linkCount = 0;
  const size_t textStart = parseHeader(data, out, textLen, linkCount);
  if (textStart == std::string_view::npos || textStart + textLen > data.size()) return false;
  out.text = data.substr(textStart, textLen);
  size_t pos = textStart + textLen;
  while (pos < data.size() && out.links.size() < linkCount) {
    if (data[pos] == '\n') ++pos;
    const size_t eol = data.find('\n', pos);
    const std::string_view line = data.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
    pos = eol == std::string_view::npos ? data.size() : eol;
    if (line.empty()) continue;
    char* end = nullptr;
    const std::string copy(line);
    const unsigned long start = strtoul(copy.c_str(), &end, 10);
    const unsigned long stop = strtoul(end, &end, 10);
    if (*end != ' ' || stop <= start || stop > textLen) continue;
    out.links.push_back({static_cast<uint32_t>(start), static_cast<uint32_t>(stop), std::string(end + 1)});
  }
  return true;
}

}  // namespace feed
