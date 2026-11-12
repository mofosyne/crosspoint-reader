#include "ExtrasActivity.h"

#include <Memory.h>

#include "DashboardMenuActivity.h"
#include "FeedsActivity.h"
#include "activities/flashcards/FlashcardsActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
const char* const ROWS[] = {"Dashboard", "Flashcards", "Feeds"};
}

ExtrasActivity::ExtrasActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Extras", renderer, mappedInput) {}

void ExtrasActivity::onEnter() {
  UiListActivity::onEnter();
  RenderLock lock(*this);
  rows_.clear();
  for (size_t i = 0; i < std::size(ROWS); ++i) {
    fui::ListItem row;
    row.label = ROWS[i];
    row.actionValue = static_cast<int16_t>(i);
    rows_.push_back(row);
  }
}

void ExtrasActivity::buildScreen(UiScreen& screen) {
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
  syncListViewport(screen, props);
  screen.list(props);
}

void ExtrasActivity::activateIndex(const int index) {
  app.clearTapFlash();
  std::unique_ptr<Activity> next;
  if (index == 0) next = makeUniqueNoThrow<DashboardMenuActivity>(renderer, mappedInput);
  if (index == 1) next = makeUniqueNoThrow<FlashcardsActivity>(renderer, mappedInput);
  if (index == 2) next = makeUniqueNoThrow<FeedsActivity>(renderer, mappedInput);
  if (next) startActivityForResult(std::move(next), [](const ActivityResult&) {});
}

#ifdef CROSSPOINT_SERIAL_INPUT
bool ExtrasActivity::debugCommand(const std::string& command) {
  if (command.rfind("ROW:", 0) == 0) {
    activateIndex(atoi(command.c_str() + 4));
    return true;
  }
  return false;
}
#endif
