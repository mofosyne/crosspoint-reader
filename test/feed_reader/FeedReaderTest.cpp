#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "CachedArticle.h"
#include "CommentFeed.h"
#include "ContentExtract.h"
#include "FeedDate.h"
#include "FeedParser.h"
#include "HnComments.h"
#include "LinkExtractor.h"
#include "SeenSet.h"
#include "SourcesFile.h"

using namespace feed;

TEST(FeedDate, ParsesRfc822) {
  EXPECT_EQ(parseFeedDate("Fri, 06 Nov 2026 23:30:52 +0000"), 1794007852);
  EXPECT_EQ(parseFeedDate("Fri, 06 Nov 2026 23:30:52 GMT"), 1794007852);
  EXPECT_EQ(parseFeedDate("6 Nov 2026 23:30 GMT"), 1794007800);
  EXPECT_EQ(parseFeedDate("Fri, 06 Nov 2026 18:30:52 EST"), 1794007852);  // 5 hours behind UTC
  EXPECT_EQ(parseFeedDate("Sat, 07 Nov 2026 00:30:52 +0100"), 1794007852);
  EXPECT_EQ(parseFeedDate("Thu, 01 Jan 1970 00:00:00 GMT"), 0);         // the epoch itself reads as unknown
  EXPECT_EQ(parseFeedDate("Fri, 06 Nov 26 23:30:52 GMT"), 1794007852);  // two-digit year
}

TEST(FeedDate, ParsesIso8601) {
  EXPECT_EQ(parseFeedDate("2026-11-06T23:30:52Z"), 1794007852);
  EXPECT_EQ(parseFeedDate("2026-11-07T00:30:52+01:00"), 1794007852);
  EXPECT_EQ(parseFeedDate("2026-11-06T23:30:52.123Z"), 1794007852);
  EXPECT_EQ(parseFeedDate("2026-11-06T18:30:52-05:00"), 1794007852);
  EXPECT_EQ(parseFeedDate("2026-11-06"), 1793923200);  // date only: midnight UTC
  EXPECT_EQ(parseFeedDate("  2026-11-06T23:30:52Z \n"), 1794007852);
}

TEST(FeedDate, RejectsGarbage) {
  EXPECT_EQ(parseFeedDate(""), 0);
  EXPECT_EQ(parseFeedDate("yesterday"), 0);
  EXPECT_EQ(parseFeedDate("2026-13-45T25:61:61Z"), 0);
  EXPECT_EQ(parseFeedDate("Fri, 06 Foo 2026 23:30:52 GMT"), 0);
}

static const char* HN_RSS =
    "<rss version=\"2.0\"><channel><title>Hacker News</title><link>https://news.ycombinator.com/</link>"
    "<description>Links for the intellectually curious.</description>"
    "<item><title>Libghostty 1.0</title><link>https://mitchellh.com/writing/libghostty-1-0</link>"
    "<pubDate>Fri, 06 Nov 2026 23:30:52 +0000</pubDate>"
    "<comments>https://news.ycombinator.com/item?id=50399933</comments>"
    "<description><![CDATA[<a "
    "href=\"https://news.ycombinator.com/item?id=50399933\">Comments</a>]]></description></item>"
    "<item><title>Second &amp; third</title><link>https://example.org/2</link>"
    "<guid isPermaLink=\"false\">abc-123</guid></item></channel></rss>";

TEST(FeedParser, ReadsRssItems) {
  std::vector<FeedItem> items;
  FeedKind kind = FeedKind::Unknown;
  ASSERT_TRUE(parseFeed(HN_RSS, items, 100, &kind));
  EXPECT_EQ(kind, FeedKind::Rss);
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].title, "Libghostty 1.0");
  EXPECT_EQ(items[0].url, "https://mitchellh.com/writing/libghostty-1-0");
  EXPECT_EQ(items[0].commentsUrl, "https://news.ycombinator.com/item?id=50399933");
  EXPECT_EQ(items[0].published, 1794007852);
  EXPECT_NE(items[0].summary.find("Comments"), std::string::npos);
  EXPECT_EQ(items[1].title, "Second & third");
  EXPECT_EQ(items[1].guid, "abc-123");
  EXPECT_EQ(items[1].published, 0);
}

TEST(FeedParser, ExposesTheFeedTitle) {
  std::vector<FeedItem> items;
  FeedParser parser([&](FeedItem& i) {
    items.push_back(i);
    return true;
  });
  const std::string xml = HN_RSS;
  ASSERT_TRUE(parser.feed(xml.data(), xml.size(), true));
  EXPECT_EQ(parser.title(), "Hacker News");  // not the first item's title
  EXPECT_EQ(parser.itemCount(), 2u);
}

TEST(FeedParser, FullContentBeatsDescription) {
  const char* xml =
      "<rss version=\"2.0\" xmlns:content=\"http://purl.org/rss/1.0/modules/content/\"><channel><title>T</title>"
      "<item><title>A</title><link>https://e.org/a</link><description>short</description>"
      "<content:encoded><![CDATA[<p>the whole article</p>]]></content:encoded></item></channel></rss>";
  std::vector<FeedItem> items;
  ASSERT_TRUE(parseFeed(xml, items));
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].summary, "<p>the whole article</p>");
}

TEST(FeedParser, ReadsAtomEntries) {
  const char* xml =
      "<?xml version=\"1.0\"?><feed xmlns=\"http://www.w3.org/2005/Atom\"><title>Blog</title>"
      "<link href=\"https://blog.example/\"/>"
      "<entry><title>Post one</title><link rel=\"alternate\" href=\"https://blog.example/one\"/>"
      "<link rel=\"replies\" href=\"https://blog.example/one#comments\"/>"
      "<id>tag:blog.example,2026:1</id><updated>2026-11-06T23:30:52Z</updated>"
      "<published>2026-11-05T10:00:00Z</published><summary>About one</summary></entry>"
      "<entry><title>Post two</title><link href=\"https://blog.example/two\"/>"
      "<updated>2026-11-06T23:30:52Z</updated><content type=\"html\">&lt;p&gt;body&lt;/p&gt;</content>"
      "<summary>ignored</summary></entry></feed>";
  std::vector<FeedItem> items;
  FeedKind kind;
  ASSERT_TRUE(parseFeed(xml, items, 100, &kind));
  EXPECT_EQ(kind, FeedKind::Atom);
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].url, "https://blog.example/one");  // the feed-level link is not an item
  EXPECT_EQ(items[0].commentsUrl, "https://blog.example/one#comments");
  EXPECT_EQ(items[0].published, 1793872800);  // <published> wins over <updated>
  EXPECT_EQ(items[0].summary, "About one");
  EXPECT_EQ(items[1].published, 1794007852);   // only <updated>
  EXPECT_EQ(items[1].summary, "<p>body</p>");  // <content> wins over <summary>
}

TEST(FeedParser, ReadsSitemaps) {
  const char* xml =
      "<?xml version=\"1.0\"?><urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">"
      "<url><loc>https://s.example/a</loc><lastmod>2026-11-01</lastmod></url>"
      "<url><loc>https://s.example/b</loc></url></urlset>";
  std::vector<FeedItem> items;
  FeedKind kind;
  ASSERT_TRUE(parseFeed(xml, items, 100, &kind));
  EXPECT_EQ(kind, FeedKind::Sitemap);
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].url, "https://s.example/a");
  EXPECT_EQ(items[0].published, 1793491200);
  EXPECT_EQ(items[1].published, 0);
  EXPECT_FALSE(items[0].isSitemap);
}

TEST(FeedParser, SitemapIndexListsChildSitemaps) {
  const char* xml =
      "<sitemapindex xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">"
      "<sitemap><loc>https://s.example/posts.xml</loc><lastmod>2026-11-01</lastmod></sitemap></sitemapindex>";
  std::vector<FeedItem> items;
  FeedKind kind;
  ASSERT_TRUE(parseFeed(xml, items, 100, &kind));
  EXPECT_EQ(kind, FeedKind::SitemapIndex);
  ASSERT_EQ(items.size(), 1u);
  EXPECT_TRUE(items[0].isSitemap);
  EXPECT_EQ(items[0].url, "https://s.example/posts.xml");
}

TEST(FeedParser, ParsesInSmallChunks) {
  const std::string xml = HN_RSS;
  std::vector<FeedItem> items;
  FeedParser parser([&](FeedItem& i) {
    items.push_back(i);
    return true;
  });
  for (size_t i = 0; i < xml.size(); i += 7) {
    ASSERT_TRUE(parser.feed(xml.data() + i, std::min<size_t>(7, xml.size() - i), i + 7 >= xml.size()));
  }
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].title, "Libghostty 1.0");
}

TEST(FeedParser, StopsWhenTheSinkSaysEnough) {
  std::vector<FeedItem> items;
  EXPECT_TRUE(parseFeed(HN_RSS, items, 1));
  ASSERT_EQ(items.size(), 1u);
}

TEST(FeedParser, KeepsItemsBeforeAnXmlError) {
  const char* xml =
      "<rss><channel><item><title>A</title><link>https://e.org/a</link></item>"
      "<item><title>B &nbsp; broken</title><link>https://e.org/b</link></item></channel></rss>";  // &nbsp; is not XML
  std::vector<FeedItem> items;
  FeedParser parser([&](FeedItem& i) {
    items.push_back(i);
    return true;
  });
  EXPECT_FALSE(parser.feed(xml, strlen(xml), true));
  EXPECT_FALSE(parser.stopped());
  EXPECT_FALSE(parser.error().empty());
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].title, "A");
}

TEST(FeedParser, CapsLongSummaries) {
  const std::string body(50000, 'x');
  const std::string xml =
      "<rss><channel><item><link>https://e.org/a</link><description>" + body + "</description></item></channel></rss>";
  std::vector<FeedItem> items;
  FeedParser parser(
      [&](FeedItem& i) {
        items.push_back(i);
        return true;
      },
      1000);
  ASSERT_TRUE(parser.feed(xml.data(), xml.size(), true));
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].summary.size(), 1000u);
}

TEST(FeedParser, SkipsItemsWithoutALink) {
  std::vector<FeedItem> items;
  ASSERT_TRUE(parseFeed("<rss><channel><item><title>no link</title></item></channel></rss>", items));
  EXPECT_TRUE(items.empty());
}

TEST(FeedParser, UsesAPermalinkGuidWhenThereIsNoLink) {
  std::vector<FeedItem> items;
  ASSERT_TRUE(parseFeed("<rss><channel><item><guid>https://e.org/x</guid></item></channel></rss>", items));
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].url, "https://e.org/x");
}

TEST(ResolveUrl, HandlesTheUsualForms) {
  const std::string base = "https://Blog.Example/posts/2026/index.html?x=1";
  EXPECT_EQ(resolveUrl(base, "https://other.example/a#top"), "https://other.example/a");
  EXPECT_EQ(resolveUrl(base, "//cdn.example/x.js"), "https://cdn.example/x.js");
  EXPECT_EQ(resolveUrl(base, "/about"), "https://blog.example/about");
  EXPECT_EQ(resolveUrl(base, "next.html"), "https://blog.example/posts/2026/next.html");
  EXPECT_EQ(resolveUrl(base, "../2025/old.html"), "https://blog.example/posts/2025/old.html");
  EXPECT_EQ(resolveUrl(base, "./a/../b"), "https://blog.example/posts/2026/b");
  EXPECT_EQ(resolveUrl(base, "?page=2"), "https://blog.example/posts/2026/index.html?page=2");
  EXPECT_EQ(resolveUrl("https://e.org", "item?id=5"), "https://e.org/item?id=5");
  EXPECT_EQ(resolveUrl("https://e.org/a/b/", "../../../../x"), "https://e.org/x");
}

TEST(ResolveUrl, RejectsOtherSchemesAndAnchors) {
  const std::string base = "https://e.org/a";
  EXPECT_EQ(resolveUrl(base, "mailto:a@b.c"), "");
  EXPECT_EQ(resolveUrl(base, "javascript:void(0)"), "");
  EXPECT_EQ(resolveUrl(base, "#top"), "");
  EXPECT_EQ(resolveUrl(base, ""), "");
  EXPECT_EQ(resolveUrl("not a url", "x"), "");
}

static const char* INDEX_HTML =
    "<html><head><title> Small &amp; Blog\n</title>"
    "<link rel=\"alternate\" type=\"application/rss+xml\" href=\"/feed.xml\"></head><body>"
    "<a href=\"/about\">About</a>"
    "<a href=\"/posts/one\">First <b>post</b></a> <a class=x href='/posts/two#c'>Second</a>"
    "<a href=\"/posts/one\">First post, again but longer</a>"
    "<a href=\"https://elsewhere.example/posts/x\">external</a>"
    "<a href=\"mailto:me@e.org\">mail</a><a>no href</a></body></html>";

TEST(LinkExtractor, KeepsLinksThatMatchThePattern) {
  const auto links = extractLinks(INDEX_HTML, "https://blog.example/", "/posts/");
  ASSERT_EQ(links.size(), 2u);
  EXPECT_EQ(links[0].url, "https://blog.example/posts/one");
  EXPECT_EQ(links[0].text, "First post, again but longer");  // the longer text of a repeated link
  EXPECT_EQ(links[1].url, "https://blog.example/posts/two");
}

TEST(LinkExtractor, NoPatternMeansSameHostLinks) {
  const auto links = extractLinks(INDEX_HTML, "https://blog.example/", "");
  EXPECT_EQ(links.size(), 3u);  // about, one, two: not the external one or the mailto
}

TEST(LinkExtractor, ACrossHostPatternMayLeaveTheSite) {
  const auto links = extractLinks(INDEX_HTML, "https://blog.example/", "https://elsewhere.example/");
  ASSERT_EQ(links.size(), 1u);
  EXPECT_EQ(links[0].text, "external");
}

TEST(LinkExtractor, RespectsTheLimit) {
  EXPECT_EQ(extractLinks(INDEX_HTML, "https://blog.example/", "", 1).size(), 1u);
}

TEST(LinkExtractor, FindsAdvertisedFeedAndTitle) {
  EXPECT_EQ(findFeedLink(INDEX_HTML, "https://blog.example/"), "https://blog.example/feed.xml");
  EXPECT_EQ(findFeedLink("<html><head></head></html>", "https://blog.example/"), "");
  EXPECT_EQ(pageTitle(INDEX_HTML), "Small & Blog");
  EXPECT_EQ(pageTitle("<p>none</p>"), "");
}

TEST(SourcesFile, ParsesAllThreeKindsAndOptions) {
  const auto r = parseSources(
      "# my sources\n"
      "feed https://news.ycombinator.com/rss every=1 content=auto comments=link limit=30\n"
      "\n"
      "site https://smallblog.example/ every=3 links=/posts/ name=\"Small blog\"  # a comment\n"
      "sitemap https://other.example/sitemap.xml every=7 limit=10\n");
  EXPECT_TRUE(r.errors.empty());
  ASSERT_EQ(r.sources.size(), 3u);
  EXPECT_EQ(r.sources[0].kind, SourceKind::Feed);
  EXPECT_EQ(r.sources[0].comments, CommentsMode::Link);
  EXPECT_EQ(r.sources[0].limit, 30);
  EXPECT_EQ(r.sources[0].name, "news.ycombinator.com");  // defaults to the host
  EXPECT_EQ(r.sources[1].kind, SourceKind::Site);
  EXPECT_EQ(r.sources[1].name, "Small blog");
  EXPECT_EQ(r.sources[1].linkPattern, "/posts/");
  EXPECT_EQ(r.sources[1].everyDays, 3);
  EXPECT_EQ(r.sources[2].kind, SourceKind::Sitemap);
  EXPECT_EQ(r.sources[2].everyDays, 7);
  EXPECT_EQ(r.sources[0].id.size(), 8u);
  EXPECT_NE(r.sources[0].id, r.sources[1].id);
  EXPECT_EQ(r.sources[0].id, sourceId("https://news.ycombinator.com/rss"));
}

TEST(SourcesFile, ReportsBadLinesAndKeepsTheGoodOnes) {
  const auto r = parseSources(
      "rss https://a.example/feed\n"
      "feed ftp://a.example/feed\n"
      "feed https://a.example/feed evry=2\n"
      "feed https://a.example/feed every=999\n"
      "feed https://a.example/feed content=maybe\n"
      "feed https://a.example/feed novalue\n"
      "feed https://ok.example/feed\n");
  ASSERT_EQ(r.sources.size(), 1u);
  EXPECT_EQ(r.sources[0].url, "https://ok.example/feed");
  ASSERT_EQ(r.errors.size(), 6u);
  EXPECT_NE(r.errors[0].find("line 1"), std::string::npos);
  EXPECT_NE(r.errors[2].find("evry"), std::string::npos);
}

TEST(SourcesFile, DueLogic) {
  Source s;
  s.everyDays = 3;
  const int64_t day = 86400;
  EXPECT_TRUE(isDue(s, 0, 1000 * day));                  // never checked
  EXPECT_FALSE(isDue(s, 1000 * day, 1001 * day));        // one day ago
  EXPECT_FALSE(isDue(s, 1000 * day, 1002 * day));        // two days ago
  EXPECT_TRUE(isDue(s, 1000 * day, 1003 * day - 1800));  // within the hour of slack
  EXPECT_TRUE(isDue(s, 1000 * day, 1005 * day));
  EXPECT_TRUE(isDue(s, 1000 * day, 0));  // clock unknown
  s.everyDays = 0;
  EXPECT_TRUE(isDue(s, 1000 * day, 1000 * day + 5));  // every sync
}

TEST(SeenSet, RemembersUrlsAndForgetsTheOldest) {
  SeenSet seen(3);
  EXPECT_TRUE(seen.add("https://e.org/1"));
  EXPECT_FALSE(seen.add("https://e.org/1"));
  seen.add("https://e.org/2");
  seen.add("https://e.org/3");
  seen.add("https://e.org/4");  // pushes out /1
  EXPECT_EQ(seen.size(), 3u);
  EXPECT_FALSE(seen.contains("https://e.org/1"));
  EXPECT_TRUE(seen.contains("https://e.org/4"));
}

TEST(SeenSet, RoundTripsThroughABlob) {
  SeenSet a;
  a.add("https://e.org/1");
  a.add("https://e.org/2");
  const std::string blob = a.serialize(1794007852);
  SeenSet b;
  int64_t last = 0;
  ASSERT_TRUE(b.parse(blob, last));
  EXPECT_EQ(last, 1794007852);
  EXPECT_TRUE(b.contains("https://e.org/2"));
  EXPECT_EQ(b.size(), 2u);
  EXPECT_FALSE(b.parse("garbage", last));
  EXPECT_FALSE(b.parse(blob.substr(0, blob.size() - 3), last));  // truncated
}

TEST(CachedArticle, RoundTripsTextAndLinks) {
  const std::string text = "Hello world\nSecond line with a link here.";
  CachedArticle in;
  in.url = "https://e.org/post";
  in.title = "A title\nwith a newline";
  in.sourceId = "deadbeef";
  in.commentsUrl = "https://e.org/post#comments";
  in.published = 1794007852;
  in.fetched = 1794010000;
  in.read = true;
  in.text = text;
  in.links.push_back({31, 35, "https://e.org/linked"});
  std::string file;
  ASSERT_TRUE(serializeArticle(in, [&](const char* d, size_t n) {
    file.append(d, n);
    return true;
  }));
  CachedArticle out;
  ASSERT_TRUE(parseArticle(file, out));
  EXPECT_EQ(out.url, in.url);
  EXPECT_EQ(out.title, "A title with a newline");
  EXPECT_EQ(out.commentsUrl, in.commentsUrl);
  EXPECT_EQ(out.published, in.published);
  EXPECT_TRUE(out.read);
  EXPECT_EQ(out.text, text);
  ASSERT_EQ(out.links.size(), 1u);
  EXPECT_EQ(out.text.substr(out.links[0].start, out.links[0].end - out.links[0].start), "link");
  EXPECT_EQ(out.links[0].url, "https://e.org/linked");
  EXPECT_EQ(out.text.data() - file.data() > 0, true);  // zero-copy: the text points into the buffer
}

TEST(CachedArticle, HeaderCanBeReadWithoutTheText) {
  CachedArticle in;
  in.url = "https://e.org/x";
  in.title = "T";
  in.text = std::string(10000, 'a');
  std::string file;
  serializeArticle(in, [&](const char* d, size_t n) {
    file.append(d, n);
    return true;
  });
  CachedArticle head;
  ASSERT_TRUE(parseArticleHeader(std::string_view(file).substr(0, 300), head));
  EXPECT_EQ(head.title, "T");
  EXPECT_TRUE(head.text.empty());
}

TEST(CachedArticle, RejectsDamagedFiles) {
  CachedArticle out;
  EXPECT_FALSE(parseArticle("not an article", out));
  CachedArticle in;
  in.text = "0123456789";
  std::string file;
  serializeArticle(in, [&](const char* d, size_t n) {
    file.append(d, n);
    return true;
  });
  EXPECT_FALSE(parseArticle(file.substr(0, file.size() - 8), out));  // text cut short
}

TEST(CachedArticle, DropsStaleLinkRanges) {
  CachedArticle in;
  in.text = "short";
  in.links.push_back({2, 99, "https://e.org/past-the-end"});
  in.links.push_back({3, 3, "https://e.org/empty"});
  std::string file;
  serializeArticle(in, [&](const char* d, size_t n) {
    file.append(d, n);
    return true;
  });
  CachedArticle out;
  ASSERT_TRUE(parseArticle(file, out));
  EXPECT_TRUE(out.links.empty());
}

TEST(ContentExtract, PrefersTheLargestArticle) {
  const std::string filler(300, 'x');
  const std::string html =
      "<html><body><nav>menu</nav><article><h2>Teaser</h2></article>"
      "<article><h1>Real</h1><p>" +
      filler +
      "</p><article>nested</article></article>"
      "<footer>foot</footer></body></html>";
  const auto main = extractMainContent(html);
  EXPECT_NE(main.find("Real"), std::string_view::npos);
  EXPECT_EQ(main.find("menu"), std::string_view::npos);
  EXPECT_EQ(main.find("foot"), std::string_view::npos);
  EXPECT_EQ(main.find("Teaser"), std::string_view::npos);
}

TEST(ContentExtract, FallsBackToMainThenIdThenBody) {
  const std::string filler(300, 'y');
  EXPECT_NE(extractMainContent("<body><header>h</header><main><p>" + filler + "</p></main></body>").find(filler),
            std::string_view::npos);
  const std::string byId = "<body><div id=\"sidebar\">side</div><div id=\"content\"><p>" + filler + "</p></div></body>";
  const auto viaId = extractMainContent(byId);
  EXPECT_NE(viaId.find(filler), std::string_view::npos);
  EXPECT_EQ(viaId.find("side"), std::string_view::npos);
  const auto body = extractMainContent("<html><head><title>t</title></head><body><p>just text</p></body></html>");
  EXPECT_EQ(body, "<p>just text</p>");
  EXPECT_EQ(extractMainContent("plain text"), "plain text");
}

// ---- Hacker News comments (Algolia) -----------------------------------------------------------------

namespace {
// Algolia lists "children" before "text", and a reply nests inside its parent.
const char* HN_JSON =
    R"({"author":"op","children":[)"
    R"({"author":"alice","children":[)"
    R"({"author":"bob","children":[],"created_at_i":1000300,"id":12,"parent_id":11,"text":"<p>Deep reply &#x27;x&#x27;","type":"comment"})"
    R"(],"created_at_i":1000100,"id":11,"parent_id":10,"text":"First<p>second <i>para</i>","type":"comment"},)"
    R"({"author":null,"children":[],"created_at_i":1000200,"id":13,"parent_id":10,"text":null,"type":"comment"})"
    R"(],"created_at_i":1000000,"id":10,"options":[],"points":5,"story_id":10,"text":null,"title":"Story","type":"story"})";

std::string renderThread(const char* json, HnLimits limits, size_t feedStep, int64_t now, HnThread** keep = nullptr) {
  static std::vector<char> pool(64 * 1024);
  static std::vector<char> out(64 * 1024);
  static std::unique_ptr<HnThread> thread;
  thread = std::make_unique<HnThread>(pool.data(), pool.size(), limits);
  const std::string_view all(json);
  for (size_t i = 0; i < all.size(); i += feedStep) thread->feed(all.data() + i, std::min(feedStep, all.size() - i));
  if (keep) *keep = thread.get();
  return std::string(out.data(), thread->render(out.data(), out.size(), now));
}
}  // namespace

TEST(HnComments, ApiUrl) {
  std::string url;
  EXPECT_TRUE(HnThread::apiUrlFor("https://news.ycombinator.com/item?id=50402535", url));
  EXPECT_EQ(url, "https://hn.algolia.com/api/v1/items/50402535");
  EXPECT_FALSE(HnThread::apiUrlFor("https://example.com/item?id=5", url));
  EXPECT_FALSE(HnThread::apiUrlFor("https://news.ycombinator.com/item?id=", url));
  EXPECT_FALSE(HnThread::apiUrlFor("https://news.ycombinator.com/news", url));
}

TEST(HnComments, RendersRepliesInReadingOrder) {
  HnThread* t = nullptr;
  const std::string html = renderThread(HN_JSON, {}, 7, 1000000 + 7200, &t);  // fed in 7-byte pieces
  EXPECT_EQ(t->totalComments(), 3u);
  EXPECT_EQ(t->keptComments(), 3u);
  EXPECT_FALSE(t->failed());
  const size_t alice = html.find("&gt; alice");
  const size_t bob = html.find("&gt;&gt; bob");
  const size_t deleted = html.find("&gt; [deleted]");
  ASSERT_NE(alice, std::string::npos);
  ASSERT_NE(bob, std::string::npos);
  ASSERT_NE(deleted, std::string::npos);
  EXPECT_LT(alice, bob);    // a reply follows its parent...
  EXPECT_LT(bob, deleted);  // ...and comes before the next top-level comment
  EXPECT_NE(html.find("First<p>second <i>para</i>"), std::string::npos);
  EXPECT_NE(html.find("[no text]"), std::string::npos);
  EXPECT_NE(html.find("</b> 1h"), std::string::npos);  // alice: 100 s after the story, now is 7200 s later
}

TEST(HnComments, LimitsCountTheRest) {
  HnLimits limits;
  limits.maxComments = 2;
  HnThread* t = nullptr;
  const std::string html = renderThread(HN_JSON, limits, 4096, 0, &t);
  EXPECT_EQ(t->totalComments(), 3u);
  EXPECT_EQ(t->keptComments(), 2u);
  EXPECT_NE(html.find("[1 more comments not shown]"), std::string::npos);
  EXPECT_NE(html.find("bob"), std::string::npos);  // bob's parent was cut: bob is shown at top level
}

TEST(HnComments, TruncatesLongTextOnACharacterBoundary) {
  const std::string text = std::string(10, 'a') + "\\u00e9\\u00e9\\u00e9";  // 10 bytes + three 2-byte characters
  const std::string json =
      R"({"author":"op","children":[{"author":"x","children":[],"created_at_i":5,"id":2,"parent_id":1,"text":")" +
      text + R"(","type":"comment"}],"id":1,"type":"story"})";
  HnLimits limits;
  limits.maxCommentBytes = 11;  // would split the first é
  const std::string html = renderThread(json.c_str(), limits, 4096, 0);
  ASSERT_GE(html.size(), 10u);
  EXPECT_EQ(html.substr(html.size() - 10), "aaaaaaaaaa") << html;  // cut at 10 bytes, no half character
}

TEST(HnComments, BrokenJsonKeepsWhatWasRead) {
  HnThread* t = nullptr;
  renderThread(R"({"author":"op","children":[{"author":"x","children":[],"id":2,"parent_id":1,"text":"hi"},{"aut)", {},
               4096, 0, &t);
  EXPECT_EQ(t->keptComments(), 1u);
  EXPECT_FALSE(t->failed());
}

// ---- WordPress comment feeds --------------------------------------------------------------------------

TEST(CommentFeed, UrlForPostsWhoseCommentsAreOnThePage) {
  std::string url;
  EXPECT_TRUE(commentFeedUrlFor("https://hackaday.com/2026/11/07/post/",
                                "https://hackaday.com/2026/11/07/post/#comments", url));
  EXPECT_EQ(url, "https://hackaday.com/2026/11/07/post/feed/");
  EXPECT_TRUE(
      commentFeedUrlFor("https://blog.example/post", "https://blog.example/post#respond", url));  // no trailing slash
  EXPECT_EQ(url, "https://blog.example/post/feed/");
  EXPECT_TRUE(commentFeedUrlFor("https://blog.example/post/", "https://blog.example/post/", url));  // same URL
  EXPECT_FALSE(commentFeedUrlFor("https://blog.example/post/", "https://forum.example/thread/1", url));
  EXPECT_FALSE(commentFeedUrlFor("https://blog.example/?p=12", "https://blog.example/?p=12#comments", url));
}

TEST(CommentFeed, RendersCommentsFromAWordPressFeed) {
  const char* xml =
      R"(<?xml version="1.0"?><rss version="2.0" xmlns:dc="http://purl.org/dc/elements/1.1/"><channel><title>Comments on: Post</title>)"
      R"(<item><title>By: Ostracus</title><link>https://h.example/p/#comment-1</link><dc:creator><![CDATA[Ostracus]]></dc:creator>)"
      R"(<pubDate>Sat, 07 Nov 2026 13:37:26 +0000</pubDate><description><![CDATA[Bang for the buck.]]></description></item>)"
      R"(<item><title>By: Jo</title><link>https://h.example/p/#comment-2</link><pubDate>Sat, 07 Nov 2026 14:37:26 +0000</pubDate>)"
      R"(<description><![CDATA[<p>Agreed</p>]]></description></item></channel></rss>)";
  std::vector<char> buf(4096);
  CommentFeedRenderer renderer(buf.data(), buf.size(), 1794058646 + 3 * 3600 + 30);
  FeedParser parser([&](FeedItem& item) { return renderer.add(item); }, 2048);
  parser.feed(xml, strlen(xml), true);
  const std::string html(buf.data(), renderer.size());
  EXPECT_EQ(renderer.count(), 2u);
  EXPECT_NE(html.find("<p><b>&gt; Ostracus</b> 3h</p>Bang for the buck."), std::string::npos) << html;
  EXPECT_NE(html.find("<p><b>&gt; Jo</b> 2h</p><p>Agreed</p>"), std::string::npos) << html;
}

TEST(CommentFeed, StopsAtTheLimit) {
  std::vector<char> buf(4096);
  CommentFeedRenderer renderer(buf.data(), buf.size(), 0, 2);
  FeedItem c;
  c.title = "By: A";
  c.summary = "x";
  EXPECT_TRUE(renderer.add(c));
  EXPECT_TRUE(renderer.add(c));
  EXPECT_FALSE(renderer.add(c));
  EXPECT_EQ(renderer.count(), 2u);
}

// ---- extraction of plain pages -------------------------------------------------------------------------

TEST(ContentExtract, PicksTheContainerWithTheProse) {
  const std::string prose(600, 'w');
  const std::string page =
      "<html><body><header id=header><ul><li><a href=/a>About</a></li><li><a href=/b>Donate</a></li></ul></header>"
      "<section id=post-section><h1>Title</h1><p>" +
      prose + "</p></section><footer><a href=/c>Contact</a></footer></body></html>";
  const std::string_view main = extractMainContent(page);
  EXPECT_NE(main.find(prose), std::string_view::npos);
  EXPECT_EQ(main.find("About"), std::string_view::npos);
  EXPECT_EQ(main.find("Contact"), std::string_view::npos);
}

TEST(ContentExtract, KeepsTheWholeBodyWhenNothingDominates) {
  const std::string a(500, 'a');
  const std::string b(500, 'b');
  const std::string page = "<body><div>" + a + "</div><div>" + b + "</div></body>";
  const std::string_view main = extractMainContent(page);
  EXPECT_NE(main.find(a), std::string_view::npos);
  EXPECT_NE(main.find(b), std::string_view::npos);
}

TEST(ContentExtract, DescendsThroughWrappers) {
  const std::string prose(700, 'p');
  const std::string page = "<body><div id=wrap><nav><a href=/x>Home</a></nav><div class=c><div class=d><p>" + prose +
                           "</p></div></div></div></body>";
  const std::string_view main = extractMainContent(page);
  EXPECT_EQ(main.find("Home"), std::string_view::npos);
  EXPECT_NE(main.find(prose), std::string_view::npos);
}

TEST(ContentExtract, DropsTheHeadingThatRepeatsTheTitle) {
  const std::string_view page =
      "<header><h1 class=\"t\">The Hidden Microcontroller Inside A LoRa Chip</h1><p>by Jo</p></header><p>text</p>";
  EXPECT_EQ(dropLeadingTitle(page, "The Hidden Microcontroller Inside a LoRa chip"),
            "<p>by Jo</p></header><p>text</p>");
  const std::string_view quoted = "<h1>It&#8217;s Alpine Linux &amp; Kindles</h1><p>x</p>";
  EXPECT_EQ(dropLeadingTitle(quoted, "It\xE2\x80\x99s Alpine Linux & Kindles"), "<p>x</p>");
  EXPECT_EQ(dropLeadingTitle(quoted, "Something else entirely"), quoted);
  EXPECT_EQ(dropLeadingTitle("<p>no heading</p>", "A long enough title"), "<p>no heading</p>");
  EXPECT_EQ(dropLeadingTitle("<h1>Short</h1>", "Short"), "<h1>Short</h1>");  // too short to trust
  // A site name often comes first.
  EXPECT_EQ(
      dropLeadingTitle("<h1><a href=/>My Blog</a></h1><h2>Libghostty 1.0 is out</h2><p>x</p>", "Libghostty 1.0 is out"),
      "<p>x</p>");
}

TEST(ContentExtract, TrimsBareImagePlaceholdersAtTheEnd) {
  EXPECT_EQ(trimTrailingImages("Text.\n\n[image]\n[image]\n[image]"), 5u);
  EXPECT_EQ(trimTrailingImages("Text.\n[image: a chart]"), 22u);  // alt text is kept
  EXPECT_EQ(trimTrailingImages("Text.\nsee [image]"), 17u);       // part of a line
  EXPECT_EQ(trimTrailingImages("Text.\n[image]\nMore text.\n"), 25u);
  EXPECT_EQ(trimTrailingImages("[image]"), 0u);
}
