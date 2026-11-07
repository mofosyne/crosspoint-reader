#include "ZimLinkListActivity.h"

#include <I18n.h>

#include "components/UITheme.h"

namespace fui = freeink::ui;

ZimLinkListActivity::ZimLinkListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* title,
                                         std::vector<std::string> labels, int* chosen)
    : UiListActivity("ZimLinks", renderer, mappedInput), title_(title), labels_(std::move(labels)), chosen_(chosen) {
  items_.reserve(labels_.size());
  for (size_t i = 0; i < labels_.size(); ++i) {
    fui::ListItem item;
    item.label = labels_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    items_.push_back(item);
  }
}

const char* ZimLinkListActivity::headerTitle() const { return title_.c_str(); }

void ZimLinkListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = items_.data();
  props.count = static_cast<uint16_t>(items_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void ZimLinkListActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index < 0 || index >= listCount()) return;
  *chosen_ = index;
  finish();
}

void ZimLinkListActivity::onBackButton() {
  *chosen_ = -1;
  finish();
}
