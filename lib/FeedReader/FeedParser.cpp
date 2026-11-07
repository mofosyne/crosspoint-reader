#include "FeedParser.h"

#include <expat.h>

#include <algorithm>
#include <cstring>

#include "FeedDate.h"

namespace feed {
namespace {

constexpr size_t MAX_FIELD_BYTES = 2048;  // titles, urls, ids and dates

bool eq(const char* a, const char* b) { return strcmp(a, b) == 0; }

void trim(std::string& s) {
  size_t a = 0;
  while (a < s.size() && isspace(static_cast<unsigned char>(s[a]))) ++a;
  size_t b = s.size();
  while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  s = s.substr(a, b - a);
}

const char* attr(const char** attrs, const char* name) {
  for (int i = 0; attrs && attrs[i]; i += 2) {
    if (eq(attrs[i], name)) return attrs[i + 1];
  }
  return nullptr;
}

}  // namespace

FeedParser::FeedParser(Sink sink, size_t maxSummaryBytes) : sink_(std::move(sink)), maxSummary_(maxSummaryBytes) {
  parser_ = XML_ParserCreate(nullptr);
  if (parser_) {
    XML_SetUserData(parser_, this);
    XML_SetElementHandler(parser_, &FeedParser::onStart, &FeedParser::onEnd);
    XML_SetCharacterDataHandler(parser_, &FeedParser::onText);
  }
}

FeedParser::~FeedParser() {
  if (parser_) {
    XML_ParserFree(parser_);
  }
}

void FeedParser::onStart(void* self, const char* name, const char** attrs) {
  static_cast<FeedParser*>(self)->start(name, attrs);
}
void FeedParser::onEnd(void* self, const char* name) { static_cast<FeedParser*>(self)->end(name); }
void FeedParser::onText(void* self, const char* text, int len) { static_cast<FeedParser*>(self)->text(text, len); }

bool FeedParser::feed(const char* data, size_t len, bool last) {
  if (!parser_) {
    error_ = "no parser";
    return false;
  }
  if (stopped_ || failed_) return false;
  // expat takes int lengths; feed big chunks in pieces
  while (len > 0 || last) {
    const int n = static_cast<int>(len > (1u << 20) ? (1u << 20) : len);
    const bool final = last && static_cast<size_t>(n) == len;
    if (XML_Parse(parser_, data, n, final) == XML_STATUS_ERROR) {
      if (!stopped_) {
        failed_ = true;
        error_ = XML_ErrorString(XML_GetErrorCode(parser_));
      }
      return false;
    }
    data += n;
    len -= static_cast<size_t>(n);
    if (final) break;
  }
  return !stopped_;
}

std::string* FeedParser::targetFor(const char* name) {
  // Item fields, by feed type. Elements are matched by their full (prefixed) name.
  switch (kind_) {
    case FeedKind::Rss:
      if (eq(name, "title")) return &item_.title;
      if (eq(name, "link")) return &item_.url;
      if (eq(name, "guid")) return &item_.guid;
      if (eq(name, "pubDate") || eq(name, "dc:date")) return &published_;
      if (eq(name, "comments")) return &item_.commentsUrl;
      if (eq(name, "content:encoded")) return &item_.summary;  // the full text beats <description>
      if (eq(name, "description")) return &desc_;
      return nullptr;
    case FeedKind::Atom:
      if (eq(name, "title")) return &item_.title;
      if (eq(name, "id")) return &item_.guid;
      if (eq(name, "published")) return &published_;
      if (eq(name, "updated")) return &updated_;
      if (eq(name, "content")) return &item_.summary;
      if (eq(name, "summary")) return &desc_;
      return nullptr;
    case FeedKind::Sitemap:
    case FeedKind::SitemapIndex:
      if (eq(name, "loc")) return &item_.url;
      if (eq(name, "lastmod")) return &published_;
      return nullptr;
    default:
      return nullptr;
  }
}

void FeedParser::start(const char* name, const char** attrs) {
  ++depth_;
  if (depth_ == 1) {
    if (eq(name, "rss") || eq(name, "rdf:RDF")) kind_ = FeedKind::Rss;
    else if (eq(name, "feed")) kind_ = FeedKind::Atom;
    else if (eq(name, "urlset")) kind_ = FeedKind::Sitemap;
    else if (eq(name, "sitemapindex")) kind_ = FeedKind::SitemapIndex;
    return;
  }
  const bool itemStart = (kind_ == FeedKind::Rss && eq(name, "item")) || (kind_ == FeedKind::Atom && eq(name, "entry")) ||
                         (kind_ == FeedKind::Sitemap && eq(name, "url")) ||
                         (kind_ == FeedKind::SitemapIndex && eq(name, "sitemap"));
  if (itemStart && !inItem_) {
    inItem_ = true;
    itemDepth_ = depth_;
    item_ = FeedItem{};
    item_.isSitemap = kind_ == FeedKind::SitemapIndex;
    published_.clear();
    updated_.clear();
    desc_.clear();
    capture_ = nullptr;
    return;
  }
  if (!inItem_) {
    // The feed's own title: <channel><title> / <feed><title>, before the first item.
    if (eq(name, "title") && title_.empty() && inChannelTitleScope_ &&
        ((kind_ == FeedKind::Rss && depth_ == 3) || (kind_ == FeedKind::Atom && depth_ == 2))) {
      capture_ = &title_;
      captureDepth_ = depth_;
      captureLimit_ = MAX_FIELD_BYTES;
    }
    return;
  }
  // Atom <link href rel>: an alternate link is the page, a "replies" link is the comments.
  if (kind_ == FeedKind::Atom && eq(name, "link")) {
    const char* href = attr(attrs, "href");
    const char* rel = attr(attrs, "rel");
    if (href) {
      if (!rel || eq(rel, "alternate")) {
        if (item_.url.empty()) item_.url = href;
      } else if (eq(rel, "replies") && item_.commentsUrl.empty()) {
        item_.commentsUrl = href;
      }
    }
    return;
  }
  if (capture_) return;  // nested markup inside a captured field: its text is kept
  if (std::string* target = targetFor(name)) {
    capture_ = target;
    captureDepth_ = depth_;
    captureLimit_ = (target == &item_.summary || target == &desc_) ? maxSummary_ : MAX_FIELD_BYTES;
    target->clear();
  }
}

void FeedParser::text(const char* data, int len) {
  if (!capture_) return;
  const size_t room = captureLimit_ > capture_->size() ? captureLimit_ - capture_->size() : 0;
  capture_->append(data, std::min(static_cast<size_t>(len), room));
}

void FeedParser::end(const char* name) {
  if (capture_ && depth_ == captureDepth_) {
    trim(*capture_);
    capture_ = nullptr;
    if (!inItem_) inChannelTitleScope_ = false;
  }
  if (inItem_ && depth_ == itemDepth_) {
    const bool itemEnd = (kind_ == FeedKind::Rss && eq(name, "item")) || (kind_ == FeedKind::Atom && eq(name, "entry")) ||
                         (kind_ == FeedKind::Sitemap && eq(name, "url")) ||
                         (kind_ == FeedKind::SitemapIndex && eq(name, "sitemap"));
    if (itemEnd) finishItem();
  }
  --depth_;
}

void FeedParser::finishItem() {
  inItem_ = false;
  capture_ = nullptr;
  if (item_.summary.empty()) item_.summary = std::move(desc_);
  if (published_.empty()) published_ = updated_;
  item_.published = parseFeedDate(published_);
  trim(item_.url);
  if (item_.url.empty() && kind_ == FeedKind::Rss && item_.guid.find("://") != std::string::npos) item_.url = item_.guid;
  if (item_.url.empty()) return;  // nothing to open
  ++count_;
  if (sink_ && !sink_(item_)) {
    stopped_ = true;
    XML_StopParser(parser_, XML_FALSE);
  }
}

bool parseFeed(std::string_view xml, std::vector<FeedItem>& out, size_t maxItems, FeedKind* kind) {
  FeedParser parser([&](FeedItem& item) {
    out.push_back(item);
    return out.size() < maxItems;
  });
  const bool ok = parser.feed(xml.data(), xml.size(), true);
  if (kind) *kind = parser.kind();
  return ok || parser.stopped();
}

}  // namespace feed
