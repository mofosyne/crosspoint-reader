#include "WordListsActivity.h"

#include <Memory.h>

#include <algorithm>

#include "DiscoverActivity.h"
#include "components/UITheme.h"
#include "flashcards/DeckStore.h"

namespace fui = freeink::ui;

WordListsActivity::WordListsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("WordLists", renderer, mappedInput) {}

void WordListsActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void WordListsActivity::reload() {
  RenderLock lock(*this);
  lists_ = flashcards::listWordLists();
  labels_.clear();
  values_.clear();
  rows_.clear();
  for (const auto& name : lists_) {
    const auto owned = flashcards::ownedWords(name);
    flashcards::Deck list;
    std::string value = "cannot open";
    if (list.openFile(std::string(flashcards::WORDLISTS_ROOT) + "/" + name + ".tsv")) {
      size_t fresh = 0;
      for (size_t i = 0; i < list.size(); ++i) fresh += !std::binary_search(owned.begin(), owned.end(), list.state(i).key);
      value = std::to_string(fresh) + " new of " + std::to_string(list.size());
    }
    labels_.push_back(name);
    values_.push_back(value);
  }
  if (lists_.empty()) {
    labels_.push_back("No word lists yet");
    values_.push_back("put .tsv files in /wordlists");
  }
  for (size_t i = 0; i < labels_.size(); ++i) {
    fui::ListItem row;
    row.label = labels_[i].c_str();
    row.value = values_[i].c_str();
    row.actionValue = static_cast<int16_t>(i);
    rows_.push_back(row);
  }
}

void WordListsActivity::buildScreen(UiScreen& screen) {
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

void WordListsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index < 0 || static_cast<size_t>(index) >= lists_.size()) return;
  if (auto discover = makeUniqueNoThrow<DiscoverActivity>(renderer, mappedInput, lists_[static_cast<size_t>(index)])) {
    startActivityForResult(std::move(discover), [this](const ActivityResult&) {
      reload();
      requestUpdate();
    });
  }
}
