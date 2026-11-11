#include "BigDigits.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace {

// Segment bits: a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle.
constexpr unsigned A = 1, B = 2, C = 4, D = 8, E = 16, F = 32, G = 64;
constexpr unsigned DIGITS[10] = {A | B | C | D | E | F, B | C,         A | B | D | E | G, A | B | C | D | G, B | C | F | G,
                                 A | C | D | F | G,     A | C | D | E | F | G, A | B | C, A | B | C | D | E | F | G,
                                 A | B | C | D | F | G};

struct Metrics {
  int w, gap, t;
};
Metrics metricsFor(int height) {
  const int t = std::max(3, height / 9);
  return {height * 55 / 100, std::max(4, height / 10), t};
}

void fillCircle(const GfxRenderer& r, int cx, int cy, int radius, bool black) {
  for (int dy = -radius; dy <= radius; ++dy) {
    const int hw = static_cast<int>(std::sqrt(static_cast<float>(radius * radius - dy * dy)));
    r.fillRect(cx - hw, cy + dy, 2 * hw + 1, 1, black);
  }
}

void drawDigit(const GfxRenderer& r, int digit, int x, int y, int h, const Metrics& m) {
  const unsigned seg = DIGITS[digit];
  const int uh = (h - 3 * m.t) / 2;  // the length of each vertical bar
  if (seg & A) r.fillRect(x + m.t, y, m.w - 2 * m.t, m.t, true);
  if (seg & D) r.fillRect(x + m.t, y + h - m.t, m.w - 2 * m.t, m.t, true);
  if (seg & G) r.fillRect(x + m.t, y + (h - m.t) / 2, m.w - 2 * m.t, m.t, true);
  if (seg & F) r.fillRect(x, y + m.t, m.t, uh, true);
  if (seg & B) r.fillRect(x + m.w - m.t, y + m.t, m.t, uh, true);
  if (seg & E) r.fillRect(x, y + 2 * m.t + uh, m.t, uh, true);
  if (seg & C) r.fillRect(x + m.w - m.t, y + 2 * m.t + uh, m.t, uh, true);
}

int ringRadius(int height) { return std::max(5, height * 13 / 100); }

}  // namespace

int bigTemperatureWidth(int value, int height) {
  const Metrics m = metricsFor(height);
  const int digits = static_cast<int>(std::to_string(std::abs(value)).size());
  return (value < 0 ? m.w * 70 / 100 + m.gap : 0) + digits * m.w + digits * m.gap + 2 * ringRadius(height);
}

int drawBigTemperature(const GfxRenderer& r, int value, int x, int y, int height) {
  const Metrics m = metricsFor(height);
  int cx = x;
  if (value < 0) {  // a minus sign: the middle bar alone
    r.fillRect(cx, y + (height - m.t) / 2, m.w * 70 / 100, m.t, true);
    cx += m.w * 70 / 100 + m.gap;
  }
  for (const char c : std::to_string(std::abs(value))) {
    drawDigit(r, c - '0', cx, y, height, m);
    cx += m.w + m.gap;
  }
  const int radius = ringRadius(height);
  fillCircle(r, cx + radius, y + radius, radius, true);
  fillCircle(r, cx + radius, y + radius, std::max(1, radius - std::max(2, m.t * 2 / 3)), false);
  return cx + 2 * radius - x;
}
