#pragma once

#include <ZimBlobLoader.h>
#include <ZimReader.h>

#include <string>
#include <string_view>
#include <vector>

namespace zim {

// Browse-by-title state for one open archive: a fixed-size window of titles in the content
// namespace, plus article reading. No UI and no platform code. The source, decoder and
// loader are owned by the caller and must outlive this object.
// Some archives store titles with escaped markup, such as "&lt;i&gt;Agathis&lt;/i&gt;". Decodes the
// common entities and removes simple tags in place, so the list shows "Agathis". A title without
// '&' or '<' is left untouched, and the stored title is still what search compares against.
void cleanTitle(std::string& title);

// Resolves a link's href against the article it appears in. `baseFull` is that article's full path
// with its namespace ("A/Moon", "A/User:x/Landing"); links are relative to it, so a page deep in
// the archive can say "../../A/Moon". Drops "#fragment" and "?query" and decodes %XX. On success
// `path` is the target without namespace. False for links that leave the article namespace
// (images, "../I/..."), the archive ("https://", "mailto:") or point only at an anchor.
bool linkToPath(std::string_view href, std::string_view baseFull, char contentNs, std::string& path);

class ZimBrowser {
 public:
  static constexpr size_t DEFAULT_PAGE_SIZE = 40;

  explicit ZimBrowser(size_t pageSize = DEFAULT_PAGE_SIZE);

  Status open(freeink::content::ByteSource& source, ClusterDecoder& decoder, ZimBlobLoader& loader);

  // Loads the first page of titles that sort at or after `prefix`. Titles sort by raw bytes, so
  // capitals come before lowercase letters; when `prefix` matches nothing as typed, "Moon" and
  // "Moon Landing" style spellings are tried, then the typed text as an article path.
  Status search(std::string_view prefix);
  // Loads the page that starts at title-order index `titleIndex` (a random jump for benchmarks).
  // An index past the namespace gives an empty page.
  Status jumpTo(uint32_t titleIndex) { return loadPage(titleIndex); }
  uint32_t entryCount() const { return reader_.entryCount(); }
  // Loads the page after the current one. NotFound when hasMore() is false. A full page that
  // ends exactly at the end of the namespace loads an empty next page (count() == 0).
  Status nextPage();

  size_t count() const { return count_; }
  bool hasMore() const { return hasMore_; }
  const std::string& title(size_t row) const { return titles_[row]; }
  char contentNamespace() const { return ns_; }
  // Full path ("A/Moon") of the article read last, after redirects; the base for its links.
  std::string lastArticlePath() const { return std::string(1, entry_.ns) + "/" + entry_.url; }

  // Reads the article at `row`, following redirects. `headword` receives the final title.
  // `blob` stays valid until the next readArticle call. Unsupported for non-HTML content.
  Status readArticle(size_t row, Blob& blob, std::string& headword);
  // Reads the article at an archive path that linkToPath() produced. NotFound when it is missing.
  Status readPath(std::string_view path, Blob& blob, std::string& headword);
  // True when the archive names a main page (or has an "index" / "Main_Page" article to use).
  bool hasMainPage() { return reader_.header().mainPage != 0xFFFFFFFF || probeMainPage(); }
  Status readMainPage(Blob& blob, std::string& headword);

 private:
  bool probeMainPage();
  Status finishRead(Blob& blob, std::string& headword);
  bool titleStartsWith(uint32_t titleIndex, std::string_view prefix);
  Status loadPage(uint32_t firstTitleIndex);

  ZimReader reader_;
  ClusterDecoder* decoder_ = nullptr;
  ZimBlobLoader* loader_ = nullptr;
  char ns_ = 0;
  uint32_t firstIndex_ = 0;  // title-order index of row 0
  size_t count_ = 0;
  bool hasMore_ = false;
  size_t pageSize_;
  std::vector<std::string> titles_;  // pageSize_ slots, kept across pages so they reuse capacity
  Entry entry_;
};

}  // namespace zim
