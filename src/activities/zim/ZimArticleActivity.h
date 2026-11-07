#pragma once

#include <HalMemory.h>
#include <I18n.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ZimArticle.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Paged plain-text viewer for one ZIM article, with a list of the links on the current page.
// The text, the line table and the links all live in PSRAM (see ZimArticle). Following a link
// opens the target in a new viewer on top of this one, so Back returns to the same page.
class ZimArticleActivity final : public Activity {
 public:
  // Loads the article behind an archive path. False when it is missing or does not fit in memory.
  using Opener = std::function<bool(std::string_view path, ZimArticle& out)>;
  static constexpr int MAX_DEPTH = 8;

  ZimArticleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ZimArticle article, Opener opener,
                     int depth = 0);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
#ifdef CROSSPOINT_SERIAL_INPUT
  std::string debugState() override;
  bool debugCommand(const std::string& command) override;
#endif

 private:
  // One wrapped display line: a byte span of the article text.
  struct Line {
    uint32_t start;
    uint16_t len;
    uint8_t bold;
    uint8_t reserved;
  };
  struct BodyArea {
    int x;
    int y;
    int width;
    int height;
  };

  BodyArea bodyArea() const;
  bool layout();
  int measureSpan(int fontId, const char* text, size_t len, EpdFontFamily::Style style) const;
  const Line* lines() const { return reinterpret_cast<const Line*>(lines_.get()); }
  void drawBody(int fontId, int x, int startY, int page, bool underline) const;
  bool linkExtent(const Line& line, const ZimArticle::Link& link, int fontId, int originX, int& x0, int& x1) const;
  int linkAt(int tx, int ty) const;
  // Distinct links whose text is on the current page, in reading order (indexes into the article).
  std::vector<uint32_t> linksOnPage() const;
  void showLinks();
  void showContents();
  void jumpToPercent(int percent);
  void followPendingLink();
  void showMessage(StrId text);
  void turnPage(int delta);

  ZimArticle article_;
  Opener opener_;
  const int depth_;

  HalMemory::PsramBuffer lines_;
  size_t lineCount_ = 0;
  int linesPerPage_ = 1;
  int totalPages_ = 1;
  int currentPage_ = 0;
  bool layoutFailed_ = false;

  std::vector<uint32_t> pageLinks_;  // article link index for each row of the link list
  int chosenLink_ = -1;
  int pendingLink_ = -1;
  std::vector<uint32_t> contentLines_;  // first line of each heading shown in the contents list
  int chosenContents_ = -1;
  int pendingContents_ = -1;

  StrId message_ = StrId::STR_DICT_NOT_FOUND;
  unsigned long messageTime_ = 0;
  bool messageVisible_ = false;
  ButtonNavigator buttonNavigator;
};
