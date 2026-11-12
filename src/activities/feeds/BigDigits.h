#pragma once

#include <string>

class GfxRenderer;

// Large seven-segment numbers for a glance across a room; the fonts are far too small for that, and these
// scale to any height. `glyphs` may hold digits, '-', '.', 'o' (a degree ring) and '%'. Both calls return the
// width in pixels (drawBigGlyphs also draws).
int bigGlyphsWidth(const std::string& glyphs, int height);
int drawBigGlyphs(const GfxRenderer& renderer, const std::string& glyphs, int x, int y, int height);

// A whole number of degrees, e.g. 14 -> "14o".
int bigTemperatureWidth(int value, int height);
int drawBigTemperature(const GfxRenderer& renderer, int value, int x, int y, int height);
