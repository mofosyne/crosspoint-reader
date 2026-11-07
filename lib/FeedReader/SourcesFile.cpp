#include "SourcesFile.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace feed {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Splits a line into words; "double quoted" text (also after key=) stays one word.
std::vector<std::string> words(std::string_view line) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && isSpace(line[i])) ++i;
    if (i >= line.size()) break;
    std::string word;
    bool quoted = false;
    while (i < line.size() && (quoted || !isSpace(line[i]))) {
      if (line[i] == '"') {
        quoted = !quoted;
      } else {
        word += line[i];
      }
      ++i;
    }
    out.push_back(std::move(word));
  }
  return out;
}

std::string hostOf(const std::string& url) {
  const size_t scheme = url.find("://");
  const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
  const size_t end = url.find_first_of("/?#", start);
  return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool parseInt(const std::string& text, int minValue, int maxValue, int& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  const long v = strtol(text.c_str(), &end, 10);
  if (*end != '\0' || v < minValue || v > maxValue) return false;
  out = static_cast<int>(v);
  return true;
}

}  // namespace

std::string sourceId(std::string_view url) {
  uint32_t h = 2166136261u;
  for (const unsigned char c : url) {
    h ^= c;
    h *= 16777619u;
  }
  char buf[9];
  snprintf(buf, sizeof(buf), "%08x", h);
  return buf;
}

SourcesParse parseSources(std::string_view text) {
  SourcesParse result;
  int lineNo = 0;
  size_t pos = 0;
  while (pos <= text.size()) {
    const size_t eol = text.find('\n', pos);
    std::string_view line = text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
    pos = eol == std::string_view::npos ? text.size() + 1 : eol + 1;
    ++lineNo;
    const size_t hash = line.find('#');
    if (hash != std::string_view::npos) line = line.substr(0, hash);  // a comment, also after options
    const std::vector<std::string> w = words(line);
    if (w.empty()) continue;
    const auto fail = [&](const std::string& why) { result.errors.push_back("line " + std::to_string(lineNo) + ": " + why); };

    Source s;
    if (w[0] == "feed") s.kind = SourceKind::Feed;
    else if (w[0] == "site") s.kind = SourceKind::Site;
    else if (w[0] == "sitemap") s.kind = SourceKind::Sitemap;
    else {
      fail("unknown source type '" + w[0] + "' (use feed, site or sitemap)");
      continue;
    }
    if (w.size() < 2 || (w[1].rfind("http://", 0) != 0 && w[1].rfind("https://", 0) != 0)) {
      fail("expected an http(s) URL after '" + w[0] + "'");
      continue;
    }
    s.url = w[1];
    bool bad = false;
    for (size_t i = 2; i < w.size() && !bad; ++i) {
      const size_t eq = w[i].find('=');
      if (eq == std::string::npos) {
        fail("option '" + w[i] + "' needs a value (key=value)");
        bad = true;
        break;
      }
      const std::string key = w[i].substr(0, eq);
      const std::string value = w[i].substr(eq + 1);
      if (key == "every") {
        if (!parseInt(value, 0, 365, s.everyDays)) fail("every= wants 0 to 365 days"), bad = true;
      } else if (key == "limit") {
        if (!parseInt(value, 1, 200, s.limit)) fail("limit= wants 1 to 200"), bad = true;
      } else if (key == "content") {
        if (value == "auto") s.content = ContentMode::Auto;
        else if (value == "feed") s.content = ContentMode::Feed;
        else if (value == "page") s.content = ContentMode::Page;
        else fail("content= wants auto, feed or page"), bad = true;
      } else if (key == "comments") {
        if (value == "none") s.comments = CommentsMode::None;
        else if (value == "link") s.comments = CommentsMode::Link;
        else fail("comments= wants none or link"), bad = true;
      } else if (key == "links") {
        s.linkPattern = value;
      } else if (key == "name") {
        s.name = value;
      } else {
        fail("unknown option '" + key + "'");
        bad = true;
      }
    }
    if (bad) continue;
    if (s.name.empty()) s.name = hostOf(s.url);
    s.id = sourceId(s.url);
    result.sources.push_back(std::move(s));
  }
  return result;
}

bool isDue(const Source& source, int64_t lastCheck, int64_t now) {
  if (source.everyDays <= 0 || lastCheck <= 0 || now <= 0) return true;
  return now - lastCheck >= static_cast<int64_t>(source.everyDays) * 86400 - 3600;
}

}  // namespace feed
