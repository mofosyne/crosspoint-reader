#include "FeedItemListActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <ctime>

#include "components/UITheme.h"
#include "feeds/FeedArticleView.h"

namespace fui = freeink::ui;

namespace {
std::string dateText(int64_t seconds) {
  if (seconds <= 0) return {};
  const time_t t = static_cast<time_t>(seconds);
  tm parts{};
  gmtime_r(&t, &parts);
  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &parts);
  return buf;
}
}  // namespace

FeedItemListActivity::FeedItemListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::string sourceId, std::string title)
    : UiListActivity("FeedItems", renderer, mappedInput), sourceId_(std::move(sourceId)), title_(std::move(title)) {}

void FeedItemListActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void FeedItemListActivity::reload() {
  RenderLock lock(*this);
  items_ = feeds::listItems(sourceId_);
  labels_.clear();
  values_.clear();
  rows_.clear();
  labels_.reserve(items_.size());
  values_.reserve(items_.size());
  for (const auto& item : items_) {
    labels_.push_back((item.read ? "" : "* ") + item.title);
    values_.push_back(dateText(item.published ? item.published : item.fetched));
  }
  for (size_t i = 0; i < items_.size(); ++i) {
    fui::ListItem row;
    row.label = labels_[i].c_str();
    row.value = values_[i].empty() ? nullptr : values_[i].c_str();
    row.actionValue = static_cast<int16_t>(i);
    rows_.push_back(row);
  }
}

void FeedItemListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (rows_.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
  }
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

void FeedItemListActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index < 0 || static_cast<size_t>(index) >= items_.size()) return;
  auto viewer = feeds::makeArticleViewer(renderer, mappedInput, items_[index].url);
  if (!viewer) {
    LOG_ERR("FEED", "Cannot open %s", items_[index].url.c_str());
    return;
  }
  startActivityForResult(std::move(viewer), [this](const ActivityResult&) { reload(); requestUpdate(); });
}

#ifdef CROSSPOINT_SERIAL_INPUT
std::string FeedItemListActivity::debugState() {
  std::string out = "FeedItems '" + title_ + "'\n";
  for (size_t i = 0; i < items_.size(); ++i) {
    out += std::to_string(i) + ": " + labels_[i] + "  " + values_[i] + "\n";
  }
  return out;
}

bool FeedItemListActivity::debugCommand(const std::string& command) {
  if (command.rfind("ROW:", 0) == 0) {
    activateIndex(atoi(command.c_str() + 4));
    return true;
  }
  return false;
}
#endif
