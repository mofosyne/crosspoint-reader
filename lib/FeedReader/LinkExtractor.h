#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace feed {

// Resolves `ref` (a link as written in a page) against the page's URL `base`. Drops "#fragment".
// Only http and https results are returned; "" for mailto:, javascript:, data: and unusable input.
std::string resolveUrl(std::string_view base, std::string_view ref);

struct PageLink {
  std::string url;   // absolute
  std::string text;  // the link's visible text, tags removed; may be empty
};

// Links on an HTML page, in page order, without repeats. Only links whose absolute URL contains
// `pattern` are kept; an empty pattern keeps every link to the same host as `baseUrl`. A pattern that
// contains "://" may also match other hosts.
std::vector<PageLink> extractLinks(std::string_view html, std::string_view baseUrl, std::string_view pattern,
                                   size_t maxLinks = 200);

// The page's advertised feed: <link rel="alternate" type="application/rss+xml|atom+xml" href>.
// Absolute URL, or "" when there is none.
std::string findFeedLink(std::string_view html, std::string_view baseUrl);

// Text of <title>, entities decoded and whitespace collapsed; "" when absent.
std::string pageTitle(std::string_view html);

}  // namespace feed
