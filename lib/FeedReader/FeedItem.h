#pragma once

#include <cstdint>
#include <string>

namespace feed {

// One entry found by a source: an RSS or Atom item, a sitemap URL, or a link on an index page.
struct FeedItem {
  std::string url;          // the page to read
  std::string title;        // may be empty (sitemaps)
  std::string summary;      // text or HTML the feed carried; may be short or empty
  std::string commentsUrl;  // RSS <comments>; empty when the feed has none
  std::string guid;         // stable id when the feed gives one, else empty
  int64_t published = 0;    // Unix seconds, UTC; 0 when unknown
  bool isSitemap = false;   // an entry of a sitemap index: `url` is another sitemap
};

}  // namespace feed
