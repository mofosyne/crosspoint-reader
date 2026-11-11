#pragma once

#include <GfxRenderer.h>

#include <string>
#include <vector>

// Wraps text that may hold line breaks: each line of `text` is wrapped on its own, and at most `maxLines`
// lines come back in all. (GfxRenderer::wrappedText draws a '\n' as a missing-glyph box.)
inline std::vector<std::string> wrapLines(const GfxRenderer& renderer, int fontId, const std::string& text, int width,
                                          int maxLines, EpdFontFamily::Style style) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= text.size() && static_cast<int>(out.size()) < maxLines) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    const std::string part = text.substr(pos, nl - pos);
    pos = nl + 1;
    if (part.empty()) continue;
    const int room = maxLines - static_cast<int>(out.size());
    for (auto& line : renderer.wrappedText(fontId, part.c_str(), width, room, style)) out.push_back(std::move(line));
  }
  return out;
}
