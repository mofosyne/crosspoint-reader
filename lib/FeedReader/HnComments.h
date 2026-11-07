#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace feed {

struct HnLimits {
  size_t maxComments = 400;       // comments kept; the rest are only counted
  size_t maxCommentBytes = 1500;  // text kept per comment
};

// Reads a Hacker News thread from the Algolia API (hn.algolia.com/api/v1/items/<id>) and writes the
// comments as HTML for htmlToPlainText. Algolia lists a comment's replies before its text, so the
// comments are collected first and written out in reading order by render().
class HnThread {
 public:
  // `pool` is caller-owned scratch that holds the comment texts while the JSON streams in.
  HnThread(char* pool, size_t poolCap, HnLimits limits = {});

  // Algolia URL for an HN item link such as https://news.ycombinator.com/item?id=123; false for other URLs.
  static bool apiUrlFor(std::string_view commentsUrl, std::string& apiUrl);

  void feed(const char* data, size_t len);

  bool failed() const { return parser_.hasError() && kept_ == 0; }
  size_t totalComments() const { return total_; }
  size_t keptComments() const { return kept_; }

  // Writes the kept comments, with replies marked by leading ">" characters. `now` (Unix seconds) turns
  // creation times into ages; 0 leaves them out. Returns the bytes written (never more than cap).
  size_t render(char* out, size_t cap, int64_t now) const;

 private:
  struct Node {
    uint32_t id = 0;
    uint32_t parent = 0;
    uint32_t created = 0;
    uint32_t textOff = 0;
    uint16_t textLen = 0;
    char author[16] = {};  // HN names are at most 15 characters
  };
  struct Frame {
    Node node;
    bool hasAuthor = false;
  };
  static constexpr size_t MAX_FRAMES = StreamingJsonParser::MAX_NESTING / 2 + 1;

  static void onKey(void* ctx, const char* key, size_t len);
  static void onString(void* ctx, const char* value, size_t len);
  static void onNumber(void* ctx, const char* value, size_t len);
  static void onObjectStart(void* ctx);
  static void onObjectEnd(void* ctx);
  static void ignoreBool(void*, bool) {}
  static void ignoreNull(void*) {}
  static void ignoreVoid(void*) {}

  void setText(const char* value, size_t len);
  size_t renderNode(const Node& n, int depth, char* out, size_t cap, size_t used, int64_t now) const;

  StreamingJsonParser parser_;
  char* pool_;
  size_t poolCap_;
  size_t poolUsed_ = 0;
  HnLimits limits_;
  Frame frames_[MAX_FRAMES];
  size_t depth_ = 0;
  enum class Key : uint8_t { None, Author, Text, Id, Parent, Created };
  Key key_ = Key::None;
  std::unique_ptr<Node[]> nodes_;
  size_t kept_ = 0;
  size_t total_ = 0;
  uint32_t rootId_ = 0;
};

}  // namespace feed
