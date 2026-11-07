#include "HnComments.h"

#include "CommentFormat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace feed {

namespace {

constexpr std::string_view HN_HOST = "https://news.ycombinator.com/item?id=";
constexpr int MAX_MARKS = 5;  // ">" characters shown for deep replies

bool keyIs(const char* key, size_t len, const char* name) { return strlen(name) == len && memcmp(key, name, len) == 0; }

uint32_t parseUint(const char* s, size_t len) {
  uint64_t v = 0;
  for (size_t i = 0; i < len && s[i] >= '0' && s[i] <= '9'; ++i) {
    v = v * 10 + static_cast<uint64_t>(s[i] - '0');
    if (v > 0xFFFFFFFFu) return 0;
  }
  return static_cast<uint32_t>(v);
}

}  // namespace

HnThread::HnThread(char* pool, size_t poolCap, HnLimits limits)
    : parser_(JsonCallbacks{this, &HnThread::onKey, &HnThread::onString, &HnThread::onNumber, &HnThread::ignoreBool,
                            &HnThread::ignoreNull, &HnThread::onObjectStart, &HnThread::onObjectEnd,
                            &HnThread::ignoreVoid, &HnThread::ignoreVoid,
                            // long comments arrive as a prefix, which is all that is kept anyway
                            &HnThread::onString}),
      pool_(pool),
      poolCap_(poolCap),
      limits_(limits),
      nodes_(new (std::nothrow) Node[limits.maxComments]) {}

bool HnThread::apiUrlFor(std::string_view commentsUrl, std::string& apiUrl) {
  if (commentsUrl.substr(0, HN_HOST.size()) != HN_HOST) return false;
  const std::string_view rest = commentsUrl.substr(HN_HOST.size());
  size_t n = 0;
  while (n < rest.size() && rest[n] >= '0' && rest[n] <= '9') ++n;
  if (n == 0 || n > 10) return false;
  apiUrl = "https://hn.algolia.com/api/v1/items/";
  apiUrl.append(rest.substr(0, n));
  return true;
}

void HnThread::feed(const char* data, size_t len) {
  if (!nodes_) return;
  parser_.feed(data, len);
}

void HnThread::onKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<HnThread*>(ctx);
  if (keyIs(key, len, "author")) {
    self->key_ = Key::Author;
  } else if (keyIs(key, len, "text")) {
    self->key_ = Key::Text;
  } else if (keyIs(key, len, "id")) {
    self->key_ = Key::Id;
  } else if (keyIs(key, len, "parent_id")) {
    self->key_ = Key::Parent;
  } else if (keyIs(key, len, "created_at_i")) {
    self->key_ = Key::Created;
  } else {
    self->key_ = Key::None;
  }
}

void HnThread::setText(const char* value, size_t len) {
  Frame& f = frames_[depth_ - 1];
  if (kept_ >= limits_.maxComments) return;  // the comment will be dropped; keep the pool for the ones that count
  size_t n = len < limits_.maxCommentBytes ? len : limits_.maxCommentBytes;
  // Cut on a character boundary.
  while (n > 0 && n < len && (static_cast<unsigned char>(value[n]) & 0xC0) == 0x80) --n;
  if (poolUsed_ + n > poolCap_ || n > 0xFFFF) return;
  memcpy(pool_ + poolUsed_, value, n);
  f.node.textOff = static_cast<uint32_t>(poolUsed_);
  f.node.textLen = static_cast<uint16_t>(n);
  poolUsed_ += n;
}

void HnThread::onString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<HnThread*>(ctx);
  if (self->depth_ == 0) return;
  if (self->key_ == Key::Author) {
    Frame& f = self->frames_[self->depth_ - 1];
    const size_t n = len < sizeof(f.node.author) - 1 ? len : sizeof(f.node.author) - 1;
    memcpy(f.node.author, value, n);
    f.node.author[n] = 0;
    f.hasAuthor = true;
  } else if (self->key_ == Key::Text) {
    self->setText(value, len);
  }
  self->key_ = Key::None;
}

void HnThread::onNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<HnThread*>(ctx);
  if (self->depth_ == 0) return;
  Node& n = self->frames_[self->depth_ - 1].node;
  const uint32_t v = parseUint(value, len);
  if (self->key_ == Key::Id) {
    n.id = v;
  } else if (self->key_ == Key::Parent) {
    n.parent = v;
  } else if (self->key_ == Key::Created) {
    n.created = v;
  }
  self->key_ = Key::None;
}

void HnThread::onObjectStart(void* ctx) {
  auto* self = static_cast<HnThread*>(ctx);
  self->key_ = Key::None;
  if (self->depth_ >= MAX_FRAMES) return;
  self->frames_[self->depth_] = Frame{};
  ++self->depth_;
}

void HnThread::onObjectEnd(void* ctx) {
  auto* self = static_cast<HnThread*>(ctx);
  self->key_ = Key::None;
  if (self->depth_ == 0) return;
  Frame& f = self->frames_[--self->depth_];
  if (self->depth_ == 0) {  // the story itself
    self->rootId_ = f.node.id;
    return;
  }
  ++self->total_;
  if (self->kept_ >= self->limits_.maxComments || f.node.id == 0) return;
  if (!f.hasAuthor) memcpy(f.node.author, "[deleted]", 10);
  self->nodes_[self->kept_++] = f.node;
}

size_t HnThread::renderNode(const Node& n, int depth, char* out, size_t cap, size_t used, int64_t now) const {
  HtmlOut w{out, cap, used};
  char age[16];
  formatAge(now, n.created, age, sizeof(age));
  w.put("<p><b>");
  for (int m = 0; m < (depth < MAX_MARKS ? depth : MAX_MARKS) + 1; ++m) w.put("&gt;");
  w.put(" ");
  w.put(n.author);
  w.put("</b>");
  w.put(age);
  w.put("</p>");
  if (n.textLen > 0) {
    w.put(pool_ + n.textOff, n.textLen);
  } else {
    w.put("<p>[no text]</p>");
  }
  for (size_t i = 0; i < kept_ && !w.full(); ++i) {
    if (nodes_[i].parent == n.id) w.used = renderNode(nodes_[i], depth + 1, out, cap, w.used, now);
  }
  return w.used;
}

size_t HnThread::render(char* out, size_t cap, int64_t now) const {
  if (!nodes_ || cap == 0) return 0;
  size_t used = 0;
  // Top level: replies to the story, plus comments whose parent was cut by the limit.
  for (size_t i = 0; i < kept_ && used + 1 < cap; ++i) {
    const Node& n = nodes_[i];
    bool reachable = n.parent == rootId_;
    for (size_t j = 0; j < kept_ && !reachable; ++j) reachable = nodes_[j].id == n.parent;
    if (reachable && n.parent != rootId_) continue;  // rendered under its parent
    used = renderNode(n, 0, out, cap, used, now);
  }
  if (total_ > kept_ && used + 80 < cap) {
    char tail[80];
    const int n =
        snprintf(tail, sizeof(tail), "<p>[%u more comments not shown]</p>", static_cast<unsigned>(total_ - kept_));
    memcpy(out + used, tail, static_cast<size_t>(n));
    used += static_cast<size_t>(n);
  }
  return used;
}

}  // namespace feed
