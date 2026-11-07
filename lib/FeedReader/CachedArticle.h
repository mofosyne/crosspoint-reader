#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace feed {

// One article saved on the SD card by a sync: where it came from, its converted text and the links in
// that text. The file is a short header of "key: value" lines, a blank line, `textLen` bytes of text,
// then one "start end url" line per link. Parsing is zero-copy over a buffer the caller owns (a PSRAM
// copy of the file on the device), so a large article never needs the internal heap.
struct CachedArticle {
  struct Link {
    uint32_t start = 0;  // byte range of the link text inside `text`
    uint32_t end = 0;
    std::string url;  // absolute
  };

  std::string url;
  std::string title;
  std::string sourceId;
  std::string commentsUrl;
  int64_t published = 0;  // 0 = unknown
  int64_t fetched = 0;    // when the sync saved it
  bool read = false;
  std::string_view text;  // points into the parsed buffer; the serializer takes its own text
  std::vector<Link> links;
};

using ByteSink = std::function<bool(const char* data, size_t len)>;

// Writes `article` (text taken from article.text) through `sink`. False when the sink fails.
bool serializeArticle(const CachedArticle& article, const ByteSink& sink);

// Reads an article from the whole file in `data`. `out.text` points into `data`.
bool parseArticle(std::string_view data, CachedArticle& out);

// Reads only the header of a file (a list view needs no text); `out.text` stays empty and no links are read.
// `data` may be just the first few KB of the file.
bool parseArticleHeader(std::string_view data, CachedArticle& out);

}  // namespace feed
