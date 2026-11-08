#pragma once

#include <HalMemory.h>
#include <ZimReader.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "util/HtmlToPlainText.h"

// One converted article: plain text plus the links found in it. All bulk data lives in PSRAM, so
// an article does not depend on the small internal heap (its largest block is about 155 KB).
struct ZimArticle {
  struct Link {
    uint32_t start;  // byte range of the link text inside `text`
    uint32_t end;
    uint32_t pathOffset;  // archive path inside `paths`
    uint32_t pathLen;
  };

  std::string headword;
  std::string path;             // where it came from: archive path, or feed article URL (for resuming)
  HalMemory::PsramBuffer text;  // NUL-terminated
  size_t textLen = 0;
  HalMemory::PsramBuffer linkTable;
  size_t linkCount = 0;
  HalMemory::PsramBuffer paths;
  size_t pathBytes = 0;

  const char* chars() const { return reinterpret_cast<const char*>(text.get()); }
  const Link* links() const { return reinterpret_cast<const Link*>(linkTable.get()); }
  std::string_view pathOf(const Link& link) const {
    return std::string_view(reinterpret_cast<const char*>(paths.get()) + link.pathOffset, link.pathLen);
  }
};

// Collects links while an article is converted. Owns scratch tables sized for the largest article
// it accepts; reuse one instance for every article of a session.
class ZimLinkCollector final : public LinkSink {
 public:
  static constexpr size_t MAX_LINKS = 4096;
  static constexpr size_t MAX_PATH_BYTES = 128 * 1024;

  ZimLinkCollector();
  bool valid() const { return table_ && paths_; }
  void reset() {
    count_ = 0;
    pathBytes_ = 0;
  }
  void onLink(size_t start, size_t end, std::string_view href) override;

  // Converts the HTML in `html` (destroyed in place) into `out`. `html` must be writable memory
  // that the caller owns, such as the cluster buffer. False when PSRAM is short.
  // `basePath` and `contentNs` say where the article lives, so relative links can be resolved.
  bool build(zim::Blob& html, std::string headword, std::string basePath, char contentNs, ZimArticle& out);

 private:
  HalMemory::PsramBuffer table_;
  HalMemory::PsramBuffer paths_;
  size_t count_ = 0;
  size_t pathBytes_ = 0;
  std::string basePath_;
  char contentNs_ = 0;
};
