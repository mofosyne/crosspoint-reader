#include "FlashcardReviewActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "WrapText.h"
#include "activities/reader/DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

using flashcards::Grade;

namespace {
constexpr int BUTTON_HEIGHT = 74;
constexpr int SAVE_EVERY = 8;
const char* const GRADE_NAMES[4] = {"Again", "Hard", "Good", "Easy"};
}  // namespace

FlashcardReviewActivity::FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 flashcards::DeckRef deck)
    : Activity("FlashcardReview", renderer, mappedInput), ref_(std::move(deck)) {}

void FlashcardReviewActivity::onEnter() {
  Activity::onEnter();
  loaded_ = deck_.open(ref_);
  if (loaded_) startSession();
  requestUpdate();
}

void FlashcardReviewActivity::onExit() {
  Activity::onExit();
  deck_.save();
}

void FlashcardReviewActivity::startSession() {
  deck_.startVisit();
  today_ = flashcards::today(deck_.visits());
  queue_ = flashcards::studyQueue(deck_.stateArray(), deck_.size(), today_, NEW_PER_SESSION);
  queuePos_ = 0;
  answered_ = 0;
  flipped_ = false;
  finished_ = queue_.empty();
  if (!finished_) deck_.card(queue_[0], card_);
}

void FlashcardReviewActivity::showNext() {
  notice_.clear();
  ++queuePos_;
  flipped_ = false;
  if (queuePos_ >= queue_.size()) {
    finished_ = true;
    deck_.save();
    return;
  }
  deck_.card(queue_[queuePos_], card_);
}

// Opens the full dictionary entry for the word on screen, in the dictionary chosen in Settings.
void FlashcardReviewActivity::lookupFront() {
  notice_.clear();
  if (SETTINGS.dictionaryName[0] == '\0') {
    notice_ = "No dictionary is selected in Settings.";
  } else {
    if (!dict_) {
      dict_ = makeUniqueNoThrow<Dictionary>();
      if (dict_ && !dict_->open(SETTINGS.dictionaryName)) dict_.reset();
    }
    std::string definition, headword;
    Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
    if (!dict_ || (dict_->needsIndex() && !dict_->buildIndex([](void*) { vTaskDelay(1); }, nullptr))) {
      notice_ = "The dictionary could not be opened.";
    } else if (!dict_->lookup(card_.front.c_str(), definition, headword, &result)) {
      notice_ = result == Dictionary::LookupResult::NotFound ? "Not in the dictionary." : "The lookup failed.";
    } else if (auto entry = makeUniqueNoThrow<DictionaryDefinitionActivity>(
                   renderer, mappedInput, std::move(headword), std::move(definition), dict_->definitionsAreHtml());
               entry && entry->enableLookup(*dict_, std::string(card_.front))) {
      startActivityForResult(std::move(entry), [this](const ActivityResult&) { requestUpdate(); });
      return;
    } else {
      notice_ = "Not enough memory to show the entry.";
    }
  }
  requestUpdate();
}

void FlashcardReviewActivity::flip() {
  RenderLock lock(*this);
  flipped_ = true;
}

void FlashcardReviewActivity::answer(const Grade grade) {
  {
    RenderLock lock(*this);
    const size_t index = queue_[queuePos_];
    deck_.state(index) = flashcards::applyGrade(deck_.state(index), grade, today_);
    deck_.markChanged();
    ++answered_;
    if (grade == Grade::Again) queue_.push_back(index);  // once more before the session ends
    showNext();
  }
  if (++sinceSave_ >= SAVE_EVERY) {
    sinceSave_ = 0;
    deck_.save();
  }
  requestUpdate();
}

int FlashcardReviewActivity::buttonAt(const int x, const int y) const {
  const int top =
      renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight - BUTTON_HEIGHT - 8;
  if (y < top || y > top + BUTTON_HEIGHT) return -1;
  const int cell = renderer.getScreenWidth() / 4;
  return std::clamp(x / cell, 0, 3);
}

void FlashcardReviewActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!loaded_ || finished_) return;
  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    haptic_feedback::touchAction();
    if (!flipped_) {
      flip();
    } else if (const int b = buttonAt(tx, ty); b >= 0) {
      answer(static_cast<Grade>(b));
    } else {
      lookupFront();  // a tap on the answer opens the dictionary entry
      return;
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!flipped_) {
      flip();
      requestUpdate();
    } else {
      answer(Grade::Good);
    }
    return;
  }
  if (flipped_) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up))
      lookupFront();
    else if (mappedInput.wasReleased(MappedInputManager::Button::Left))
      answer(Grade::Again);
    else if (mappedInput.wasReleased(MappedInputManager::Button::Down))
      answer(Grade::Hard);
    else if (mappedInput.wasReleased(MappedInputManager::Button::Right))
      answer(Grade::Easy);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
             mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    flip();
    requestUpdate();
  }
}

std::string FlashcardReviewActivity::intervalText(const int days) {
  if (days <= 0) return "<1d";
  if (days < 30) return std::to_string(days) + "d";
  if (days < 365) return std::to_string((days + 15) / 30) + "mo";
  return std::to_string((days + 182) / 365) + "y";
}

void FlashcardReviewActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int x = metrics.contentSidePadding;
  const int textWidth = width - 2 * x;
  const auto progress = [&]() {
    if (!loaded_ || finished_) return std::string();
    return std::to_string(queuePos_ + 1) + " / " + std::to_string(queue_.size());
  }();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, ref_.name.c_str(),
                 progress.empty() ? nullptr : progress.c_str());

  const int fontId = SETTINGS.getReaderFontId();  // the reader's font, so CJK text uses the user's font
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;
  const int lineHeight = renderer.getLineHeight(fontId);
  const auto centeredLines = [&](const std::string& text, EpdFontFamily::Style style, int maxLines) {
    if (text.empty()) return;
    if (renderer.isSdCardFont(fontId)) renderer.ensureSdCardFontReady(fontId, text.c_str(), 0x03);
    for (const auto& line : wrapLines(renderer, fontId, text, textWidth, maxLines, style)) {
      const int w = renderer.getTextWidth(fontId, line.c_str(), style);
      renderer.drawText(fontId, (width - w) / 2, y, line.c_str(), true, style);
      y += lineHeight + 2;
    }
  };

  if (!loaded_) {
    centeredLines("This deck could not be opened.", EpdFontFamily::REGULAR, 3);
  } else if (finished_) {
    centeredLines(answered_ == 0 ? "Nothing to study right now." : "All done.", EpdFontFamily::REGULAR, 2);
    y += lineHeight;
    centeredLines(answered_ == 0 ? "Come back when cards are due, or add some words."
                                 : std::to_string(answered_) + " cards answered.",
                  EpdFontFamily::REGULAR, 3);
  } else {
    if (!card_.context.empty() && !flipped_) {  // the sentence the word comes from, above it
      renderer.drawText(UI_10_FONT_ID, x, y,
                        renderer.truncatedText(UI_10_FONT_ID, card_.context.c_str(), textWidth).c_str(), true);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 12;
    }
    y += lineHeight;
    centeredLines(card_.front, EpdFontFamily::REGULAR, 3);  // not bold: a CJK font has no bold face to load
    if (!card_.reading.empty()) {
      y += 4;
      centeredLines(card_.reading, EpdFontFamily::REGULAR, 2);
    }
    if (flipped_) {
      y += lineHeight / 2;
      renderer.drawLine(x, y, width - x, y);
      y += lineHeight / 2;
      // leave room for the context line, the hint and the buttons below
      const int room =
          height - metrics.buttonHintsHeight - BUTTON_HEIGHT - 8 - renderer.getLineHeight(UI_10_FONT_ID) * 3 - 24;
      centeredLines(card_.back, EpdFontFamily::REGULAR, std::max(1, (room - y) / (lineHeight + 2)));
      if (!card_.context.empty()) {
        y += lineHeight / 2;
        renderer.drawText(UI_10_FONT_ID, x, y,
                          renderer.truncatedText(UI_10_FONT_ID, card_.context.c_str(), textWidth).c_str(), true);
      }
    }
  }

  // ---- the bottom: a hint, or the four grade buttons with the wait each would give
  const int bottom = height - metrics.buttonHintsHeight;
  if (loaded_ && !finished_ && flipped_) {
    const int top = bottom - BUTTON_HEIGHT - 8;
    const std::string hint = notice_.empty() ? "Tap the card for the dictionary" : notice_;
    const int hw = renderer.getTextWidth(UI_10_FONT_ID, hint.c_str());
    renderer.drawText(UI_10_FONT_ID, std::max(x, (width - hw) / 2), top - renderer.getLineHeight(UI_10_FONT_ID) - 8,
                      hint.c_str(), true);
    const int cell = width / 4;
    const flashcards::CardState& state = deck_.state(queue_[queuePos_]);
    for (int i = 0; i < 4; ++i) {
      const int bx = i * cell + 4;
      renderer.drawRoundedRect(bx, top, cell - 8, BUTTON_HEIGHT, 2, 8, true);
      const auto grade = static_cast<Grade>(i);
      const std::string wait = intervalText(flashcards::daysUntil(state, grade, today_));
      const int w1 = renderer.getTextWidth(UI_12_FONT_ID, GRADE_NAMES[i], EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, bx + (cell - 8 - w1) / 2, top + 12, GRADE_NAMES[i], true, EpdFontFamily::BOLD);
      const int w2 = renderer.getTextWidth(UI_10_FONT_ID, wait.c_str());
      renderer.drawText(UI_10_FONT_ID, bx + (cell - 8 - w2) / 2, top + 14 + renderer.getLineHeight(UI_12_FONT_ID),
                        wait.c_str(), true);
    }
  } else if (loaded_ && !finished_) {
    const char* hint = "Tap to show the answer";
    const int w = renderer.getTextWidth(UI_10_FONT_ID, hint);
    renderer.drawText(UI_10_FONT_ID, (width - w) / 2, bottom - renderer.getLineHeight(UI_10_FONT_ID) - 16, hint, true);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), flipped_ ? "Good" : "Show", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
