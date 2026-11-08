#include "FeedSync.h"

#include <Arduino.h>
#include <CommentFeed.h>
#include <ContentExtract.h>
#include <HnComments.h>
#include <LinkExtractor.h>
#include <Logging.h>
#include <Weather.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "network/HttpDownloader.h"
#include "util/Dictionary.h"
#include "util/HtmlToPlainText.h"

namespace feeds {
namespace {

constexpr const char* TAG = "FEED";
constexpr size_t FETCH_CAP = 1536 * 1024;    // a page body beyond this is cut
constexpr size_t WORK_CAP = 1536 * 1024;     // converted text of one article
constexpr size_t MIN_FEED_TEXT = 800;        // content=auto: a shorter feed text means "fetch the page"
constexpr double WEATHER_LATITUDE = -37.81;  // Melbourne
constexpr double WEATHER_LONGITUDE = 144.96;
constexpr size_t DEFERRED_FEED_TEXT_CAP = 6 * 1024;  // feed text kept while its comments wait to be fetched
constexpr size_t FEED_SUMMARY_CAP = 40 * 1024;
constexpr unsigned long HN_DEADLINE_MS = 60000;     // the comment JSON of a busy story is large
constexpr size_t COMMENT_TEXT_CAP = 4096;           // text kept per comment of a comment feed
constexpr size_t COMMENTS_CAP = 256 * 1024;         // text taken from a comments page
constexpr unsigned long FETCH_DEADLINE_MS = 30000;  // a slower page is given up on

// Links to files that are not web pages (a PDF would only become junk text).
bool isBinaryUrl(const std::string& url) {
  std::string path = url.substr(0, url.find_first_of("?#"));
  for (char& c : path) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  for (const char* ext : {".pdf", ".zip", ".gz", ".tar", ".epub", ".mp3", ".mp4", ".webm", ".png", ".jpg", ".jpeg",
                          ".gif", ".webp", ".iso", ".dmg", ".exe"}) {
    const size_t n = strlen(ext);
    if (path.size() > n && path.compare(path.size() - n, n, ext) == 0) return true;
  }
  return false;
}

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

bool tlsHeapOk() {
  return ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP &&
         ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC;
}

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
    if (current_ >= due_.size() && !weatherDone_) {
      weatherDone_ = true;
      fetchWeather();
      return true;
    }
    if (current_ >= due_.size()) {
      status_.finished = true;
      status_.headline = "Done";
      if (status_.failed == 0) status_.detail.clear();  // a note about an earlier skipped item is stale now
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

// The dashboard's forecast. Failing to get it is not a sync failure: the dashboard keeps the old one.
void FeedSync::fetchWeather() {
  status_.headline = "Checking the weather";
  if (!tlsHeapOk()) return;
  std::string_view body;
  if (!fetchToBuffer(feed::openMeteoUrl(WEATHER_LATITUDE, WEATHER_LONGITUDE, 4), body)) return;
  feed::Weather weather;
  if (!feed::parseOpenMeteo(body, weather)) {
    LOG_ERR(TAG, "Weather answer not understood");
    return;
  }
  if (!saveWeather(body, now_)) LOG_ERR(TAG, "Could not save the weather");
}

void FeedSync::startSource(size_t index) {
  const feed::Source& source = due_[index];
  status_.headline = "Checking " + source.name;
  loadState(source.id, state_);
  pagesBlocked_ = pagesBlocked(source.id);
  blockedHits_ = 0;
  pagesRead_ = 0;
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
  const unsigned long start = millis();
  bool timedOut = false;
  const bool ok = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, size_t n) {
    if (millis() - start > FETCH_DEADLINE_MS) {
      timedOut = true;
      return false;
    }
    const size_t room = fetchCap_ - len;
    memcpy(buf + len, data, std::min(n, room));
    len += std::min(n, room);
    return true;  // a body past the cap is dropped, not an error
  });
  body = std::string_view(reinterpret_cast<const char*>(buf), len);
  if (!ok || timedOut) {
    status_.detail = timedOut ? "Too slow " + url.substr(0, 60)
                              : "HTTP " + std::to_string(HttpDownloader::lastStatus()) + " " + url.substr(0, 60);
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
        // Without the preload setting, Auto keeps to what the feed carries and never fetches the page.
        const bool feedOnly =
            source.content == feed::ContentMode::Auto && (!SETTINGS.feedPreloadPages || pagesBlocked_);
        const bool useFeedText = source.content == feed::ContentMode::Feed ||
                                 (source.content == feed::ContentMode::Auto && (longEnough || feedOnly));
        Pending p{item.url, item.title, item.commentsUrl, item.published};
        std::string text = item.summary;
        if (text.empty() && feedOnly) text = "<p>" + item.url + "</p>";  // nothing but the link to keep
        if (useFeedText && !text.empty() && source.comments == feed::CommentsMode::None) {
          if (saveItem(source, p, text, /*fragment=*/true)) state_.seen.add(item.url);
        } else {
          // The comments come from a second request, which can't run inside this download.
          if (useFeedText && !text.empty()) p.feedText = text.substr(0, DEFERRED_FEED_TEXT_CAP);
          if (!useFeedText && source.content == feed::ContentMode::Auto) {
            p.fallbackText = item.summary.empty() ? "<p>" + item.url + "</p>" : item.summary.substr(0, MIN_FEED_TEXT);
          }
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
  std::string note;  // stands in for the page when it can't be read but there are comments to keep
  const std::string* feedText = item.feedText.empty() ? nullptr : &item.feedText;
  if (!feedText && pagesBlocked_ && !item.fallbackText.empty()) feedText = &item.fallbackText;
  if (feedText) {
    // the feed's own text stands in for the page
  } else if (isBinaryUrl(item.url)) {
    note = "<p>This link is a file, not a web page: " + item.url + "</p>";
  } else if (!fetchToBuffer(item.url, page)) {
    const int status = HttpDownloader::lastStatus();
    if ((status == 401 || status == 403 || status == 429) && source.content == feed::ContentMode::Auto) {
      // Two refusals and no page read this run: stop asking this site for pages.
      if (++blockedHits_ >= 2 && pagesRead_ == 0 && !pagesBlocked_) {
        pagesBlocked_ = true;
        setPagesBlocked(source.id);
        LOG_INF(TAG, "%s refuses page fetches, keeping to feed text", source.name.c_str());
      }
      if (!item.fallbackText.empty()) feedText = &item.fallbackText;
    }
    if (!feedText) note = "<p>The page could not be fetched (" + status_.detail + "). Link: " + item.url + "</p>";
  } else {
    ++pagesRead_;
  }
  const bool hasComments = source.comments == feed::CommentsMode::Link && !item.commentsUrl.empty();
  if (!note.empty() && !hasComments) {
    if (isBinaryUrl(item.url)) state_.seen.add(item.url);  // nothing to retry
    ++status_.failed;
    return;
  }
  const std::string_view main = feedText       ? std::string_view(*feedText)
                                : note.empty() ? feed::dropLeadingTitle(feed::extractMainContent(page), item.title)
                                               : std::string_view(note);
  if (saveItem(source, item, main, /*fragment=*/feedText != nullptr)) {
    state_.seen.add(item.url);
  } else {
    ++status_.failed;
  }
}

// Converts `html` (a page's main content or a feed's own text) and the comments page, if wanted, and
// writes the article. The title becomes a heading at the top.
// Hacker News comments from the Algolia API: threaded, with authors and ages. Returns false when the
// item is not an HN link or the request fails, so the caller can fall back to the comments page.
bool FeedSync::appendHnComments(const Pending& item, char* work, size_t& used,
                                std::vector<feed::CachedArticle::Link>& links) {
  std::string apiUrl;
  if (!feed::HnThread::apiUrlFor(item.commentsUrl, apiUrl)) return false;
  // fetchBuf_ holds the page only until saveItem has copied it, so it is free to hold comment texts.
  feed::HnThread thread(reinterpret_cast<char*>(fetchBuf_.get()), fetchCap_);
  const unsigned long start = millis();
  bool timedOut = false;
  const bool ok = HttpDownloader::fetchUrl(apiUrl, [&](const uint8_t* data, size_t n) {
    if (millis() - start > HN_DEADLINE_MS) {
      timedOut = true;
      return false;
    }
    thread.feed(reinterpret_cast<const char*>(data), n);
    return true;
  });
  if ((!ok && thread.keptComments() == 0) || timedOut || thread.failed()) {
    LOG_ERR(TAG, "Algolia comments failed for %s", apiUrl.c_str());
    return false;
  }
  if (thread.keptComments() == 0) return true;  // no comments yet

  char heading[48];
  const int headingLen = snprintf(heading, sizeof(heading), "\n%cComments (%u)\n", DICTIONARY_PLAIN_HEADWORD_MARKER,
                                  static_cast<unsigned>(thread.totalComments()));
  memcpy(work + used, heading, static_cast<size_t>(headingLen));
  used += static_cast<size_t>(headingLen);
  const size_t room = std::min(workCap_ - used - 1, COMMENTS_CAP);
  const size_t htmlLen = thread.render(work + used, room, now_);
  Links commentLinks(item.commentsUrl, used);
  used += htmlToPlainTextInPlace(work + used, htmlLen, DICTIONARY_PLAIN_HEADWORD_MARKER, &commentLinks, true);
  for (auto& l : commentLinks.links) links.push_back(std::move(l));
  return true;
}

// Comments from a WordPress-style comment feed (<post URL>/feed/), flat and in time order. A site
// without such a feed just ends up with no comments section.
void FeedSync::appendCommentFeed(const std::string& feedUrl, const Pending& item, char* work, size_t& used,
                                 std::vector<feed::CachedArticle::Link>& links) {
  constexpr size_t HEADING_ROOM = 48;
  char* html = work + used + HEADING_ROOM;
  const size_t room = std::min(workCap_ - used - HEADING_ROOM - 1, COMMENTS_CAP);
  feed::CommentFeedRenderer renderer(html, room, now_);
  feed::FeedParser parser([&](feed::FeedItem& comment) { return renderer.add(comment); }, COMMENT_TEXT_CAP);
  const unsigned long start = millis();
  HttpDownloader::fetchUrl(feedUrl, [&](const uint8_t* data, size_t n) {
    if (millis() - start > FETCH_DEADLINE_MS) return false;
    return parser.feed(reinterpret_cast<const char*>(data), n);
  });
  parser.feed("", 0, true);
  if (renderer.count() == 0) {
    LOG_INF(TAG, "No comments from %s", feedUrl.c_str());
    return;
  }
  Links commentLinks(item.url, 0);
  const size_t textLen =
      htmlToPlainTextInPlace(html, renderer.size(), DICTIONARY_PLAIN_HEADWORD_MARKER, &commentLinks, true);
  char heading[HEADING_ROOM];
  const int headingLen = snprintf(heading, sizeof(heading), "\n%cComments (%u)\n", DICTIONARY_PLAIN_HEADWORD_MARKER,
                                  static_cast<unsigned>(renderer.count()));
  memmove(work + used + headingLen, html, textLen);  // close the gap the heading leaves
  memcpy(work + used, heading, static_cast<size_t>(headingLen));
  const size_t textStart = used + static_cast<size_t>(headingLen);
  for (auto& l : commentLinks.links) {
    l.start += static_cast<uint32_t>(textStart);
    l.end += static_cast<uint32_t>(textStart);
    links.push_back(std::move(l));
  }
  used = textStart + textLen;
}

bool FeedSync::saveItem(const feed::Source& source, const Pending& item, std::string_view html, bool fragment) {
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
  if (!fragment) {  // trailing related-post thumbnails are not part of the article
    const size_t bodyStart = used - bodyLen;
    used = bodyStart + feed::trimTrailingImages(std::string_view(work + bodyStart, bodyLen));
    std::erase_if(links.links, [used](const feed::CachedArticle::Link& l) { return l.end > used; });
  }

  // Comments: a second page, taken as text under its own heading.
  if (source.comments == feed::CommentsMode::Link && !item.commentsUrl.empty() && used + 4096 < workCap_ &&
      tlsHeapOk()) {
    std::string_view commentsPage;
    std::string commentFeedUrl;
    if (appendHnComments(item, work, used, links.links)) {
      // Hacker News: the threaded comments came from the Algolia API.
    } else if (feed::commentFeedUrlFor(item.url, item.commentsUrl, commentFeedUrl)) {
      // The comments are on the article page itself: only the post's comment feed can supply them.
      appendCommentFeed(commentFeedUrl, item, work, used, links.links);
    } else if (fetchToBuffer(item.commentsUrl, commentsPage) && !commentsPage.empty()) {
      const char* heading =
          "\n\x01"
          "Comments\n";
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
