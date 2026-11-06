#include "ZimBrowser.h"

#include <Logging.h>

namespace zim {

ZimBrowser::ZimBrowser(size_t pageSize) : pageSize_(pageSize) { titles_.resize(pageSize_); }

Status ZimBrowser::open(freeink::content::ByteSource& source, ClusterDecoder& decoder, ZimBlobLoader& loader) {
  decoder_ = &decoder;
  loader_ = &loader;
  count_ = 0;
  hasMore_ = false;
  const Status status = reader_.open(source);
  if (status != Status::Ok) return status;

  // 6.1+ archives keep articles in 'C', older ones in 'A'. Use the first with a title.
  ns_ = 0;
  for (const char candidate : {'C', 'A'}) {
    uint32_t index = 0;
    if (reader_.lowerBoundTitle(candidate, "", index) != Status::Ok) continue;
    if (index < reader_.entryCount() && reader_.entryByTitleIndex(index, entry_) == Status::Ok &&
        entry_.ns == candidate) {
      ns_ = candidate;
      break;
    }
  }
  if (ns_ == 0) {
    LOG_ERR("ZIM", "No article namespace found");
    return Status::NotFound;
  }
  return Status::Ok;
}

Status ZimBrowser::search(std::string_view prefix) {
  if (ns_ == 0) return Status::NotFound;
  uint32_t index = 0;
  const Status status = reader_.lowerBoundTitle(ns_, prefix, index);
  if (status != Status::Ok) return status;
  return loadPage(index);
}

Status ZimBrowser::nextPage() {
  if (!hasMore_) return Status::NotFound;
  return loadPage(firstIndex_ + static_cast<uint32_t>(count_));
}

Status ZimBrowser::loadPage(uint32_t firstTitleIndex) {
  firstIndex_ = firstTitleIndex;
  count_ = 0;
  hasMore_ = false;
  const uint32_t total = reader_.entryCount();
  while (count_ < pageSize_ && firstTitleIndex + count_ < total) {
    const Status status = reader_.entryByTitleIndex(firstTitleIndex + static_cast<uint32_t>(count_), entry_);
    if (status != Status::Ok) return status;
    if (entry_.ns != ns_) return Status::Ok;  // end of the article namespace
    titles_[count_].assign(entry_.title);
    ++count_;
  }
  // A full page may or may not be followed by more; the next load settles it.
  hasMore_ = count_ == pageSize_ && firstTitleIndex + count_ < total;
  return Status::Ok;
}

Status ZimBrowser::readArticle(size_t row, Blob& blob, std::string& headword) {
  if (row >= count_ || !decoder_ || !loader_) return Status::NotFound;
  Status status = reader_.entryByTitleIndex(firstIndex_ + static_cast<uint32_t>(row), entry_);
  if (status == Status::Ok) status = reader_.resolveRedirects(entry_);
  if (status != Status::Ok) return status;
  if (reader_.mimeType(entry_.mimeIndex) != "text/html") return Status::Unsupported;
  headword = entry_.title;
  return loader_->read(reader_, decoder_, entry_.clusterNumber, entry_.blobNumber, blob);
}

}  // namespace zim
