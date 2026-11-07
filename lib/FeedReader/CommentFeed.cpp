#include "CommentFeed.h"

#include "CommentFormat.h"

namespace feed {

namespace {

std::string_view withoutFragment(std::string_view url) { return url.substr(0, url.find('#')); }

std::string_view withoutSlash(std::string_view url) {
  while (!url.empty() && url.back() == '/') url.remove_suffix(1);
  return url;
}

}  // namespace

bool commentFeedUrlFor(std::string_view itemUrl, std::string_view commentsUrl, std::string& feedUrl) {
  const std::string_view page = withoutSlash(withoutFragment(itemUrl));
  if (page.empty() || withoutSlash(withoutFragment(commentsUrl)) != page) return false;
  if (page.find('?') != std::string_view::npos) return false;  // "?p=12" style links have no /feed/ form
  feedUrl.assign(page);
  feedUrl += "/feed/";
  return true;
}

bool CommentFeedRenderer::add(const FeedItem& comment) {
  if (count_ >= maxComments_ || used_ + 1 >= cap_) return false;
  HtmlOut w{out_, cap_, used_};
  // WordPress titles read "By: Name".
  std::string_view author = comment.title;
  if (author.substr(0, 3) == "By:") author.remove_prefix(3);
  while (!author.empty() && author.front() == ' ') author.remove_prefix(1);
  while (!author.empty() && author.back() == ' ') author.remove_suffix(1);
  char age[16];
  formatAge(now_, comment.published, age, sizeof(age));
  w.put("<p><b>&gt; ");
  w.put(author.empty() ? "anonymous" : author.data(), author.empty() ? 9 : author.size());
  w.put("</b>");
  w.put(age);
  w.put("</p>");
  w.put(comment.summary.data(), comment.summary.size());
  used_ = w.used;
  ++count_;
  return true;
}

}  // namespace feed
