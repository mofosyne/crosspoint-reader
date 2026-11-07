#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace feed {

enum class SourceKind : uint8_t { Feed, Site, Sitemap };
enum class ContentMode : uint8_t { Auto, Feed, Page };  // where an item's text comes from
enum class CommentsMode : uint8_t { None, Link };       // Link: also fetch the item's comments page

// One line of the sources file, for example:
//   feed    https://news.ycombinator.com/rss  every=1 content=auto comments=link limit=30
//   site    https://smallblog.example/        every=3 links=/posts/ name="Small blog"
//   sitemap https://other.example/sitemap.xml every=7 limit=10
struct Source {
  SourceKind kind = SourceKind::Feed;
  std::string url;
  std::string name;         // shown in lists; defaults to the host
  std::string linkPattern;  // site: only links containing this are items
  std::string id;           // short stable id (8 hex digits of the URL); names the cache folder
  int everyDays = 1;        // how often to check; 0 = on every sync
  int limit = 20;           // newest items to look at per check
  ContentMode content = ContentMode::Auto;
  CommentsMode comments = CommentsMode::None;
};

struct SourcesParse {
  std::vector<Source> sources;
  std::vector<std::string> errors;  // "line 3: unknown option 'evry'"; bad lines are skipped
};

SourcesParse parseSources(std::string_view text);

// 8 lowercase hex digits (FNV-1a of the URL).
std::string sourceId(std::string_view url);

// Whether a source should be checked now. `lastCheck` is Unix seconds (0 = never); `now` is 0 when the
// clock is unknown, which counts as due. An hour of slack lets a daily source sync daily.
bool isDue(const Source& source, int64_t lastCheck, int64_t now);

}  // namespace feed
