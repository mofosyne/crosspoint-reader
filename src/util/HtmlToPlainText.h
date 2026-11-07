#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Convert an HTML fragment to plain text for the dictionary's plain-text
// fallback. Styling is ignored: block elements become line breaks and HTML
// entities are decoded. A heading becomes a label line of its own: the
// headingMarker byte is written at the start of that line, and the definition
// view draws a line starting with it in bold.
// Reuses the input buffer; move an owned string to avoid copying it.
std::string htmlToPlainText(std::string html, char headingMarker);

// Receives each <a href> link as a byte range of the converted text.
class LinkSink {
 public:
  virtual ~LinkSink() = default;
  // `start`..`end` span the link text in the output; empty ranges are not reported.
  virtual void onLink(size_t start, size_t end, std::string_view href) = 0;
};

// Same conversion on a caller-owned buffer, in place. Returns the text length (the text starts at
// `buffer`). The caller may pass a PSRAM buffer so a large article never needs internal RAM.
// When `links` is set, every <a href> is reported with offsets into the returned text.
size_t htmlToPlainTextInPlace(char* buffer, size_t size, char headingMarker, LinkSink* links = nullptr);
