// Host tests for htmlToPlainText(), used to render dictionary definitions that
// cannot be laid out as styled pages.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "HtmlToPlainText.h"

namespace {

TEST(HtmlToPlainText, ReusesOwnedBufferForLargeFallback) {
  const std::string text(60 * 1024, 'a');
  std::string html = "<p>" + text + "</p>";
  const char* buffer = html.data();
  const auto output = htmlToPlainText(std::move(html), '\x01');
  EXPECT_EQ(output, text);
  EXPECT_EQ(output.data(), buffer);
}

TEST(HtmlToPlainText, StripsTagsAndKeepsText) {
  EXPECT_EQ(htmlToPlainText("<b>quixotic</b> <i>adj.</i>", '\x01'), "quixotic adj.");
  EXPECT_EQ(htmlToPlainText("<span class=\"x\">in span</span>", '\x01'), "in span");
  EXPECT_EQ(htmlToPlainText("plain", '\x01'), "plain");
  EXPECT_EQ(htmlToPlainText("", '\x01'), "");
}

TEST(HtmlToPlainText, BlockElementsBecomeBreaks) {
  EXPECT_EQ(htmlToPlainText("a<br>b", '\x01'), "a\nb");
  EXPECT_EQ(htmlToPlainText("<div>a</div><div>b</div>", '\x01'), "a\nb");
  EXPECT_EQ(htmlToPlainText("<p>a</p><p>b</p>", '\x01'), "a\n\nb");
  // Consecutive breaks do not stack up.
  EXPECT_EQ(htmlToPlainText("a<br><br><br>b", '\x01'), "a\nb");
}

TEST(HtmlToPlainText, HeadingsBecomeBoldLabelLines) {
  // tagBreak() lists h1-h6 among the heading names, so they must actually
  // reach that comparison -- a name scan that stops at the first digit
  // reads "h1" as "h" and the heading runs into the text after it.
  const char marker = '\x01';
  for (const char* tag : {"h1", "h2", "h3", "h4", "h5", "h6"}) {
    const std::string html = std::string("<") + tag + ">title</" + tag + ">body";
    EXPECT_EQ(htmlToPlainText(html, marker), std::string(1, marker) + "title\nbody") << tag;
  }
  // The marker only means something at the start of a line, so a heading that
  // follows text still starts its own line.
  EXPECT_EQ(htmlToPlainText("lead<h2>title</h2>body", marker), "lead\n" + std::string(1, marker) + "title\nbody");
  EXPECT_EQ(htmlToPlainText("<hr>after", marker), "after");
}

TEST(HtmlToPlainText, DecodesEntities) {
  EXPECT_EQ(htmlToPlainText("Tom &amp; Jerry", '\x01'), "Tom & Jerry");
  EXPECT_EQ(htmlToPlainText("a&nbsp;b", '\x01'),
            "a\xC2\xA0"
            "b");
  EXPECT_EQ(htmlToPlainText("&#65;&#66;", '\x01'), "AB");
  EXPECT_EQ(htmlToPlainText("&#x2014;", '\x01'), "\xE2\x80\x94");
  // Not entities: left as written rather than swallowed.
  EXPECT_EQ(htmlToPlainText("&notanentity;", '\x01'), "&notanentity;");
  EXPECT_EQ(htmlToPlainText("100% & up", '\x01'), "100% & up");
  EXPECT_EQ(htmlToPlainText("<p>Literal &lt;b&gt;word&lt;/b&gt; and &#60;tag&#62;</p>", '\x01'),
            "Literal <b>word</b> and <tag>");
}

TEST(HtmlToPlainText, TrimsSurroundingWhitespace) {
  EXPECT_EQ(htmlToPlainText("<p>only</p>", '\x01'), "only");
  EXPECT_EQ(htmlToPlainText("text   ", '\x01'), "text");
  EXPECT_EQ(htmlToPlainText("a\tb", '\x01'), "a b");
  EXPECT_EQ(htmlToPlainText("<br>a", '\x01'), "a");
}

TEST(HtmlToPlainText, SurvivesMalformedMarkup) {
  EXPECT_EQ(htmlToPlainText("a <b", '\x01'), "a <b");
  EXPECT_EQ(htmlToPlainText("x < y", '\x01'), "x < y");
  EXPECT_EQ(htmlToPlainText("<!-- comment -->kept", '\x01'), "kept");
}

TEST(HtmlToPlainText, DropsHeadStyleAndScriptContent) {
  EXPECT_EQ(htmlToPlainText("<html><head><title>T</title><style>p { color: red; }</style></head>"
                            "<body><p>Body text</p></body></html>",
                            '\x01'),
            "Body text");
  EXPECT_EQ(htmlToPlainText("a<style>x{}</style>b<script src=\"x.js\">var a = 1 < 2;</script>c", '\x01'), "abc");
  EXPECT_EQ(htmlToPlainText("<HEAD><STYLE>x</STYLE></HEAD>kept", '\x01'), "kept");
}

TEST(HtmlToPlainText, KeepsTextAroundHiddenElements) {
  EXPECT_EQ(htmlToPlainText("one <style>hidden</style>two", '\x01'), "one two");
  // Similar names are not hidden elements.
  EXPECT_EQ(htmlToPlainText("<header>shown</header><scripture>too</scripture>", '\x01'), "showntoo");
}

TEST(HtmlToPlainText, UnclosedHiddenElementEndsTheText) {
  EXPECT_EQ(htmlToPlainText("before<style>never closed", '\x01'), "before");
  EXPECT_EQ(htmlToPlainText("before<script/>after", '\x01'), "beforeafter");  // self-closing: nothing to skip
}

TEST(HtmlToPlainText, SuperscriptGetsACaret) {
  EXPECT_EQ(htmlToPlainText("7.3\xC3\x97"
                            "10<sup>22</sup> kg",
                            '\x01'),
            "7.3\xC3\x97"
            "10^22 kg");
  EXPECT_EQ(htmlToPlainText("km<SUP>2</SUP>", '\x01'), "km^2");
  EXPECT_EQ(htmlToPlainText("<super>x</super>", '\x01'), "x");  // similar name
}

TEST(HtmlToPlainText, FootnoteSuperscriptsAreDropped) {
  EXPECT_EQ(htmlToPlainText("fact<sup id=\"c1\" class=\"mw-ref reference\"><a href=\"#n1\">[1]</a></sup>.", '\x01'),
            "fact.");
  EXPECT_EQ(htmlToPlainText("claim<sup class='noprint Inline-Template'>[citation needed]</sup>", '\x01'), "claim");
}

TEST(HtmlToPlainText, ImageWithoutAltBecomesPlaceholder) {
  EXPECT_EQ(htmlToPlainText("a<img src=\"../I/photo.webp\" width=\"290\" height=\"276\">b", '\x01'), "a[image]b");
}

TEST(HtmlToPlainText, TinyImageWithoutAltIsDropped) {
  EXPECT_EQ(htmlToPlainText("a<img src=\"i.png\" width=\"16\" height=\"16\">b", '\x01'), "ab");
}

TEST(HtmlToPlainText, ImageAltTextIsShown) {
  EXPECT_EQ(htmlToPlainText("<img alt=\"A &amp; B map\" src=\"m.png\">", '\x01'), "[image: A & B map]");
  EXPECT_EQ(htmlToPlainText("<img data-alt=\"no\" src=\"m.png\">", '\x01'), "[image]");
}

TEST(HtmlToPlainText, SymbolImageIsInsertedAsText) {
  EXPECT_EQ(htmlToPlainText("x <img alt=\"\xE2\x98\xBE\" src=\"s.png\" width=\"24\"> y", '\x01'), "x \xE2\x98\xBE y");
}

TEST(HtmlToPlainText, MathImageShowsItsFormula) {
  EXPECT_EQ(
      htmlToPlainText("<img src=\"m.svg\" aria-hidden=\"true\" alt=\"{\\displaystyle e}\" loading=\"lazy\">", '\x01'),
      "[formula: e]");
  EXPECT_EQ(htmlToPlainText("<img alt=\"{\\displaystyle \\sin ^{2}(e/2)}\" src=\"m.svg\">", '\x01'),
            "[formula: \\sin ^{2}(e/2)]");
}

TEST(HtmlToPlainText, BareImageTagNeverOverrunsTheBuffer) {
  // "[image]" is longer than "<img>", so it must be skipped rather than overwrite unread text.
  EXPECT_EQ(htmlToPlainText("<img>after", '\x01'), "after");
  EXPECT_EQ(htmlToPlainText("<img/>after", '\x01'), "after");
}

struct Collected : LinkSink {
  struct Link {
    size_t start;
    size_t end;
    std::string href;
  };
  void onLink(size_t start, size_t end, std::string_view href) override {
    links.push_back({start, end, std::string(href)});
  }
  std::vector<Link> links;
};

std::string convert(std::string html, Collected& sink) {
  html.resize(htmlToPlainTextInPlace(html.data(), html.size(), '\x01', &sink));
  return html;
}

TEST(HtmlToPlainText, ReportsLinkRangesInTheOutput) {
  Collected sink;
  const std::string text = convert(
      "<p>See <a href=\"Ruby_(programming_language)\" title=\"x\">Ruby</a> and <a href='PyPy'>PyPy</a>.</p>", sink);
  EXPECT_EQ(text, "See Ruby and PyPy.");
  ASSERT_EQ(sink.links.size(), 2u);
  EXPECT_EQ(text.substr(sink.links[0].start, sink.links[0].end - sink.links[0].start), "Ruby");
  EXPECT_EQ(sink.links[0].href, "Ruby_(programming_language)");
  EXPECT_EQ(text.substr(sink.links[1].start, sink.links[1].end - sink.links[1].start), "PyPy");
  EXPECT_EQ(sink.links[1].href, "PyPy");
}

TEST(HtmlToPlainText, LinkOffsetsSurviveEarlierCompaction) {
  // Tags and entities before the link shrink the text, so offsets must follow the output.
  Collected sink;
  const std::string text = convert("<div><b>a</b> &amp; <i>b</i> <a class=\"x\" href=\"Target\">go</a></div>", sink);
  EXPECT_EQ(text, "a & b go");
  ASSERT_EQ(sink.links.size(), 1u);
  EXPECT_EQ(text.substr(sink.links[0].start, sink.links[0].end - sink.links[0].start), "go");
}

TEST(HtmlToPlainText, AnchorsWithoutHrefAreNotLinks) {
  Collected sink;
  EXPECT_EQ(convert("<a id=\"top\">Top</a> <a href=\"\">e</a>", sink), "Top e");
  EXPECT_TRUE(sink.links.empty());
}

TEST(HtmlToPlainText, EmptySuperscriptGetsNoCaret) {
  EXPECT_EQ(htmlToPlainText("m/s<sup><a class=\"reference-link\"></a></sup> end", '\x01'), "m/s end");
  EXPECT_EQ(htmlToPlainText("m/s<sup>2</sup>", '\x01'), "m/s^2");
}

TEST(HtmlToPlainText, InfoboxRowsReadAsLabelAndValue) {
  EXPECT_EQ(htmlToPlainText("<table><tr><th>Perigee</th><td>362600 km</td></tr>"
                            "<tr><th>Eccentricity</th><td>0.0549</td></tr></table>",
                            '\x01'),
            "Perigee: 362600 km\nEccentricity: 0.0549");
}

TEST(HtmlToPlainText, DataTableCellsAreSetApart) {
  EXPECT_EQ(htmlToPlainText("<table><tr><td>a</td><td>b</td><td>c</td></tr></table>", '\x01'), "a | b | c");
}

TEST(HtmlToPlainText, NavigationBoxesAndReferenceListsAreDropped) {
  EXPECT_EQ(
      htmlToPlainText("before<div class=\"navbox\"><div class=\"navbox-inner\">links</div>more</div>after", '\x01'),
      "beforeafter");
  EXPECT_EQ(htmlToPlainText("text<div class=\"reflist\"><ol><li>ref</li></ol></div>", '\x01'), "text");
  EXPECT_EQ(htmlToPlainText("<div class=\"hatnote\">For other uses</div>", '\x01'), "For other uses");
}

TEST(HtmlToPlainText, InlineFootnoteMarkersAreDropped) {
  EXPECT_EQ(htmlToPlainText("0.0549<span class=\"mw-ref reference\"><a href=\"#c\"><span class=\"b\">[</span>1"
                            "<span class=\"b\">]</span></a></span> km",
                            '\x01'),
            "0.0549 km");
}

TEST(HtmlToPlainText, LabelCellWithABlockStillJoinsItsValue) {
  EXPECT_EQ(htmlToPlainText("<tr><th><div>Mean radius</div></th><td>1737.4 km</td></tr>", '\x01'),
            "Mean radius: 1737.4 km");
}

TEST(HtmlToPlainText, PageChromeIsDroppedOnRequest) {
  const std::string html =
      "<nav><a href=\"/\">Home</a></nav><p>Body text</p><aside>ads</aside><form><input></form><footer>(c)</footer>";
  std::string withChrome = html;
  withChrome.resize(htmlToPlainTextInPlace(withChrome.data(), withChrome.size(), '\x01', nullptr, false));
  EXPECT_NE(withChrome.find("Home"), std::string::npos);  // the default keeps it
  std::string without = html;
  without.resize(htmlToPlainTextInPlace(without.data(), without.size(), '\x01', nullptr, true));
  EXPECT_EQ(without, "Body text");
}

}  // namespace

namespace {
std::string webText(std::string html) {
  html.push_back('\0');
  html.pop_back();
  const size_t n = htmlToPlainTextInPlace(html.data(), html.size(), '#', nullptr, true);
  return html.substr(0, n);
}
}  // namespace

TEST(HtmlToPlainText, WebPagesCollapseWhitespaceLikeHtml) {
  EXPECT_EQ(webText("<div>\n      <p>\n   one\n   two  three</p>\n\n   <p>  four </p></div>"), "one two three\n\nfour");
  EXPECT_EQ(webText("<h2>\n  Title</h2><p>x</p>"), "#Title\n\nx");  // no space after the heading marker
}

TEST(HtmlToPlainText, WebPagesKeepPreformattedText) {
  EXPECT_EQ(webText("<p>run:</p><pre>a\n  b\n</pre><p>done</p>"), "run:\n\na\n  b\n\ndone");
}
