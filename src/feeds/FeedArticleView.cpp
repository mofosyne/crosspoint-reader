#include "FeedArticleView.h"

#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "FeedStore.h"
#include "activities/zim/ZimArticleActivity.h"

namespace feeds {

bool loadViewerArticle(std::string_view url, ZimArticle& out, bool markAsRead) {
  LoadedArticle loaded;
  const std::string key(url);
  if (!loadArticle(key, loaded)) return false;
  const auto& a = loaded.article;

  out = ZimArticle{};
  out.headword = a.title;
  out.path = key;
  out.text = HalMemory::allocatePsram(a.text.size() + 1);
  size_t pathBytes = 0;
  for (const auto& l : a.links) pathBytes += l.url.size();
  if (!a.links.empty()) {
    out.linkTable = HalMemory::allocatePsram(a.links.size() * sizeof(ZimArticle::Link));
    out.paths = HalMemory::allocatePsram(pathBytes + 1);
  }
  if (!out.text || (!a.links.empty() && (!out.linkTable || !out.paths))) {
    LOG_ERR("FEED", "PSRAM short for the viewer copy of a %u byte article", static_cast<unsigned>(a.text.size()));
    out = ZimArticle{};
    return false;
  }
  memcpy(out.text.get(), a.text.data(), a.text.size());
  out.text[a.text.size()] = '\0';
  out.textLen = a.text.size();
  auto* links = reinterpret_cast<ZimArticle::Link*>(out.linkTable.get());
  size_t offset = 0;
  for (size_t i = 0; i < a.links.size(); ++i) {
    memcpy(out.paths.get() + offset, a.links[i].url.data(), a.links[i].url.size());
    links[i] = {a.links[i].start, a.links[i].end, static_cast<uint32_t>(offset),
                static_cast<uint32_t>(a.links[i].url.size())};
    offset += a.links[i].url.size();
  }
  out.linkCount = a.links.size();
  out.pathBytes = pathBytes;
  if (markAsRead && !a.read) markRead(key);
  return true;
}

std::unique_ptr<Activity> makeArticleViewer(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                            const std::string& url, int startPage) {
  ZimArticle article;
  if (!loadViewerArticle(url, article)) return nullptr;
  auto viewer = makeUniqueNoThrow<ZimArticleActivity>(
      renderer, mappedInput, std::move(article),
      [](std::string_view target, ZimArticle& out) { return loadViewerArticle(target, out); });
  if (viewer) {
    viewer->setResumeContext(2, "");
    viewer->setStartPage(startPage);
  }
  return viewer;
}

}  // namespace feeds
