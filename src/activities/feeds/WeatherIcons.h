#pragma once

#include <Weather.h>

class GfxRenderer;

// Symbols drawn from shapes (no bitmaps), `size` pixels square with the top-left corner at x, y.
void drawWeatherIcon(const GfxRenderer& renderer, feed::WeatherIcon icon, int x, int y, int size);
void drawOuterIcon(const GfxRenderer& renderer, feed::Outer outer, int x, int y, int size);
void drawUmbrellaIcon(const GfxRenderer& renderer, int x, int y, int size);
void drawSunscreenIcon(const GfxRenderer& renderer, int x, int y, int size);  // a sun: strong UV
void drawLayersIcon(const GfxRenderer& renderer, int x, int y, int size);
