#include "DiscoverActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "WrapText.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int BUTTON_HEIGHT = 74;
const char* const NAMES[3] = {"Not now", "Know it", "Add"};
}  // namespace

DiscoverActivity::DiscoverActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string listName)
    : Activity("Discover", renderer, mappedInput), list_(std::move(listName)) {}

bool DiscoverActivity::owned(const uint32_t key) const { return std::binary_search(owned_.begin(), owned_.end(), key); }

void DiscoverActivity::onEnter() {
  Activity::onEnter();
  loaded_ = words_.openFile(std::string(flashcards::WORDLISTS_ROOT) + "/" + list_ + ".tsv");
  if (loaded_) {
    owned_ = flashcards::ownedWords(list_);
    for (size_t i = 0; i < words_.size(); ++i) left_ += !owned(words_.state(i).key);
    pos_ = 0;
    while (pos_ < words_.size() && owned(words_.state(pos_).key)) ++pos_;
    finished_ = pos_ >= words_.size();
    if (!finished_) words_.card(pos_, card_);
  }
  requestUpdate();
}

void DiscoverActivity::advance() {
  if (left_ > 0) --left_;
  ++pos_;
  while (pos_ < words_.size() && owned(words_.state(pos_).key)) ++pos_;
  finished_ = pos_ >= words_.size();
  if (!finished_) words_.card(pos_, card_);
}

void DiscoverActivity::choose(const Choice choice) {
  {
    RenderLock lock(*this);
    const uint32_t key = words_.state(pos_).key;
    if (choice == Choice::Add && flashcards::appendCard(list_, card_)) {
      owned_.insert(std::upper_bound(owned_.begin(), owned_.end(), key), key);
      ++added_;
    } else if (choice == Choice::Known && flashcards::addKnownWord(list_, key)) {
      owned_.insert(std::upper_bound(owned_.begin(), owned_.end(), key), key);
    }
    advance();
  }
  requestUpdate();
}

int DiscoverActivity::buttonAt(const int x, const int y) const {
  const int top = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight - BUTTON_HEIGHT - 8;
  if (y < top || y > top + BUTTON_HEIGHT) return -1;
  return std::clamp(x * 3 / renderer.getScreenWidth(), 0, 2);
}

void DiscoverActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!loaded_ || finished_) return;
  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (const int b = buttonAt(tx, ty); b >= 0) {
      haptic_feedback::touchAction();
      choose(b == 0 ? Choice::Skip : b == 1 ? Choice::Known : Choice::Add);
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) choose(Choice::Add);
  else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) choose(Choice::Known);
  else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) choose(Choice::Skip);
}

void DiscoverActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const int textWidth = width - 2 * x;
  const std::string left = loaded_ && !finished_ ? std::to_string(left_) + " new" : std::string();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, list_.c_str(),
                 left.empty() ? nullptr : left.c_str());
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId);
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 4;
  const auto centered = [&](const std::string& text, int maxLines) {
    if (text.empty()) return;
    if (renderer.isSdCardFont(fontId)) renderer.ensureSdCardFontReady(fontId, text.c_str(), 0x03);
    for (const auto& line : wrapLines(renderer, fontId, text, textWidth, maxLines, EpdFontFamily::REGULAR)) {
      const int w = renderer.getTextWidth(fontId, line.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(fontId, (width - w) / 2, y, line.c_str(), true, EpdFontFamily::REGULAR);
      y += lineHeight + 2;
    }
  };
  if (!loaded_) {
    centered("This word list could not be opened.", 3);
  } else if (finished_) {
    centered("No new words left in this list.", 2);
    y += lineHeight;
    centered(added_ > 0 ? std::to_string(added_) + " added." : "", 1);
  } else {
    centered(card_.front, 3);
    if (!card_.reading.empty()) {
      y += 6;
      centered(card_.reading, 2);
    }
    y += lineHeight / 2;
    renderer.drawLine(x, y, width - x, y);
    y += lineHeight / 2;
    centered(card_.back, 8);
    const int top = renderer.getScreenHeight() - metrics.buttonHintsHeight - BUTTON_HEIGHT - 8;
    const int cell = width / 3;
    for (int i = 0; i < 3; ++i) {
      const int bx = i * cell + 4;
      renderer.drawRoundedRect(bx, top, cell - 8, BUTTON_HEIGHT, 2, 8, true);
      const int w = renderer.getTextWidth(UI_12_FONT_ID, NAMES[i], EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, bx + (cell - 8 - w) / 2, top + (BUTTON_HEIGHT - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                        NAMES[i], true, EpdFontFamily::BOLD);
    }
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Add", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
