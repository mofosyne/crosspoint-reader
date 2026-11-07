#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "FeedItem.h"

namespace feed {

// WordPress (and similar) blogs keep an RSS feed of a post's comments at <post URL>/feed/. When a
// feed's <comments> link points at the article itself (often with #comments or #respond), the
// comments are on that page, so this gives the feed URL to try instead. False when the comments
// link is a different page.
bool commentFeedUrlFor(std::string_view itemUrl, std::string_view commentsUrl, std::string& feedUrl);

// Turns the items of a comment feed into HTML: "> author 5h" lines each followed by the comment.
class CommentFeedRenderer {
 public:
  CommentFeedRenderer(char* out, size_t cap, int64_t now, size_t maxComments = 200)
      : out_(out), cap_(cap), now_(now), maxComments_(maxComments) {}

  // A FeedParser sink: false once the limit or the buffer is reached.
  bool add(const FeedItem& comment);

  size_t size() const { return used_; }
  size_t count() const { return count_; }

 private:
  char* out_;
  size_t cap_;
  int64_t now_;
  size_t maxComments_;
  size_t used_ = 0;
  size_t count_ = 0;
};

}  // namespace feed
