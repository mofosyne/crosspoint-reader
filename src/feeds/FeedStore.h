#pragma once

#include <CachedArticle.h>
#include <HalMemory.h>
#include <SeenSet.h>
#include <SourcesFile.h>

#include <string>
#include <string_view>
#include <vector>

// SD card layout of the feed reader (the sources file is written by the user):
//   /feeds/sources.txt        one source per line, see SourcesFile.h
//   /feeds/state/<id>.bin     per source: when it was last checked and the URLs already delivered
//   /feeds/items/<hash>.cpf   one saved article each (CachedArticle format)
namespace feeds {

constexpr const char* SOURCES_PATH = "/feeds/sources.txt";

// File name part of an article: 16 hex digits of the URL's hash.
std::string itemName(const std::string& url);
std::string itemPath(const std::string& url);

bool loadSources(feed::SourcesParse& out);  // false when the file is missing or unreadable

struct SourceState {
  feed::SeenSet seen;
  int64_t lastCheck = 0;
};
bool loadState(const std::string& sourceId, SourceState& out);  // false: never checked (state left empty)
bool saveState(const std::string& sourceId, const SourceState& state);
// A source whose pages refuse the reader (bot challenge, 401/403): content=auto then keeps to the
// feed's own text. The mark lives next to the state and goes away with it.
bool pagesBlocked(const std::string& sourceId);
void setPagesBlocked(const std::string& sourceId);

// The last weather answer for the dashboard, with the time it was fetched.
bool saveWeather(std::string_view json, int64_t fetched);
bool loadWeather(std::string& json, int64_t& fetched);

bool saveArticle(const feed::CachedArticle& article);  // writes through a temp file, then renames

// Header information for lists; reading it costs one small read per file.
struct ItemInfo {
  std::string url;
  std::string title;
  std::string sourceId;
  int64_t published = 0;
  int64_t fetched = 0;
  bool read = false;
  std::string name;  // file name part, see itemName()
};
// Newest first (by published, else fetched). `sourceId` empty = every source. At most `maxItems`.
std::vector<ItemInfo> listItems(const std::string& sourceId, size_t maxItems = 300);
size_t countUnread(const std::string& sourceId);

// Loads a whole article file into PSRAM and parses it (the views point into `buffer`).
struct LoadedArticle {
  HalMemory::PsramBuffer buffer;
  size_t size = 0;
  feed::CachedArticle article;
};
bool loadArticle(const std::string& url, LoadedArticle& out);
bool markRead(const std::string& url);  // rewrites the header's read flag in place

}  // namespace feeds
