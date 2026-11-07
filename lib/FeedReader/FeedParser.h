#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "FeedItem.h"

typedef struct XML_ParserStruct* XML_Parser;

namespace feed {

enum class FeedKind : uint8_t { Unknown, Rss, Atom, Sitemap, SitemapIndex };

// Streaming parser for RSS 2.0 / RSS 1.0 / Atom feeds and sitemaps. Feed it the response body in
// chunks; each finished item goes to the sink, so only the item being read is held in memory.
class FeedParser {
 public:
  // Return false from the sink to stop parsing (enough items).
  using Sink = std::function<bool(FeedItem&)>;

  explicit FeedParser(Sink sink, size_t maxSummaryBytes = 16 * 1024);
  ~FeedParser();
  FeedParser(const FeedParser&) = delete;
  FeedParser& operator=(const FeedParser&) = delete;

  // Parses one chunk (`last` on the final one). False after an XML error or when the sink stopped
  // the parse; items delivered before that stay valid. Check stopped() to tell them apart.
  bool feed(const char* data, size_t len, bool last = false);

  FeedKind kind() const { return kind_; }
  const std::string& title() const { return title_; }  // the feed's own title
  bool stopped() const { return stopped_; }
  const std::string& error() const { return error_; }
  size_t itemCount() const { return count_; }

 private:
  static void onStart(void* self, const char* name, const char** attrs);
  static void onEnd(void* self, const char* name);
  static void onText(void* self, const char* text, int len);
  void start(const char* name, const char** attrs);
  void end(const char* name);
  void text(const char* data, int len);
  void finishItem();
  std::string* targetFor(const char* name);

  Sink sink_;
  size_t maxSummary_;
  XML_Parser parser_ = nullptr;
  FeedKind kind_ = FeedKind::Unknown;
  FeedItem item_;
  std::string published_;  // raw date text of the current item (pubDate, published, updated, lastmod)
  std::string updated_;    // Atom <updated>, the fallback date
  std::string desc_;       // RSS <description> / Atom <summary>, the fallback text
  std::string title_;
  std::string error_;
  std::string* capture_ = nullptr;  // where character data goes right now
  int captureDepth_ = 0;
  size_t captureLimit_ = 0;
  int depth_ = 0;
  int itemDepth_ = 0;
  bool inItem_ = false;
  bool inChannelTitleScope_ = true;
  bool stopped_ = false;
  bool failed_ = false;
  size_t count_ = 0;
};

// Convenience for small inputs and tests: all items of `xml`, at most `maxItems`.
bool parseFeed(std::string_view xml, std::vector<FeedItem>& out, size_t maxItems = 200, FeedKind* kind = nullptr);

}  // namespace feed
