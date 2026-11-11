#pragma once

class GfxRenderer;

// A temperature in large seven-segment digits with a minus sign and a degree ring, `height` pixels tall.
// The fonts are too small for a glance across a room, and these scale to any size. Returns the width drawn.
int bigTemperatureWidth(int value, int height);
int drawBigTemperature(const GfxRenderer& renderer, int value, int x, int y, int height);
