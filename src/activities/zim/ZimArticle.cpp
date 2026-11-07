#include "ZimArticle.h"

#include <Logging.h>
#include <ZimBrowser.h>

#include <algorithm>
#include <cstring>

#include "util/Dictionary.h"

namespace {
// HTML beyond this is ignored; a "nopic" article can reach 650 KB.
constexpr size_t MAX_HTML_BYTES = 2 * 1024 * 1024;
}  // namespace

ZimLinkCollector::ZimLinkCollector()
    : table_(HalMemory::allocatePsram(MAX_LINKS * sizeof(ZimArticle::Link))),
      paths_(HalMemory::allocatePsram(MAX_PATH_BYTES)) {}

void ZimLinkCollector::onLink(size_t start, size_t end, std::string_view href) {
  if (end <= start || count_ >= MAX_LINKS) return;
  std::string path;
  if (!zim::linkToPath(href, basePath_, contentNs_, path) || pathBytes_ + path.size() > MAX_PATH_BYTES) return;
  memcpy(paths_.get() + pathBytes_, path.data(), path.size());
  auto* links = reinterpret_cast<ZimArticle::Link*>(table_.get());
  links[count_++] = {static_cast<uint32_t>(start), static_cast<uint32_t>(end), static_cast<uint32_t>(pathBytes_),
                     static_cast<uint32_t>(path.size())};
  pathBytes_ += path.size();
}

bool ZimLinkCollector::build(zim::Blob& html, std::string headword, std::string basePath, const char contentNs,
                             ZimArticle& out) {
  if (!valid()) return false;
  reset();
  basePath_ = std::move(basePath);
  contentNs_ = contentNs;
  auto* buffer = const_cast<char*>(reinterpret_cast<const char*>(html.data));
  const size_t size = std::min(html.size, MAX_HTML_BYTES);
  const size_t len = htmlToPlainTextInPlace(buffer, size, DICTIONARY_PLAIN_HEADWORD_MARKER, this);

  out = ZimArticle{};
  out.headword = std::move(headword);
  out.text = HalMemory::allocatePsram(len + 1);
  const size_t tableBytes = count_ * sizeof(ZimArticle::Link);
  if (tableBytes > 0) out.linkTable = HalMemory::allocatePsram(tableBytes);
  if (pathBytes_ > 0) out.paths = HalMemory::allocatePsram(pathBytes_);
  if (!out.text || (tableBytes > 0 && !out.linkTable) || (pathBytes_ > 0 && !out.paths)) {
    LOG_ERR("ZIM", "PSRAM short for a %u byte article", static_cast<unsigned>(len));
    out = ZimArticle{};
    return false;
  }
  memcpy(out.text.get(), buffer, len);
  out.text[len] = '\0';
  out.textLen = len;
  if (tableBytes > 0) memcpy(out.linkTable.get(), table_.get(), tableBytes);
  if (pathBytes_ > 0) memcpy(out.paths.get(), paths_.get(), pathBytes_);
  // A link whose end ran past the trimmed text is clipped; empty ones are dropped by the viewer.
  auto* links = reinterpret_cast<ZimArticle::Link*>(out.linkTable.get());
  for (size_t i = 0; i < count_; ++i) {
    links[i].start = std::min<uint32_t>(links[i].start, static_cast<uint32_t>(len));
    links[i].end = std::min<uint32_t>(links[i].end, static_cast<uint32_t>(len));
  }
  out.linkCount = count_;
  out.pathBytes = pathBytes_;
  return true;
}
