#include "WeatherIcons.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>

namespace {

void fillCircle(const GfxRenderer& r, int cx, int cy, int radius, bool black) {
  for (int dy = -radius; dy <= radius; ++dy) {
    const int w = static_cast<int>(std::sqrt(static_cast<float>(radius * radius - dy * dy)));
    r.fillRect(cx - w, cy + dy, 2 * w + 1, 1, black);
  }
}

// A cloud inside the unit square scaled to `s`; `grow` pixels wider all round (white, to cut a gap).
void cloud(const GfxRenderer& r, int x, int y, int s, int dy, int grow, bool black) {
  fillCircle(r, x + s * 30 / 100, y + s * 58 / 100 + dy, s * 20 / 100 + grow, black);
  fillCircle(r, x + s * 50 / 100, y + s * 44 / 100 + dy, s * 26 / 100 + grow, black);
  fillCircle(r, x + s * 74 / 100, y + s * 60 / 100 + dy, s * 18 / 100 + grow, black);
  r.fillRect(x + s * 30 / 100, y + s * 58 / 100 + dy - grow, s * 44 / 100, s * 22 / 100 + 2 * grow, black);
}

void sun(const GfxRenderer& r, int cx, int cy, int radius, int rays) {
  fillCircle(r, cx, cy, radius, true);
  for (int i = 0; i < rays; ++i) {
    const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(rays);
    const float c = std::cos(a), s = std::sin(a);
    r.drawLine(cx + static_cast<int>(c * radius * 1.45f), cy + static_cast<int>(s * radius * 1.45f),
               cx + static_cast<int>(c * radius * 2.0f), cy + static_cast<int>(s * radius * 2.0f), 2, true);
  }
}

// Points given in percent of the square.
void polygon(const GfxRenderer& r, int x, int y, int s, const int (*pts)[2], int n, bool black) {
  int px[16], py[16];
  for (int i = 0; i < n; ++i) {
    px[i] = x + s * pts[i][0] / 100;
    py[i] = y + s * pts[i][1] / 100;
  }
  r.fillPolygon(px, py, n, black);
}

}  // namespace

void drawWeatherIcon(const GfxRenderer& r, feed::WeatherIcon icon, int x, int y, int s) {
  using feed::WeatherIcon;
  const int line = s >= 40 ? 3 : 2;
  switch (icon) {
    case WeatherIcon::Sun:
      sun(r, x + s / 2, y + s / 2, s * 22 / 100, 8);
      break;
    case WeatherIcon::PartlyCloudy:
      sun(r, x + s * 62 / 100, y + s * 36 / 100, s * 17 / 100, 8);
      cloud(r, x, y, s, s * 6 / 100, std::max(2, s / 18), false);  // a white margin around the cloud
      cloud(r, x, y, s, s * 6 / 100, 0, true);
      break;
    case WeatherIcon::Cloud:
      cloud(r, x, y, s, 0, 0, true);
      break;
    case WeatherIcon::Fog:
      cloud(r, x, y, s, -s * 8 / 100, 0, true);
      r.drawLine(x + s * 18 / 100, y + s * 78 / 100, x + s * 80 / 100, y + s * 78 / 100, line, true);
      r.drawLine(x + s * 28 / 100, y + s * 90 / 100, x + s * 90 / 100, y + s * 90 / 100, line, true);
      break;
    case WeatherIcon::Rain:
      cloud(r, x, y, s, -s * 8 / 100, 0, true);
      for (int i = 0; i < 3; ++i) {
        const int dx = x + s * (32 + 18 * i) / 100;
        r.drawLine(dx + s * 5 / 100, y + s * 74 / 100, dx - s * 3 / 100, y + s * 94 / 100, line, true);
      }
      break;
    case WeatherIcon::Snow:
      cloud(r, x, y, s, -s * 8 / 100, 0, true);
      for (int i = 0; i < 3; ++i) {
        fillCircle(r, x + s * (34 + 17 * i) / 100, y + s * (82 + (i % 2) * 8) / 100, std::max(2, s / 18), true);
      }
      break;
    case WeatherIcon::Storm: {
      cloud(r, x, y, s, -s * 10 / 100, 0, true);
      static const int bolt[][2] = {{54, 60}, {38, 84}, {50, 84}, {44, 100}, {66, 74}, {54, 74}, {62, 60}};
      polygon(r, x, y, s, bolt, 7, true);
      break;
    }
  }
}

void drawOuterIcon(const GfxRenderer& r, feed::Outer outer, int x, int y, int s) {
  using feed::Outer;
  static const int tee[][2] = {{30, 10}, {42, 10}, {50, 20}, {58, 10}, {70, 10}, {95, 30},
                               {82, 48}, {72, 40}, {72, 92}, {28, 92}, {28, 40}, {18, 48}, {5, 30}};
  static const int longSleeve[][2] = {{30, 8},  {42, 8},  {50, 18}, {58, 8},  {70, 8},  {90, 32},
                                      {95, 92}, {82, 92}, {74, 46}, {72, 94}, {28, 94}, {26, 46},
                                      {18, 92}, {5, 92},  {10, 32}};
  static const int coat[][2] = {{30, 6},  {42, 6},  {50, 16}, {58, 6},  {70, 6},  {90, 30},
                                {95, 94}, {82, 94}, {74, 44}, {74, 100}, {26, 100}, {26, 44},
                                {18, 94}, {5, 94},  {10, 30}};
  if (outer == Outer::TShirt || outer == Outer::LightLayer) {
    polygon(r, x, y, s, outer == Outer::TShirt ? tee : longSleeve, outer == Outer::TShirt ? 13 : 15, true);
    if (outer == Outer::LightLayer) r.drawLine(x + s / 2, y + s * 22 / 100, x + s / 2, y + s * 90 / 100, 2, false);
    return;
  }
  polygon(r, x, y, s, outer == Outer::Jacket ? longSleeve : coat, 15, true);
  r.drawLine(x + s / 2, y + s * 20 / 100, x + s / 2, y + s * (outer == Outer::Jacket ? 90 : 96) / 100, 2, false);
  if (outer == Outer::WarmCoat) {  // buttons and a collar
    for (int i = 0; i < 3; ++i) fillCircle(r, x + s * 58 / 100, y + s * (40 + 17 * i) / 100, std::max(1, s / 22), false);
    r.drawLine(x + s * 30 / 100, y + s * 10 / 100, x + s / 2, y + s * 28 / 100, 2, false);
    r.drawLine(x + s * 70 / 100, y + s * 10 / 100, x + s / 2, y + s * 28 / 100, 2, false);
  }
}

void drawUmbrellaIcon(const GfxRenderer& r, int x, int y, int s) {
  const int cx = x + s / 2;
  const int radius = s * 44 / 100;
  const int top = y + s * 8 / 100 + radius;
  for (int dy = -radius; dy <= 0; ++dy) {  // the canopy: the top half of a disc
    const int w = static_cast<int>(std::sqrt(static_cast<float>(radius * radius - dy * dy)));
    r.fillRect(cx - w, top + dy, 2 * w + 1, 1, true);
  }
  const int line = s >= 40 ? 3 : 2;
  r.drawLine(cx, top, cx, y + s * 88 / 100, line, true);                                     // the shaft
  r.drawLine(cx, y + s * 88 / 100, cx - s * 10 / 100, y + s * 88 / 100, line, true);          // the hook
  r.drawLine(cx - s * 10 / 100, y + s * 88 / 100, cx - s * 10 / 100, y + s * 80 / 100, line, true);
}

void drawSunscreenIcon(const GfxRenderer& r, int x, int y, int s) { sun(r, x + s / 2, y + s / 2, s * 20 / 100, 12); }

void drawLayersIcon(const GfxRenderer& r, int x, int y, int s) {
  const int line = s >= 40 ? 3 : 2;
  for (int i = 0; i < 3; ++i) {  // three stacked chevrons
    const int top = y + s * (22 + 22 * i) / 100;
    r.drawLine(x + s * 10 / 100, top + s * 12 / 100, x + s / 2, top - s * 6 / 100, line, true);
    r.drawLine(x + s / 2, top - s * 6 / 100, x + s * 90 / 100, top + s * 12 / 100, line, true);
  }
}
