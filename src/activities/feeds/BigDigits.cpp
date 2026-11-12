#include "BigDigits.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// Segment bits: a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle.
constexpr unsigned A = 1, B = 2, C = 4, D = 8, E = 16, F = 32, G = 64;
constexpr unsigned DIGITS[10] = {
    A | B | C | D | E | F, B | C,     A | B | D | E | G,         A | B | C | D | G,    B | C | F | G, A | C | D | F | G,
    A | C | D | E | F | G, A | B | C, A | B | C | D | E | F | G, A | B | C | D | F | G};

struct Metrics {
  int w, gap, t;
};
Metrics metricsFor(int height) {
  const int t = std::max(3, height / 9);
  return {height * 55 / 100, std::max(4, height / 10), t};
}

int ringRadius(int height) { return std::max(5, height * 13 / 100); }

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

int glyphWidth(char c, int height, const Metrics& m) {
  switch (c) {
    case '-':
      return m.w * 70 / 100;
    case '.':
      return m.t * 2;
    case 'o':
      return 2 * ringRadius(height);
    case '%':
      return m.w;
    default:
      return m.w;
  }
}

}  // namespace

int bigGlyphsWidth(const std::string& glyphs, int height) {
  const Metrics m = metricsFor(height);
  int width = 0;
  for (const char c : glyphs) width += glyphWidth(c, height, m) + m.gap;
  return width > 0 ? width - m.gap : 0;
}

int drawBigGlyphs(const GfxRenderer& r, const std::string& glyphs, int x, int y, int height) {
  const Metrics m = metricsFor(height);
  int cx = x;
  for (const char c : glyphs) {
    const int w = glyphWidth(c, height, m);
    if (c >= '0' && c <= '9') {
      drawDigit(r, c - '0', cx, y, height, m);
    } else if (c == '-') {
      r.fillRect(cx, y + (height - m.t) / 2, w, m.t, true);
    } else if (c == '.') {
      r.fillRect(cx, y + height - m.t * 2, m.t * 2, m.t * 2, true);  // a square dot on the baseline
    } else if (c == 'o') {
      const int radius = ringRadius(height);
      fillCircle(r, cx + radius, y + radius, radius, true);
      fillCircle(r, cx + radius, y + radius, std::max(1, radius - std::max(2, m.t * 2 / 3)), false);
    } else if (c == '%') {
      const int radius = std::max(5, height * 12 / 100);
      const int ring = std::max(2, m.t * 2 / 3);
      for (const int corner : {0, 1}) {  // two rings joined by a slash
        const int cxr = corner == 0 ? cx + radius : cx + w - radius;
        const int cyr = corner == 0 ? y + radius : y + height - radius;
        fillCircle(r, cxr, cyr, radius, true);
        fillCircle(r, cxr, cyr, std::max(1, radius - ring), false);
      }
      r.drawLine(cx + w - radius, y + radius, cx + radius, y + height - radius, std::max(2, m.t / 2 + 1), true);
    }
    cx += w + m.gap;
  }
  return cx > x ? cx - m.gap - x : 0;
}

int bigTemperatureWidth(int value, int height) { return bigGlyphsWidth(std::to_string(value) + "o", height); }

int drawBigTemperature(const GfxRenderer& r, int value, int x, int y, int height) {
  return drawBigGlyphs(r, std::to_string(value) + "o", x, y, height);
}
