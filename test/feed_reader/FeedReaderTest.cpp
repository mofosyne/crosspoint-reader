#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "FeedDate.h"
#include "FeedParser.h"
#include "CachedArticle.h"
#include "ContentExtract.h"
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
  EXPECT_EQ(parseFeedDate("Thu, 01 Jan 1970 00:00:00 GMT"), 0);  // the epoch itself reads as unknown
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
    "<description><![CDATA[<a href=\"https://news.ycombinator.com/item?id=50399933\">Comments</a>]]></description></item>"
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
  EXPECT_EQ(items[1].published, 1794007852);  // only <updated>
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
  const std::string xml = "<rss><channel><item><link>https://e.org/a</link><description>" + body +
                          "</description></item></channel></rss>";
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
  EXPECT_TRUE(isDue(s, 0, 1000 * day));                   // never checked
  EXPECT_FALSE(isDue(s, 1000 * day, 1001 * day));          // one day ago
  EXPECT_FALSE(isDue(s, 1000 * day, 1002 * day));          // two days ago
  EXPECT_TRUE(isDue(s, 1000 * day, 1003 * day - 1800));    // within the hour of slack
  EXPECT_TRUE(isDue(s, 1000 * day, 1005 * day));
  EXPECT_TRUE(isDue(s, 1000 * day, 0));                    // clock unknown
  s.everyDays = 0;
  EXPECT_TRUE(isDue(s, 1000 * day, 1000 * day + 5));       // every sync
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
  const std::string html = "<html><body><nav>menu</nav><article><h2>Teaser</h2></article>"
                           "<article><h1>Real</h1><p>" + filler + "</p><article>nested</article></article>"
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
