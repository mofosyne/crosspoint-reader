#include "FeedSync.h"

#include <Arduino.h>
#include <ContentExtract.h>
#include "network/HttpDownloader.h"
#include <LinkExtractor.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "util/Dictionary.h"
#include "util/HtmlToPlainText.h"

namespace feeds {
namespace {

constexpr const char* TAG = "FEED";
constexpr size_t FETCH_CAP = 1536 * 1024;  // a page body beyond this is cut
constexpr size_t WORK_CAP = 1536 * 1024;   // converted text of one article
constexpr size_t MIN_FEED_TEXT = 800;       // content=auto: a shorter feed text means "fetch the page"
constexpr size_t FEED_SUMMARY_CAP = 40 * 1024;
constexpr size_t COMMENTS_CAP = 256 * 1024;  // text taken from a comments page

// Collects the links of one conversion, resolved against the page they came from.
class Links final : public LinkSink {
 public:
  Links(std::string base, size_t offset) : base_(std::move(base)), offset_(offset) {}
  void onLink(size_t start, size_t end, std::string_view href) override {
    if (links.size() >= 3000 || end <= start) return;
    const std::string url = feed::resolveUrl(base_, href);
    if (url.empty()) return;
    links.push_back({static_cast<uint32_t>(start + offset_), static_cast<uint32_t>(end + offset_), url});
  }
  std::vector<feed::CachedArticle::Link> links;

 private:
  std::string base_;
  size_t offset_;
};

int64_t nowSeconds() {
  const time_t t = time(nullptr);
  return t > 1700000000 ? static_cast<int64_t>(t) : 0;  // before about 2023 the clock was never set
}

bool tlsHeapOk() { return ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP && ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC; }

}  // namespace

bool FeedSync::begin(std::string& error) {
  feed::SourcesParse parsed;
  if (!loadSources(parsed)) {
    error = std::string("No sources file. Put one at ") + SOURCES_PATH;
    return false;
  }
  now_ = nowSeconds();
  for (auto& source : parsed.sources) {
    SourceState state;
    loadState(source.id, state);
    if (checkAll_ || feed::isDue(source, state.lastCheck, now_)) due_.push_back(std::move(source));
  }
  if (due_.empty()) {
    error = parsed.sources.empty() ? "The sources file has no usable lines" : "Nothing is due yet";
    if (!parsed.errors.empty()) error += " (" + parsed.errors[0] + ")";
    return false;
  }
  status_.sourcesTotal = due_.size();
  fetchBuf_ = HalMemory::allocatePsram(FETCH_CAP);
  workBuf_ = HalMemory::allocatePsram(WORK_CAP);
  if (!fetchBuf_ || !workBuf_) {
    error = "Not enough PSRAM";
    return false;
  }
  fetchCap_ = FETCH_CAP;
  workCap_ = WORK_CAP;
  return true;
}

bool FeedSync::step() {
  if (status_.finished) return false;
  if (!sourceOpen_) {
    if (current_ >= due_.size()) {
      status_.finished = true;
      status_.headline = "Done";
      return false;
    }
    startSource(current_);
    return true;
  }
  if (!sourceChecked_) {
    checkSource();
    sourceChecked_ = true;
    return true;
  }
  if (pendingAt_ < pending_.size()) {
    readPending();
    return true;
  }
  finishSource();
  return true;
}

void FeedSync::startSource(size_t index) {
  const feed::Source& source = due_[index];
  status_.headline = "Checking " + source.name;
  loadState(source.id, state_);
  pending_.clear();
  pendingAt_ = 0;
  sourceOpen_ = true;
  sourceChecked_ = false;
}

void FeedSync::finishSource() {
  const feed::Source& source = due_[current_];
  state_.lastCheck = now_;
  if (!saveState(source.id, state_)) LOG_ERR(TAG, "Could not save state for %s", source.id.c_str());
  sourceOpen_ = false;
  ++current_;
  status_.sourcesDone = current_;
}

void FeedSync::checkSource() {
  const feed::Source& source = due_[current_];
  if (!tlsHeapOk()) {
    status_.detail = "Not enough memory for a secure connection";
    ++status_.failed;
    return;
  }
  switch (source.kind) {
    case feed::SourceKind::Feed:
      checkFeed(source);
      break;
    case feed::SourceKind::Site:
      checkIndexPage(source);
      break;
    case feed::SourceKind::Sitemap:
      checkSitemap(source, source.url, 0);
      break;
  }
}

bool FeedSync::fetchToBuffer(const std::string& url, std::string_view& body) {
  size_t len = 0;
  uint8_t* buf = fetchBuf_.get();
  const bool ok = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, size_t n) {
    const size_t room = fetchCap_ - len;
    memcpy(buf + len, data, std::min(n, room));
    len += std::min(n, room);
    return true;  // a body past the cap is dropped, not an error
  });
  body = std::string_view(reinterpret_cast<const char*>(buf), len);
  if (!ok) {
    status_.detail = "HTTP " + std::to_string(HttpDownloader::lastStatus()) + " " + url.substr(0, 60);
    return false;
  }
  if (len >= 2 && buf[0] == 0x1f && buf[1] == 0x8b) {
    status_.detail = "Server sent gzip: " + url.substr(0, 50);
    LOG_ERR(TAG, "gzip body for %s", url.c_str());
    return false;
  }
  return true;
}

// ---- feeds -------------------------------------------------------------------------------------

void FeedSync::checkFeed(const feed::Source& source) {
  const size_t before = pending_.size();
  size_t considered = 0;
  feed::FeedParser parser(
      [&](feed::FeedItem& item) {
        if (considered++ >= static_cast<size_t>(source.limit)) return false;
        if (state_.seen.contains(item.url)) return true;
        const bool longEnough = item.summary.size() >= MIN_FEED_TEXT;
        const bool useFeedText = source.content == feed::ContentMode::Feed ||
                                 (source.content == feed::ContentMode::Auto && longEnough);
        Pending p{item.url, item.title, item.commentsUrl, item.published};
        if (useFeedText && !item.summary.empty() && source.comments == feed::CommentsMode::None) {
          if (saveItem(source, p, item.summary, /*fragment=*/true)) state_.seen.add(item.url);
        } else {
          pending_.push_back(std::move(p));  // the page (and comments) are fetched afterwards
        }
        return true;
      },
      FEED_SUMMARY_CAP);
  const bool ok = HttpDownloader::fetchUrl(
      source.url, [&](const uint8_t* data, size_t n) { return parser.feed(reinterpret_cast<const char*>(data), n); });
  parser.feed("", 0, true);
  if (!ok && !parser.stopped() && parser.itemCount() == 0) {
    status_.detail = "HTTP " + std::to_string(HttpDownloader::lastStatus()) + " " + source.name;
    ++status_.failed;
  } else if (parser.kind() == feed::FeedKind::Unknown) {
    status_.detail = source.name + " is not a feed";
    ++status_.failed;
  }
  LOG_INF(TAG, "%s: %u items read, %u to fetch", source.name.c_str(), static_cast<unsigned>(parser.itemCount()),
          static_cast<unsigned>(pending_.size() - before));
}

// ---- sites and sitemaps ----------------------------------------------------------------------

void FeedSync::checkIndexPage(const feed::Source& source) {
  std::string_view body;
  if (!fetchToBuffer(source.url, body)) {
    ++status_.failed;
    return;
  }
  const auto links = feed::extractLinks(body, source.url, source.linkPattern, static_cast<size_t>(source.limit) * 4);
  size_t taken = 0;
  for (const auto& link : links) {
    if (taken >= static_cast<size_t>(source.limit)) break;
    if (state_.seen.contains(link.url)) continue;
    pending_.push_back({link.url, link.text, "", 0});
    ++taken;
  }
  LOG_INF(TAG, "%s: %u links, %u new", source.name.c_str(), static_cast<unsigned>(links.size()),
          static_cast<unsigned>(taken));
}

void FeedSync::checkSitemap(const feed::Source& source, const std::string& url, int depth) {
  // Keep the newest `limit` unseen entries: by lastmod, and for undated ones by position in the file.
  struct Candidate {
    int64_t published;
    size_t order;
    Pending item;
  };
  std::vector<Candidate> best;  // newest first
  const auto newer = [](const Candidate& a, const Candidate& b) {
    return a.published != b.published ? a.published > b.published : a.order > b.order;
  };
  std::string childSitemap;  // for a sitemap index: the last child listed
  size_t order = 0;
  feed::FeedParser parser(
      [&](feed::FeedItem& item) {
        if (item.isSitemap) {
          childSitemap = item.url;
          return true;
        }
        ++order;
        if (state_.seen.contains(item.url)) return true;
        Candidate c{item.published, order, Pending{item.url, item.title, "", item.published}};
        best.insert(std::upper_bound(best.begin(), best.end(), c, newer), std::move(c));
        if (best.size() > static_cast<size_t>(source.limit)) best.pop_back();
        return true;
      },
      1024);
  const bool ok = HttpDownloader::fetchUrl(
      url, [&](const uint8_t* data, size_t n) { return parser.feed(reinterpret_cast<const char*>(data), n); });
  parser.feed("", 0, true);
  if (!ok && parser.itemCount() == 0) {
    status_.detail = "HTTP " + std::to_string(HttpDownloader::lastStatus()) + " " + url.substr(0, 60);
    ++status_.failed;
    return;
  }
  if (parser.kind() == feed::FeedKind::SitemapIndex && !childSitemap.empty() && depth == 0) {
    checkSitemap(source, childSitemap, 1);
    return;
  }
  for (auto& c : best) pending_.push_back(std::move(c.item));
  LOG_INF(TAG, "%s: sitemap with %u entries, %u new", source.name.c_str(), static_cast<unsigned>(parser.itemCount()),
          static_cast<unsigned>(best.size()));
}

// ---- items -------------------------------------------------------------------------------------

void FeedSync::readPending() {
  const feed::Source& source = due_[current_];
  const Pending item = pending_[pendingAt_++];
  status_.headline = "Reading " + (item.title.empty() ? item.url : item.title).substr(0, 60);
  if (!tlsHeapOk()) {
    status_.detail = "Low memory, item skipped";
    ++status_.failed;
    return;
  }
  std::string_view page;
  if (!fetchToBuffer(item.url, page)) {
    ++status_.failed;
    return;
  }
  const std::string_view main = feed::extractMainContent(page);
  if (saveItem(source, item, main, /*fragment=*/false)) {
    state_.seen.add(item.url);
  } else {
    ++status_.failed;
  }
}

// Converts `html` (a page's main content or a feed's own text) and the comments page, if wanted, and
// writes the article. The title becomes a heading at the top.
bool FeedSync::saveItem(const feed::Source& source, const Pending& item, std::string_view html, bool fragment) {
  (void)fragment;
  if (html.empty()) return false;
  char* work = reinterpret_cast<char*>(workBuf_.get());
  std::string title = item.title.empty() ? item.url : item.title;
  std::replace(title.begin(), title.end(), '\n', ' ');

  // "<marker>Title\n" then the converted body, all in one PSRAM buffer
  size_t used = 0;
  work[used++] = DICTIONARY_PLAIN_HEADWORD_MARKER;
  const size_t titleBytes = std::min(title.size(), static_cast<size_t>(300));
  memcpy(work + used, title.data(), titleBytes);
  used += titleBytes;
  work[used++] = '\n';

  const size_t bodyRoom = workCap_ - used - 1;
  const size_t copy = std::min(html.size(), bodyRoom);
  memcpy(work + used, html.data(), copy);
  Links links(item.url, used);
  const size_t bodyLen = htmlToPlainTextInPlace(work + used, copy, DICTIONARY_PLAIN_HEADWORD_MARKER, &links, true);
  used += bodyLen;

  // Comments: a second page, taken as text under its own heading.
  if (source.comments == feed::CommentsMode::Link && !item.commentsUrl.empty() && item.commentsUrl != item.url &&
      used + 4096 < workCap_ && tlsHeapOk()) {
    std::string_view commentsPage;
    if (fetchToBuffer(item.commentsUrl, commentsPage) && !commentsPage.empty()) {
      const char* heading = "\n\x01""Comments\n";
      const size_t headingLen = strlen(heading);
      memcpy(work + used, heading, headingLen);
      used += headingLen;
      const std::string_view region = feed::extractMainContent(commentsPage);
      const size_t room = std::min(workCap_ - used - 1, COMMENTS_CAP);
      const size_t n = std::min(region.size(), room);
      memcpy(work + used, region.data(), n);
      Links commentLinks(item.commentsUrl, used);
      const size_t len = htmlToPlainTextInPlace(work + used, n, DICTIONARY_PLAIN_HEADWORD_MARKER, &commentLinks, true);
      used += len;
      for (auto& l : commentLinks.links) links.links.push_back(std::move(l));
    }
  }

  feed::CachedArticle article;
  article.url = item.url;
  article.title = title;
  article.sourceId = source.id;
  article.commentsUrl = item.commentsUrl;
  article.published = item.published;
  article.fetched = now_;
  article.text = std::string_view(work, used);
  article.links = std::move(links.links);
  if (!saveArticle(article)) {
    status_.detail = "Could not save " + title.substr(0, 40);
    return false;
  }
  ++status_.saved;
  LOG_INF(TAG, "Saved '%s' (%u bytes, %u links)", title.c_str(), static_cast<unsigned>(used),
          static_cast<unsigned>(article.links.size()));
  return true;
}

}  // namespace feeds
