#include "ZimArticleActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "ZimLinkListActivity.h"
#include "activities/reader/EpubReaderPercentSelectionActivity.h"
#include "components/UITheme.h"
#include "util/Dictionary.h"

namespace {
constexpr size_t MAX_LINE_BYTES = 191;
constexpr unsigned long MESSAGE_DURATION_MS = 1500;
constexpr size_t MAX_PAGE_LINKS = 64;
constexpr size_t MAX_LABEL_BYTES = 80;
// The reader's own line spacing setting, so articles match the books.
float lineSpacing() { return SETTINGS.getReaderLineCompression(); }
}  // namespace

ZimArticleActivity::ZimArticleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ZimArticle article,
                                       Opener opener, const int depth)
    : Activity("ZimArticle", renderer, mappedInput),
      article_(std::move(article)),
      opener_(std::move(opener)),
      depth_(depth) {}

void ZimArticleActivity::onEnter() {
  Activity::onEnter();
  layoutFailed_ = !layout();
  if (!layoutFailed_ && startPage_ > 0) currentPage_ = std::clamp(startPage_, 0, totalPages_ - 1);
  if (layoutFailed_) showMessage(StrId::STR_DICT_LOW_MEMORY);
  requestUpdate();
}

bool ZimArticleActivity::sleepResume(SleepResume& out) const {
  if (resumeKind_ == 0 || article_.path.empty()) return false;
  out.kind = resumeKind_;
  out.container = resumeContainer_;
  out.target = article_.path;
  out.page = currentPage_;
  return true;
}

void ZimArticleActivity::onExit() {
  Activity::onExit();
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
}

ZimArticleActivity::BodyArea ZimArticleActivity::bodyArea() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto orientation = renderer.getOrientation();
  const bool isLandscape = orientation == GfxRenderer::Orientation::LandscapeClockwise ||
                           orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = isLandscape ? metrics.sideButtonHintsWidth : 0;
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int contentLeft =
      std::max(left, orientation == GfxRenderer::Orientation::LandscapeClockwise ? hintGutterWidth : 0);
  const int contentRight =
      std::max(right, orientation == GfxRenderer::Orientation::LandscapeCounterClockwise ? hintGutterWidth : 0);
  const int x = contentLeft + metrics.contentSidePadding;
  const int y = std::max(top, isInverted ? metrics.buttonHintsHeight : 0) + metrics.topPadding + metrics.headerHeight +
                metrics.verticalSpacing;
  const int bottomArea = std::max(bottom, metrics.buttonHintsHeight) + metrics.verticalSpacing;
  return {x, y, renderer.getScreenWidth() - contentLeft - contentRight - 2 * metrics.contentSidePadding,
          renderer.getScreenHeight() - y - bottomArea};
}

int ZimArticleActivity::measureSpan(const int fontId, const char* text, size_t len,
                                    const EpdFontFamily::Style style) const {
  char buf[MAX_LINE_BYTES + 1];
  len = std::min(len, MAX_LINE_BYTES);
  memcpy(buf, text, len);
  buf[len] = '\0';
  return renderer.getTextAdvanceX(fontId, buf, style);
}

// Word-wraps the text into byte spans. Lines are only offsets into the PSRAM text. A line that
// starts with DICTIONARY_PLAIN_HEADWORD_MARKER (a heading) is drawn bold.
bool ZimArticleActivity::layout() {
  const char* text = article_.chars();
  const uint32_t n = static_cast<uint32_t>(article_.textLen);
  const int fontId = SETTINGS.getReaderFontId();
  const BodyArea body = bodyArea();
  if (body.width <= 0 || body.height <= 0) return false;
  if (renderer.isSdCardFont(fontId)) renderer.ensureSdCardFontReady(fontId, text, 0x03 /* REGULAR + BOLD */);

  const size_t capacity = n / 12 + 64;
  lines_ = HalMemory::allocatePsram(capacity * sizeof(Line));
  if (!lines_) {
    LOG_ERR("ZIM", "PSRAM short for %u lines", static_cast<unsigned>(capacity));
    return false;
  }
  auto* out = reinterpret_cast<Line*>(lines_.get());
  lineCount_ = 0;

  const int maxWidth = body.width;
  const int spaceWidth = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  const int lineHeight = renderer.getLineHeight(fontId, lineSpacing());
  linesPerPage_ = std::max(1, body.height / lineHeight);

  uint32_t lineStart = 0;
  uint32_t lineEnd = 0;
  int lineWidth = 0;
  bool lineBold = false;
  bool full = false;
  const auto style = [&] { return lineBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR; };
  const auto flushLine = [&](uint32_t nextStart, const bool continuesBlock = false) {
    if (lineCount_ >= capacity) {
      full = true;  // pathological text; the rest of the article is cut
      return false;
    }
    out[lineCount_++] = {lineStart, static_cast<uint16_t>(lineEnd - lineStart), static_cast<uint8_t>(lineBold), 0};
    lineStart = nextStart;
    lineEnd = nextStart;
    lineWidth = 0;
    if (!continuesBlock) lineBold = false;  // a wrapped heading stays bold until its newline
    return true;
  };

  uint32_t i = 0;
  while (i < n && !full) {
    if (lineEnd == lineStart && i == lineStart && text[i] == DICTIONARY_PLAIN_HEADWORD_MARKER) {
      lineStart = ++i;
      lineEnd = lineStart;
      lineBold = true;
      continue;
    }
    const char c = text[i];
    if (c == '\n' || c == '\0') {
      if (!flushLine(i + 1)) break;
      i++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      i++;
      continue;
    }
    const uint32_t tokenStart = i;
    while (i < n && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' && text[i] != '\0' &&
           i - tokenStart < MAX_LINE_BYTES) {
      i++;
    }
    while (i - tokenStart > 1 && (text[i] & 0xC0) == 0x80) i--;  // never split a UTF-8 sequence
    const uint32_t tokenLen = i - tokenStart;
    const int tokenWidth = measureSpan(fontId, text + tokenStart, tokenLen, style());

    if (lineEnd == lineStart) {
      lineStart = tokenStart;
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    } else if (lineWidth + spaceWidth + tokenWidth <= maxWidth && tokenStart + tokenLen - lineStart <= UINT16_MAX) {
      lineEnd = tokenStart + tokenLen;
      lineWidth += spaceWidth + tokenWidth;
    } else {
      if (!flushLine(tokenStart, true)) break;
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    }

    // A token wider than the screen sits alone on its line: split it at the widest fitting codepoint.
    while (lineWidth > maxWidth && lineEnd - lineStart > 1) {
      const uint32_t len = lineEnd - lineStart;
      uint32_t lastFit = 0;
      for (uint32_t f = 1; f <= len; f++) {
        if (f == len || (text[lineStart + f] & 0xC0) != 0x80) {
          if (measureSpan(fontId, text + lineStart, f, style()) > maxWidth) break;
          lastFit = f;
        }
      }
      if (lastFit == 0) {
        lastFit = 1;
        while (lastFit < len && (text[lineStart + lastFit] & 0xC0) == 0x80) lastFit++;
      }
      const uint32_t rest = lineStart + lastFit;
      lineEnd = rest;
      if (!flushLine(rest, true)) break;
      lineEnd = rest + (len - lastFit);
      lineWidth = measureSpan(fontId, text + lineStart, lineEnd - lineStart, style());
    }
  }
  if (!full && lineEnd > lineStart) flushLine(n);
  while (lineCount_ > 0 && out[lineCount_ - 1].len == 0) lineCount_--;

  totalPages_ = std::max(1, (static_cast<int>(lineCount_) + linesPerPage_ - 1) / linesPerPage_);
  currentPage_ = 0;
  return true;
}

void ZimArticleActivity::showMessage(const StrId text) {
  message_ = text;
  messageTime_ = millis();
  messageVisible_ = true;
  requestUpdate();
}

void ZimArticleActivity::turnPage(const int delta) {
  const int next = currentPage_ + delta;
  if (next < 0 || next >= totalPages_) return;
  currentPage_ = next;
  requestUpdate();
}

std::vector<uint32_t> ZimArticleActivity::linksOnPage() const {
  std::vector<uint32_t> found;
  const Line* all = lines();
  const size_t first = static_cast<size_t>(currentPage_) * linesPerPage_;
  const size_t last = std::min(first + linesPerPage_, lineCount_);
  if (first >= last) return found;
  const uint32_t pageStart = all[first].start;
  const uint32_t pageEnd = all[last - 1].start + all[last - 1].len;
  const ZimArticle::Link* links = article_.links();
  for (size_t i = 0; i < article_.linkCount && found.size() < MAX_PAGE_LINKS; ++i) {
    const auto& link = links[i];
    if (link.end <= pageStart || link.start >= pageEnd) continue;
    const std::string_view path = article_.pathOf(link);
    bool duplicate = false;
    for (const uint32_t seen : found) duplicate |= article_.pathOf(links[seen]) == path;
    if (!duplicate) found.push_back(static_cast<uint32_t>(i));
  }
  return found;
}

// Collects the links whose text is on the current page (one row per distinct target) and lets the
// user pick one.
void ZimArticleActivity::showLinks() {
  pageLinks_ = linksOnPage();
  std::vector<std::string> labels;
  const ZimArticle::Link* links = article_.links();
  for (const uint32_t index : pageLinks_) {
    const auto& link = links[index];
    size_t len = std::min<size_t>(link.end - link.start, MAX_LABEL_BYTES);
    len = utf8SafeTruncateBuffer(article_.chars() + link.start, static_cast<int>(len));
    std::string label(article_.chars() + link.start, len);
    std::replace(label.begin(), label.end(), '\n', ' ');
    labels.push_back(std::move(label));
  }
  if (labels.empty()) {
    showMessage(StrId::STR_NO_ENTRIES);
    return;
  }
  chosenLink_ = -1;
  auto list =
      makeUniqueNoThrow<ZimLinkListActivity>(renderer, mappedInput, tr(STR_ZIM_LINKS), std::move(labels), &chosenLink_);
  if (!list) {
    LOG_ERR("ZIM", "OOM: link list");
    showMessage(StrId::STR_DICT_LOW_MEMORY);
    return;
  }
  startActivityForResult(std::move(list), [this](const ActivityResult&) {
    if (chosenLink_ >= 0 && static_cast<size_t>(chosenLink_) < pageLinks_.size()) {
      pendingLink_ = static_cast<int>(pageLinks_[chosenLink_]);
    }
  });
}

void ZimArticleActivity::jumpToPercent(const int percent) {
  const int page = std::clamp(percent, 0, 100) * totalPages_ / 100;
  currentPage_ = std::clamp(page, 0, totalPages_ - 1);
  requestUpdate();
}

// Contents: a "Go to %" row followed by the article's headings. Tapping the title bar opens it.
// A heading is a bold line; a heading that wraps over several lines is listed once.
void ZimArticleActivity::showContents() {
  constexpr size_t MAX_HEADINGS = 200;
  const Line* all = lines();
  contentLines_.clear();
  std::vector<std::string> labels;
  labels.emplace_back(tr(STR_GO_TO_PERCENT));
  bool inHeading = false;
  for (size_t i = 0; i < lineCount_ && contentLines_.size() < MAX_HEADINGS; ++i) {
    const bool bold = all[i].bold && all[i].len > 0;
    if (bold) {
      const size_t len =
          utf8SafeTruncateBuffer(article_.chars() + all[i].start, std::min<size_t>(all[i].len, MAX_LABEL_BYTES));
      if (!inHeading) {
        labels.emplace_back(article_.chars() + all[i].start, len);
        contentLines_.push_back(static_cast<uint32_t>(i));
      } else if (labels.back().size() < MAX_LABEL_BYTES) {  // a heading that wrapped: join its lines
        labels.back().append(" ").append(article_.chars() + all[i].start, len);
      }
    }
    inHeading = bold;
  }
  chosenContents_ = -1;
  auto list = makeUniqueNoThrow<ZimLinkListActivity>(renderer, mappedInput, tr(STR_ZIM_CONTENTS), std::move(labels),
                                                     &chosenContents_);
  if (!list) {
    showMessage(StrId::STR_DICT_LOW_MEMORY);
    return;
  }
  startActivityForResult(std::move(list), [this](const ActivityResult&) { pendingContents_ = chosenContents_; });
}

void ZimArticleActivity::followPendingLink() {
  const int index = pendingLink_;
  pendingLink_ = -1;
  if (index < 0 || static_cast<size_t>(index) >= article_.linkCount) return;
  if (depth_ + 1 >= MAX_DEPTH) {
    showMessage(StrId::STR_DICT_HISTORY_FULL);
    return;
  }
  const std::string path(article_.pathOf(article_.links()[index]));
  {
    RenderLock lock(*this);
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));  // the read and decode take a second or more
  }
  ZimArticle next;
  if (!opener_ || !opener_(path, next)) {
    showMessage(StrId::STR_DICT_NOT_FOUND);
    return;
  }
  auto viewer = makeUniqueNoThrow<ZimArticleActivity>(renderer, mappedInput, std::move(next), opener_, depth_ + 1);
  if (!viewer) {
    LOG_ERR("ZIM", "OOM: article viewer");
    showMessage(StrId::STR_DICT_LOW_MEMORY);
    return;
  }
  viewer->setResumeContext(resumeKind_, resumeContainer_);
  startActivityForResult(std::move(viewer), [](const ActivityResult&) {});
}

void ZimArticleActivity::loop() {
  if (layoutFailed_) {
    if (millis() - messageTime_ >= MESSAGE_DURATION_MS) finish();
    return;
  }
  if (pendingLink_ >= 0) {
    followPendingLink();
    return;
  }
  if (pendingContents_ >= 0) {
    const int row = pendingContents_;
    pendingContents_ = -1;
    if (row == 0) {
      startActivityForResult(makeUniqueNoThrow<EpubReaderPercentSelectionActivity>(
                                 renderer, mappedInput, currentPage_ * 100 / std::max(1, totalPages_ - 1)),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) jumpToPercent(std::get<PercentResult>(result.data).percent);
                             });
    } else if (static_cast<size_t>(row) <= contentLines_.size()) {
      currentPage_ = std::min<int>(contentLines_[row - 1] / linesPerPage_, totalPages_ - 1);
      requestUpdate();
    }
    return;
  }
  if (messageVisible_ && millis() - messageTime_ >= MESSAGE_DURATION_MS) {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock()) {
      messageVisible_ = false;
      requestUpdate();
    }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showLinks();
    return;
  }
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    haptic_feedback::touchAction();
    if (ty < bodyArea().y) {  // the title bar opens the contents
      showContents();
      return;
    }
    const int link = linkAt(tx, ty);  // a tap on a link follows it; elsewhere it turns the page
    if (link >= 0) {
      pendingLink_ = link;
    } else {
      turnPage(tx < renderer.getScreenWidth() / 3 ? -1 : 1);
    }
    return;
  }
  buttonNavigator.onNext([this] { turnPage(1); });
  buttonNavigator.onPrevious([this] { turnPage(-1); });
}

// Horizontal extent of `link` on `line`, relative to the text origin `originX`.
bool ZimArticleActivity::linkExtent(const Line& line, const ZimArticle::Link& link, const int fontId, const int originX,
                                    int& x0, int& x1) const {
  const size_t len = std::min<size_t>(line.len, MAX_LINE_BYTES);
  const uint32_t lineEnd = line.start + static_cast<uint32_t>(len);
  if (link.end <= line.start || link.start >= lineEnd) return false;
  const auto style = line.bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const uint32_t from = std::max(link.start, line.start);
  const uint32_t to = std::min(link.end, lineEnd);
  const char* text = article_.chars();
  x0 = originX + (from > line.start ? measureSpan(fontId, text + line.start, from - line.start, style) : 0);
  x1 = originX + measureSpan(fontId, text + line.start, to - line.start, style);
  return true;
}

// Index of the link under a touch, or -1. A few pixels of slack make small links easier to hit.
int ZimArticleActivity::linkAt(const int tx, const int ty) const {
  const int fontId = SETTINGS.getReaderFontId();
  const BodyArea body = bodyArea();
  const int lineHeight = renderer.getLineHeight(fontId, lineSpacing());
  if (ty < body.y || tx < body.x || lineHeight <= 0) return -1;
  const size_t row = static_cast<size_t>((ty - body.y) / lineHeight);
  const size_t index = static_cast<size_t>(currentPage_) * linesPerPage_ + row;
  if (row >= static_cast<size_t>(linesPerPage_) || index >= lineCount_) return -1;
  constexpr int SLACK = 8;
  const Line& line = lines()[index];
  const ZimArticle::Link* links = article_.links();
  for (size_t k = 0; k < article_.linkCount; ++k) {
    int x0 = 0;
    int x1 = 0;
    if (linkExtent(line, links[k], fontId, body.x, x0, x1) && tx >= x0 - SLACK && tx <= x1 + SLACK) {
      return static_cast<int>(k);
    }
  }
  return -1;
}

// Draws one page. The scan pass (underline = false) only records glyphs for the font cache.
void ZimArticleActivity::drawBody(const int fontId, const int x, const int startY, const int page,
                                  const bool underline) const {
  const Line* all = lines();
  const int lineHeight = renderer.getLineHeight(fontId, lineSpacing());
  const int glyphHeight = renderer.getLineHeight(fontId);
  const char* text = article_.chars();
  const ZimArticle::Link* links = article_.links();
  char buf[MAX_LINE_BYTES + 1];
  const size_t first = static_cast<size_t>(page) * linesPerPage_;
  const size_t last = std::min(first + linesPerPage_, lineCount_);
  for (size_t i = first; i < last; i++) {
    const Line& line = all[i];
    if (line.len == 0) continue;
    const int y = startY + static_cast<int>(i - first) * lineHeight;
    const size_t len = utf8SafeTruncateBuffer(text + line.start, std::min<size_t>(line.len, MAX_LINE_BYTES));
    memcpy(buf, text + line.start, len);
    buf[len] = '\0';
    const auto style = line.bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    renderer.drawText(fontId, x, y, buf, true, style);
    if (!underline) continue;
    // Underline each link that overlaps this line.
    for (size_t k = 0; k < article_.linkCount; ++k) {
      int x0 = 0;
      int x1 = 0;
      if (linkExtent(line, links[k], fontId, x, x0, x1))
        renderer.drawLine(x0, y + glyphHeight - 2, x1, y + glyphHeight - 2);
    }
  }
}

void ZimArticleActivity::render(RenderLock&&) {
  const int page = std::atomic_ref(currentPage_).load();
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.sideButtonHintsWidth : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = renderer.getScreenWidth() - hintGutterWidth;
  const int contentY = isInverted ? metrics.buttonHintsHeight : 0;

  char counter[16] = {};
  if (totalPages_ > 1) snprintf(counter, sizeof(counter), "%d/%d", page + 1, totalPages_);
  GUI.drawHeader(renderer, Rect{contentX, contentY + metrics.topPadding, contentWidth, metrics.headerHeight},
                 article_.headword.c_str(), counter[0] ? counter : nullptr);

  if (!layoutFailed_) {
    const int fontId = SETTINGS.getReaderFontId();
    const BodyArea body = bodyArea();
    auto* fcm = renderer.getFontCacheManager();
    auto scope = fcm->createPrewarmScope();
    drawBody(fontId, body.x, body.y, page, false);  // scan pass: records codepoints only
    scope.endScanAndPrewarm();
    drawBody(fontId, body.x, body.y, page, true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), article_.linkCount > 0 ? tr(STR_ZIM_LINKS) : "",
                                            (page > 0 ? "<" : ""), (page + 1 < totalPages_ ? ">" : ""));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (messageVisible_) {
    GUI.drawPopup(renderer, I18N.get(message_));
    return;
  }
  renderer.displayBuffer();
}

#ifdef CROSSPOINT_SERIAL_INPUT
std::string ZimArticleActivity::debugState() {
  std::string out = "ZimArticle '" + article_.headword + "' page " + std::to_string(currentPage_ + 1) + "/" +
                    std::to_string(totalPages_) + " depth " + std::to_string(depth_) + " links " +
                    std::to_string(article_.linkCount) + (messageVisible_ ? " [popup]" : "") + "\n--- page text ---\n";
  const Line* all = lines();
  const size_t first = static_cast<size_t>(currentPage_) * linesPerPage_;
  const size_t last = std::min(first + linesPerPage_, lineCount_);
  for (size_t i = first; i < last; ++i) {
    if (all[i].bold) out += "## ";
    out.append(article_.chars() + all[i].start, all[i].len);
    out += '\n';
  }
  out += "--- links on page ---\n";
  const auto onPage = linksOnPage();
  for (size_t i = 0; i < onPage.size(); ++i) {
    const auto& link = article_.links()[onPage[i]];
    out += std::to_string(i) + ": " + std::string(article_.chars() + link.start, link.end - link.start) + " -> " +
           std::string(article_.pathOf(link)) + "\n";
  }
  return out;
}

// Commands: PAGE:n (1-based), NEXT, PREV, PERCENT:p, LINK:i (index in the page's link list), HEADING:i,
// CONTENTS, BACK.
bool ZimArticleActivity::debugCommand(const std::string& command) {
  const auto number = [&](size_t prefix) { return atoi(command.c_str() + prefix); };
  if (command.rfind("PAGE:", 0) == 0) {
    currentPage_ = std::clamp(number(5) - 1, 0, totalPages_ - 1);
    requestUpdate();
  } else if (command == "NEXT") {
    turnPage(1);
  } else if (command == "PREV") {
    turnPage(-1);
  } else if (command.rfind("PERCENT:", 0) == 0) {
    jumpToPercent(number(8));
  } else if (command.rfind("LINK:", 0) == 0) {
    const auto onPage = linksOnPage();
    const int i = number(5);
    if (i < 0 || static_cast<size_t>(i) >= onPage.size()) return false;
    pendingLink_ = static_cast<int>(onPage[i]);
  } else if (command.rfind("HEADING:", 0) == 0) {
    // Jump to the nth heading (0-based) without opening the list.
    const Line* all = lines();
    int seen = -1;
    bool inHeading = false;
    for (size_t i = 0; i < lineCount_; ++i) {
      const bool bold = all[i].bold && all[i].len > 0;
      if (bold && !inHeading && ++seen == number(8)) {
        currentPage_ = std::min<int>(static_cast<int>(i) / linesPerPage_, totalPages_ - 1);
        requestUpdate();
        return true;
      }
      inHeading = bold;
    }
    return false;
  } else if (command == "CONTENTS") {
    showContents();
  } else if (command == "BACK") {
    finish();
  } else {
    return false;
  }
  return true;
}
#endif
