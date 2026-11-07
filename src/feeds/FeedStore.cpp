#include "FeedStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace feeds {
namespace {

constexpr const char* TAG = "FEED";
constexpr const char* STATE_DIR = "/feeds/state";
constexpr const char* ITEMS_DIR = "/feeds/items";
constexpr size_t MAX_SOURCES_BYTES = 32 * 1024;
constexpr size_t HEADER_PEEK = 1024;

std::string statePath(const std::string& id) { return std::string(STATE_DIR) + "/" + id + ".bin"; }

}  // namespace

std::string itemName(const std::string& url) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(feed::SeenSet::hashUrl(url)));
  return buf;
}

std::string itemPath(const std::string& url) { return std::string(ITEMS_DIR) + "/" + itemName(url) + ".cpf"; }

bool loadSources(feed::SourcesParse& out) {
  std::string text;
  if (!Storage.readFileToString(TAG, SOURCES_PATH, MAX_SOURCES_BYTES, text)) return false;
  out = feed::parseSources(text);
  return true;
}

bool loadState(const std::string& sourceId, SourceState& out) {
  out = SourceState{};
  std::string blob;
  if (!Storage.readFileToString(TAG, statePath(sourceId), 64 * 1024, blob)) return false;
  return out.seen.parse(blob, out.lastCheck);
}

bool saveState(const std::string& sourceId, const SourceState& state) {
  Storage.ensureDirectoryExists(STATE_DIR);
  const std::string blob = state.seen.serialize(state.lastCheck);
  HalFile f;
  const std::string tmp = statePath(sourceId) + ".tmp";
  if (!Storage.openFileForWrite(TAG, tmp, f)) return false;
  const bool ok = f.write(blob.data(), blob.size()) == blob.size();
  f.close();
  return ok && Storage.replaceFile(tmp.c_str(), statePath(sourceId).c_str());
}

bool saveArticle(const feed::CachedArticle& article) {
  Storage.ensureDirectoryExists("/feeds");
  Storage.ensureDirectoryExists(ITEMS_DIR);
  const std::string path = itemPath(article.url);
  const std::string tmp = path + ".tmp";
  HalFile f;
  if (!Storage.openFileForWrite(TAG, tmp, f)) return false;
  const bool ok = feed::serializeArticle(article, [&](const char* d, size_t n) { return f.write(d, n) == n; });
  f.close();
  if (!ok) {
    Storage.remove(tmp.c_str());
    return false;
  }
  return Storage.replaceFile(tmp.c_str(), path.c_str());
}

std::vector<ItemInfo> listItems(const std::string& sourceId, size_t maxItems) {
  std::vector<ItemInfo> items;
  HalFile dir = Storage.open(ITEMS_DIR);
  if (!dir || !dir.isDirectory()) return items;
  char name[64];
  std::string head(HEADER_PEEK, '\0');
  for (HalFile f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    f.getName(name, sizeof(name));
    const size_t len = strlen(name);
    if (len < 5 || strcmp(name + len - 4, ".cpf") != 0) continue;
    const int n = f.read(head.data(), head.size());
    f.close();
    if (n <= 0) continue;
    feed::CachedArticle a;
    if (!feed::parseArticleHeader(std::string_view(head.data(), static_cast<size_t>(n)), a)) continue;
    if (!sourceId.empty() && a.sourceId != sourceId) continue;
    items.push_back({a.url, a.title, a.sourceId, a.published, a.fetched, a.read, std::string(name, len - 4)});
  }
  std::sort(items.begin(), items.end(), [](const ItemInfo& x, const ItemInfo& y) {
    const int64_t kx = x.published ? x.published : x.fetched;
    const int64_t ky = y.published ? y.published : y.fetched;
    return kx > ky;
  });
  if (items.size() > maxItems) items.resize(maxItems);
  return items;
}

size_t countUnread(const std::string& sourceId) {
  size_t unread = 0;
  for (const auto& item : listItems(sourceId, 1000)) unread += item.read ? 0 : 1;
  return unread;
}

bool loadArticle(const std::string& url, LoadedArticle& out) {
  HalFile f;
  if (!Storage.openFileForRead(TAG, itemPath(url), f)) return false;
  const size_t size = f.fileSize();
  out.buffer = HalMemory::allocatePsram(size + 1);
  if (!out.buffer) {
    LOG_ERR(TAG, "PSRAM short for a %u byte article", static_cast<unsigned>(size));
    return false;
  }
  const int n = f.read(out.buffer.get(), size);
  f.close();
  if (n != static_cast<int>(size)) return false;
  out.size = size;
  return feed::parseArticle(std::string_view(reinterpret_cast<const char*>(out.buffer.get()), size), out.article);
}

bool markRead(const std::string& url) {
  // The flag is the single byte after "read: " in the header, so flipping it needs no rewrite.
  HalFile f = Storage.open(itemPath(url).c_str(), O_RDWR);
  if (!f) return false;
  std::string head(HEADER_PEEK, '\0');
  const int n = f.read(head.data(), head.size());
  const size_t at = n > 0 ? head.find("\nread: ") : std::string::npos;
  bool ok = false;
  if (at != std::string::npos && at + 7 < static_cast<size_t>(n) && f.seek(at + 7)) {
    const uint8_t one = '1';
    ok = f.write(&one, 1) == 1;
  }
  f.close();
  return ok;
}

}  // namespace feeds
