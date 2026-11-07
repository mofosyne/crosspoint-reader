#pragma once

#include <string_view>

namespace feed {

// The part of a web page that holds the article: the inner HTML of the largest <article>, else <main>,
// else an element with id "content"/"main", else <body>, else the whole input. The result points into
// `html`. Page furniture inside it (nav, footer, forms) is left for the text converter to drop.
std::string_view extractMainContent(std::string_view html);

}  // namespace feed
