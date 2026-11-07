#include "FeedsActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <ctime>

#include "FeedItemListActivity.h"
#include "FeedSyncActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

FeedsActivity::FeedsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Feeds", renderer, mappedInput) {}

void FeedsActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void FeedsActivity::reload() {
  RenderLock lock(*this);
  haveFile_ = feeds::loadSources(sources_);
  labels_.clear();
  values_.clear();
  rows_.clear();
  labels_.push_back("Sync now");
  values_.push_back(haveFile_ ? "" : "no sources file");
  labels_.push_back("Check every source");
  values_.push_back("");
  for (const auto& source : sources_.sources) {
    feeds::SourceState state;
    feeds::loadState(source.id, state);
    const size_t unread = feeds::countUnread(source.id);
    labels_.push_back(source.name);
    std::string value = std::to_string(unread) + " unread";
    if (state.lastCheck > 0) {
      const long days = static_cast<long>((time(nullptr) - state.lastCheck) / 86400);
      value += days > 0 && time(nullptr) > 1700000000 ? ", " + std::to_string(days) + "d ago" : ", checked";
    } else {
      value += ", never checked";
    }
    values_.push_back(value);
  }
  sourceRows_ = sources_.sources.size();
  for (const auto& error : sources_.errors) {
    labels_.push_back("! " + error);
    values_.push_back("");
  }
  for (size_t i = 0; i < labels_.size(); ++i) {
    fui::ListItem row;
    row.label = labels_[i].c_str();
    row.value = values_[i].empty() ? nullptr : values_[i].c_str();
    row.actionValue = static_cast<int16_t>(i);
    rows_.push_back(row);
  }
}

void FeedsActivity::buildScreen(UiScreen& screen) {
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

void FeedsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index < 0 || static_cast<size_t>(index) >= rows_.size()) return;
  if (index == 0 || index == 1) {
    auto sync = makeUniqueNoThrow<FeedSyncActivity>(renderer, mappedInput, /*checkAll=*/index == 1);
    if (sync) startActivityForResult(std::move(sync), [this](const ActivityResult&) { reload(); requestUpdate(); });
    return;
  }
  const size_t source = static_cast<size_t>(index) - 2;
  if (source >= sourceRows_) return;  // a line about a problem in the sources file
  auto list = makeUniqueNoThrow<FeedItemListActivity>(renderer, mappedInput, sources_.sources[source].id,
                                                      sources_.sources[source].name);
  if (list) startActivityForResult(std::move(list), [this](const ActivityResult&) { reload(); requestUpdate(); });
}

#ifdef CROSSPOINT_SERIAL_INPUT
std::string FeedsActivity::debugState() {
  std::string out = "Feeds\n";
  for (size_t i = 0; i < labels_.size(); ++i) out += std::to_string(i) + ": " + labels_[i] + "  " + values_[i] + "\n";
  return out;
}

bool FeedsActivity::debugCommand(const std::string& command) {
  if (command.rfind("ROW:", 0) == 0) {
    activateIndex(atoi(command.c_str() + 4));
    return true;
  }
  return false;
}
#endif
