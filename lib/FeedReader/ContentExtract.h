#pragma once

#include <string_view>

namespace feed {

// The part of a web page that holds the article: the inner HTML of the largest <article>, else <main>,
// else an element with id "content"/"main", else the container of <body> that holds most of the page's
// text outside links (so a site menu or sidebar falls away), else the whole input. The result points into
// `html`. Page furniture inside it (nav, footer, forms) is left for the text converter to drop.
std::string_view extractMainContent(std::string_view html);

// The page's own title heading repeats the feed item's title, which the saved article already shows.
// Returns `html` after that first <h1>/<h2> (within its first 2000 bytes) when its text matches
// `title`, else `html` unchanged. Matching ignores case, punctuation and entities.
std::string_view dropLeadingTitle(std::string_view html, std::string_view title);

// Length of `text` without the bare "[image]" lines (no alt text) that end it, such as the thumbnails of
// related posts after an article. A placeholder with alt text, "[image: ...]", is kept.
size_t trimTrailingImages(std::string_view text);

}  // namespace feed
