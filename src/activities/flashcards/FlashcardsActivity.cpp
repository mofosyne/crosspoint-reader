#include "FlashcardsActivity.h"

#include <Memory.h>

#include "FlashcardAddActivity.h"
#include "FlashcardReviewActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

FlashcardsActivity::FlashcardsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Flashcards", renderer, mappedInput) {}

void FlashcardsActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void FlashcardsActivity::reload() {
  RenderLock lock(*this);
  decks_ = flashcards::listDecks();
  labels_.clear();
  values_.clear();
  rows_.clear();
  labels_.push_back("Add a word");
  values_.push_back("from the dictionary");
  for (const auto& ref : decks_) {
    flashcards::Deck deck;
    std::string value = "cannot open";
    if (deck.open(ref)) {
      const int32_t day = flashcards::today(deck.visits() + 1);
      size_t due = 0, fresh = 0;
      for (size_t i = 0; i < deck.size(); ++i) {
        due += flashcards::isDue(deck.state(i), day);
        fresh += !deck.state(i).seen;
      }
      value = std::to_string(due) + " due, " + std::to_string(fresh) + " new";
    }
    labels_.push_back(ref.name);
    values_.push_back(value);
  }
  if (decks_.empty()) {
    labels_.push_back("No decks yet");
    values_.push_back("add words, or upload on the web page");
  }
  for (size_t i = 0; i < labels_.size(); ++i) {
    fui::ListItem row;
    row.label = labels_[i].c_str();
    row.value = values_[i].c_str();
    row.actionValue = static_cast<int16_t>(i);
    rows_.push_back(row);
  }
}

void FlashcardsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows_.data();
  props.count = static_cast<uint16_t>(rows_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void FlashcardsActivity::startAddWord() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "Word to look up", "", 60, InputType::Text);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const std::string word = std::get<KeyboardResult>(result.data).text;
    if (word.empty()) return;
    if (auto add = makeUniqueNoThrow<FlashcardAddActivity>(renderer, mappedInput, word)) {
      startActivityForResult(std::move(add), [this](const ActivityResult&) {
        reload();
        requestUpdate();
      });
    }
  });
}

void FlashcardsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index == 0) {
    startAddWord();
    return;
  }
  const size_t deck = static_cast<size_t>(index) - 1;
  if (deck >= decks_.size()) return;
  if (auto review = makeUniqueNoThrow<FlashcardReviewActivity>(renderer, mappedInput, decks_[deck])) {
    startActivityForResult(std::move(review), [this](const ActivityResult&) {
      reload();
      requestUpdate();
    });
  }
}

#ifdef CROSSPOINT_SERIAL_INPUT
bool FlashcardsActivity::debugCommand(const std::string& command) {
  if (command.rfind("ROW:", 0) == 0) {
    activateIndex(atoi(command.c_str() + 4));
    return true;
  }
  return false;
}
#endif
