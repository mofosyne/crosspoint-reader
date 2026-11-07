#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace feed {

// Bounded writer for building a comments section in a caller-owned buffer: stops silently when full.
struct HtmlOut {
  char* buf;
  size_t cap;
  size_t used;
  void put(const char* s, size_t n) {
    if (used + n > cap) n = cap > used ? cap - used : 0;
    memcpy(buf + used, s, n);
    used += n;
  }
  void put(const char* s) { put(s, strlen(s)); }
  bool full() const { return used + 1 >= cap; }
};

// " 5m", " 3h", " 2d" or " 1y" for how long ago `created` was; empty when either time is unknown.
inline void formatAge(int64_t now, int64_t created, char* out, size_t cap) {
  out[0] = 0;
  if (now <= 0 || created <= 0 || now < created) return;
  const int64_t s = now - created;
  if (s < 3600) {
    snprintf(out, cap, " %dm", static_cast<int>(s / 60));
  } else if (s < 2 * 86400) {
    snprintf(out, cap, " %dh", static_cast<int>(s / 3600));
  } else if (s < 60 * 86400) {
    snprintf(out, cap, " %dd", static_cast<int>(s / 86400));
  } else {
    snprintf(out, cap, " %dy", static_cast<int>(s / (365 * 86400)));
  }
}

}  // namespace feed
