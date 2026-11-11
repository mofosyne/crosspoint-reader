#include "FlashcardAddActivity.h"

#include <DeckFile.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "flashcards/DeckStore.h"
#include "fontIds.h"
#include "util/Dictionary.h"

namespace {

void indexYield(void*) { vTaskDelay(1); }

constexpr size_t MAX_MEANING_BYTES = 480;

// The plain text of a dictionary definition for the back of a card: markup and the headword line removed,
// cut at a line or word end. Plain-text definitions mark the headword line with a control character.
std::string meaningOf(const std::string& headword, const std::string& definition, bool html) {
  std::string text = html ? flashcards::plainField(definition) : definition;
  std::string cleaned;
  for (const char c : text) {
    if (c != DICTIONARY_PLAIN_HEADWORD_MARKER && c != '\r') cleaned += c;
  }
  if (cleaned.compare(0, headword.size(), headword) == 0) {  // the first line repeats the word
    const size_t nl = cleaned.find('\n');
    cleaned = nl == std::string::npos ? std::string() : cleaned.substr(nl + 1);
  }
  while (!cleaned.empty() && (cleaned.front() == '\n' || cleaned.front() == ' ')) cleaned.erase(0, 1);
  if (cleaned.size() > MAX_MEANING_BYTES) {
    size_t cut = MAX_MEANING_BYTES;
    while (cut > 0 && (static_cast<unsigned char>(cleaned[cut]) & 0xC0) == 0x80) --cut;  // not inside a character
    const size_t nl = cleaned.rfind('\n', cut);
    if (nl != std::string::npos && nl > MAX_MEANING_BYTES / 2) cut = nl;
    cleaned = cleaned.substr(0, cut) + " ...";
  }
  return cleaned;
}

}  // namespace

FlashcardAddActivity::FlashcardAddActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string word)
    : Activity("FlashcardAdd", renderer, mappedInput), word_(std::move(word)) {}

void FlashcardAddActivity::onEnter() {
  Activity::onEnter();
  requestUpdateAndWait();  // paint "Looking up" before the lookup blocks on the SD card
  lookup();
  requestUpdate();
}

void FlashcardAddActivity::lookup() {
  if (SETTINGS.dictionaryName[0] == '\0') {
    state_ = State::NoDictionary;
    return;
  }
  auto dict = makeUniqueNoThrow<Dictionary>();
  if (!dict || !dict->open(SETTINGS.dictionaryName)) {
    state_ = State::NoDictionary;
    return;
  }
  if (dict->needsIndex() && !dict->buildIndex(&indexYield, nullptr)) {
    state_ = State::Failed;
    return;
  }
  std::string definition;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  if (!dict->lookup(word_.c_str(), definition, headword_, &result)) {
    state_ = result == Dictionary::LookupResult::NotFound ? State::NotFound : State::Failed;
    return;
  }
  meaning_ = meaningOf(headword_, definition, dict->definitionsAreHtml());
  state_ = meaning_.empty() ? State::NotFound : State::Found;
}

void FlashcardAddActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  const bool confirm = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  int tx = 0, ty = 0;
  const bool tapped = mappedInput.wasScreenTapped(tx, ty);
  if (state_ == State::Found && (confirm || (tapped && ty > renderer.getScreenHeight() / 2))) {
    haptic_feedback::touchAction();
    flashcards::Card card;
    card.front = headword_;
    card.back = meaning_;
    {
      RenderLock lock(*this);
      state_ = flashcards::appendCard(flashcards::DEFAULT_DECK, card) ? State::Added : State::Failed;
    }
    requestUpdate();
    return;
  }
  if (state_ != State::Found && state_ != State::Searching && (confirm || tapped)) finish();
}

void FlashcardAddActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const int textWidth = width - 2 * x;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, "Add a word");
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const int fontId = SETTINGS.getReaderFontId();
  const int lineHeight = renderer.getLineHeight(fontId);
  const auto lines = [&](const std::string& text, EpdFontFamily::Style style, int maxLines) {
    if (renderer.isSdCardFont(fontId)) renderer.ensureSdCardFontReady(fontId, text.c_str(), 0x03);
    for (const auto& line : renderer.wrappedText(fontId, text.c_str(), textWidth, maxLines, style)) {
      renderer.drawText(fontId, x, y, line.c_str(), true, style);
      y += lineHeight + 2;
    }
  };
  switch (state_) {
    case State::Searching:
      lines("Looking up " + word_ + " ...", EpdFontFamily::REGULAR, 2);
      break;
    case State::Found:
      lines(headword_, EpdFontFamily::BOLD, 2);
      y += lineHeight / 2;
      lines(meaning_, EpdFontFamily::REGULAR, 14);
      break;
    case State::NotFound:
      lines("\"" + word_ + "\" is not in the dictionary.", EpdFontFamily::BOLD, 3);
      y += lineHeight / 2;
      lines("You can add any card by hand on the web page (File Transfer, then Flashcards).", EpdFontFamily::REGULAR, 6);
      break;
    case State::NoDictionary:
      lines("No dictionary is selected.", EpdFontFamily::BOLD, 2);
      y += lineHeight / 2;
      lines("Put a StarDict dictionary in /dictionaries and choose it in the reader's dictionary setting.",
            EpdFontFamily::REGULAR, 6);
      break;
    case State::Added:
      lines("Added to the " + std::string(flashcards::DEFAULT_DECK) + " deck.", EpdFontFamily::BOLD, 2);
      break;
    case State::Failed:
      lines("That did not work (the lookup or the SD card failed).", EpdFontFamily::BOLD, 3);
      break;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), state_ == State::Found ? "Add card" : "OK", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
