#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "activities/zim/ZimArticle.h"

class GfxRenderer;
class MappedInputManager;
class Activity;

namespace feeds {

// Loads a saved article into the PSRAM article structure the viewer uses and marks it read.
// False when the URL was never saved or PSRAM is short.
bool loadViewerArticle(std::string_view url, ZimArticle& out, bool markAsRead = true);

// A viewer for a saved article. Links to other saved articles open on top of it; links to anything
// else report "not found" (the page was not part of a sync).
std::unique_ptr<Activity> makeArticleViewer(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                            const std::string& url, int startPage = 0);

}  // namespace feeds
