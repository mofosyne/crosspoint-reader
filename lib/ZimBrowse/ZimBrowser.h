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
class ZimBrowser {
 public:
  static constexpr size_t DEFAULT_PAGE_SIZE = 40;

  explicit ZimBrowser(size_t pageSize = DEFAULT_PAGE_SIZE);

  Status open(freeink::content::ByteSource& source, ClusterDecoder& decoder, ZimBlobLoader& loader);

  // Loads the first page of titles that sort at or after `prefix`.
  Status search(std::string_view prefix);
  // Loads the page after the current one. NotFound when hasMore() is false. A full page that
  // ends exactly at the end of the namespace loads an empty next page (count() == 0).
  Status nextPage();

  size_t count() const { return count_; }
  bool hasMore() const { return hasMore_; }
  const std::string& title(size_t row) const { return titles_[row]; }
  char contentNamespace() const { return ns_; }

  // Reads the article at `row`, following redirects. `headword` receives the final title.
  // `blob` stays valid until the next readArticle call. Unsupported for non-HTML content.
  Status readArticle(size_t row, Blob& blob, std::string& headword);

 private:
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
