#include "ZimBrowser.h"

#include <Logging.h>

#include <cctype>
#include <cstring>

namespace zim {

namespace {

// Decodes one of the few entities seen in titles at text[pos] ('&'); returns its length, or 0.
size_t decodeTitleEntity(const std::string& text, size_t pos, char& out) {
  static constexpr struct {
    const char* name;
    char value;
  } ENTITIES[] = {{"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&#39;", '\''}};
  for (const auto& e : ENTITIES) {
    const size_t len = strlen(e.name);
    if (text.compare(pos, len, e.name) == 0) {
      out = e.value;
      return len;
    }
  }
  return 0;
}

}  // namespace

void cleanTitle(std::string& title) {
  if (title.find_first_of("&<") == std::string::npos) return;
  // Decode first, so that "&lt;i&gt;" becomes a tag that the second pass removes.
  size_t written = 0;
  for (size_t i = 0; i < title.size();) {
    char decoded = 0;
    const size_t len = title[i] == '&' ? decodeTitleEntity(title, i, decoded) : 0;
    title[written++] = len ? decoded : title[i];
    i += len ? len : 1;
  }
  title.resize(written);
  written = 0;
  for (size_t i = 0; i < title.size();) {
    const bool tagStart = title[i] == '<' && i + 1 < title.size() &&
                          (title[i + 1] == '/' || isalpha(static_cast<unsigned char>(title[i + 1])));
    const size_t close = tagStart ? title.find('>', i) : std::string::npos;
    if (close != std::string::npos) {
      i = close + 1;
    } else {
      title[written++] = title[i++];
    }
  }
  title.resize(written);
}

bool linkToPath(std::string_view href, std::string_view baseFull, const char contentNs, std::string& path) {
  path.clear();
  const size_t cut = href.find_first_of("#?");
  if (cut != std::string_view::npos) href = href.substr(0, cut);
  if (href.empty() || href.find("://") != std::string_view::npos || href.substr(0, 7) == "mailto:" ||
      href.substr(0, 4) == "tel:") {
    return false;
  }
  // Walk the base directory and then the href, segment by segment.
  std::vector<std::string_view> segments;
  const auto walk = [&segments](std::string_view part, const bool dropLast) -> bool {
    while (!part.empty()) {
      const size_t slash = part.find('/');
      const std::string_view segment = part.substr(0, slash);
      const bool last = slash == std::string_view::npos;
      part = last ? std::string_view() : part.substr(slash + 1);
      if (last && dropLast) break;  // the base's own file name
      if (segment.empty() || segment == ".") continue;
      if (segment == "..") {
        if (segments.empty()) return false;
        segments.pop_back();
      } else {
        segments.push_back(segment);
      }
    }
    return true;
  };
  if (href.front() != '/' && !walk(baseFull, true)) return false;
  if (!walk(href, false)) return false;
  if (segments.size() < 2 || segments[0].size() != 1 || segments[0][0] != contentNs) return false;

  std::string joined;
  for (size_t i = 1; i < segments.size(); ++i) {
    if (i > 1) joined.push_back('/');
    joined.append(segments[i]);
  }
  path.reserve(joined.size());
  for (size_t i = 0; i < joined.size(); ++i) {
    const auto hex = [](char c) { return isdigit(static_cast<unsigned char>(c)) ? c - '0' : (tolower(c) - 'a' + 10); };
    if (joined[i] == '%' && i + 2 < joined.size() && isxdigit(static_cast<unsigned char>(joined[i + 1])) &&
        isxdigit(static_cast<unsigned char>(joined[i + 2]))) {
      path.push_back(static_cast<char>(hex(joined[i + 1]) * 16 + hex(joined[i + 2])));
      i += 2;
    } else {
      path.push_back(joined[i]);
    }
  }
  return !path.empty();
}

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

bool ZimBrowser::titleStartsWith(uint32_t titleIndex, std::string_view prefix) {
  if (titleIndex >= reader_.entryCount() || reader_.entryByTitleIndex(titleIndex, entry_) != Status::Ok) return false;
  if (entry_.ns != ns_) return false;
  const std::string_view title = entry_.title.empty() ? std::string_view(entry_.url) : std::string_view(entry_.title);
  return title.substr(0, prefix.size()) == prefix;
}

Status ZimBrowser::search(std::string_view prefix) {
  if (ns_ == 0) return Status::NotFound;
  uint32_t index = 0;
  Status status = reader_.lowerBoundTitle(ns_, prefix, index);
  if (status != Status::Ok) return status;
  if (prefix.empty() || titleStartsWith(index, prefix)) return loadPage(index);

  // Nothing starts with the text as typed. Try the spellings people mean: "moon landing" as
  // "Moon landing" and as "Moon Landing".
  std::string capitalized(prefix);
  std::string titleCase(prefix);
  bool wordStart = true;
  for (size_t i = 0; i < titleCase.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(titleCase[i]);
    if (wordStart && c < 0x80) titleCase[i] = static_cast<char>(toupper(c));
    wordStart = c == ' ' || c == '_';
  }
  if (!capitalized.empty() && static_cast<unsigned char>(capitalized[0]) < 0x80) {
    capitalized[0] = static_cast<char>(toupper(static_cast<unsigned char>(capitalized[0])));
  }
  for (const std::string* variant : {&capitalized, &titleCase}) {
    uint32_t found = 0;
    if (*variant != prefix && reader_.lowerBoundTitle(ns_, *variant, found) == Status::Ok &&
        titleStartsWith(found, *variant)) {
      return loadPage(found);
    }
  }
  // Some archives store titles with markup, which sorts them elsewhere; the article path still
  // works ("Agathis"), so look the text up as a path and show the page around its title.
  for (const std::string* variant : {&capitalized, &titleCase}) {
    std::string path(*variant);
    for (char& c : path) {
      if (c == ' ') c = '_';
    }
    if (reader_.findContent(path, entry_) != Status::Ok || entry_.ns != ns_) continue;
    const std::string title = entry_.title.empty() ? entry_.url : entry_.title;
    uint32_t found = 0;
    if (reader_.lowerBoundTitle(ns_, title, found) == Status::Ok) return loadPage(found);
  }
  return loadPage(index);  // nothing matches: show where the text would sort
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
    cleanTitle(titles_[count_]);
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
  return finishRead(blob, headword);
}

Status ZimBrowser::finishRead(Blob& blob, std::string& headword) {
  if (reader_.mimeType(entry_.mimeIndex) != "text/html") return Status::Unsupported;
  headword = entry_.title.empty() ? entry_.url : entry_.title;
  cleanTitle(headword);
  return loader_->read(reader_, decoder_, entry_.clusterNumber, entry_.blobNumber, blob);
}

Status ZimBrowser::readPath(std::string_view path, Blob& blob, std::string& headword) {
  if (!decoder_ || !loader_) return Status::NotFound;
  Status status = reader_.findContent(path, entry_);
  if (status == Status::Ok) status = reader_.resolveRedirects(entry_);
  if (status != Status::Ok) return status;
  return finishRead(blob, headword);
}

bool ZimBrowser::probeMainPage() {
  Entry probe;
  return reader_.findContent("index", probe) == Status::Ok || reader_.findContent("Main_Page", probe) == Status::Ok;
}

Status ZimBrowser::readMainPage(Blob& blob, std::string& headword) {
  if (!decoder_ || !loader_) return Status::NotFound;
  Status status = Status::NotFound;
  if (reader_.header().mainPage != 0xFFFFFFFF) {
    status = reader_.entryByUrlIndex(reader_.header().mainPage, entry_);
  }
  // Older archives have no main page entry; their front page is usually one of these.
  for (const char* fallback : {"index", "Main_Page"}) {
    if (status == Status::Ok) break;
    status = reader_.findContent(fallback, entry_);
  }
  if (status == Status::Ok) status = reader_.resolveRedirects(entry_);
  if (status != Status::Ok) return status;
  return finishRead(blob, headword);
}

}  // namespace zim
