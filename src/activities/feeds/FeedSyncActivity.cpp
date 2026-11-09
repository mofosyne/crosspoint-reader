#include "FeedSyncActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "SleepRequest.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MIN_SCHEDULED_BATTERY_PERCENT = 20;
}

FeedSyncActivity::FeedSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool checkAll,
                                   bool scheduled)
    : Activity("FeedSync", renderer, mappedInput), checkAll_(checkAll), scheduled_(scheduled) {}

bool FeedSyncActivity::wifiUp() const {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void FeedSyncActivity::onEnter() {
  Activity::onEnter();
  if (scheduled_ && powerManager.getBatteryPercentage() < MIN_SCHEDULED_BATTERY_PERCENT) {
    state_ = State::Error;  // a flat battery is not worth a sync; loop() puts the device back to sleep
    error_ = "Battery low";
    return;
  }
  if (wifiUp()) {
    startSync();
    return;
  }
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    state_ = State::Error;
    error_ = "Not enough memory";
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled || !wifiUp()) {
      state_ = State::Error;
      error_ = tr(STR_WIFI_CONN_FAILED);
      requestUpdate();
    } else {
      startSync();
    }
  });
}

void FeedSyncActivity::startSync() {
  sync_ = makeUniqueNoThrow<feeds::FeedSync>(checkAll_);
  if (!sync_ || !sync_->begin(error_)) {
    state_ = State::Error;
    if (error_.empty()) error_ = "Not enough memory";
    LOG_ERR("FEED", "Sync did not start: %s", error_.c_str());
  } else {
    state_ = State::Running;
  }
  requestUpdate();
}

void FeedSyncActivity::onExit() {
  Activity::onExit();
  sync_.reset();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FeedSyncActivity::loop() {
  if (scheduled_ && (state_ == State::Done || state_ == State::Error)) {
    requestDeepSleep();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state_ == State::Running && sync_) sync_->cancel();
    finish();
    return;
  }
  if (state_ != State::Running || !sync_) return;
  const bool more = sync_->step();  // blocks for one fetch
  {
    RenderLock lock(*this);
    shown_ = sync_->status();
  }
  if (!more) state_ = State::Done;
  if (!scheduled_ || state_ != State::Running) requestUpdate();  // a timed sync does not repaint every step
}

void FeedSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, "Feeds sync");
  const int x = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - 2 * x;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID) + 6;
  const auto line = [&](const std::string& text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
    if (text.empty()) return;
    for (const auto& part : renderer.wrappedText(UI_12_FONT_ID, text.c_str(), width, 3, style)) {
      renderer.drawText(UI_12_FONT_ID, x, y, part.c_str(), true, style);
      y += lineHeight;
    }
  };
  switch (state_) {
    case State::Connecting:
      line(tr(STR_WIFI_CONN_FAILED) == nullptr ? "" : "Connecting to Wi-Fi...");
      break;
    case State::Error:
      line(error_, EpdFontFamily::BOLD);
      break;
    case State::Running:
    case State::Done:
      line(shown_.headline, EpdFontFamily::BOLD);
      line("Sources: " + std::to_string(shown_.sourcesDone) + "/" + std::to_string(shown_.sourcesTotal));
      line("Saved: " + std::to_string(shown_.saved) + "   Failed: " + std::to_string(shown_.failed));
      line(shown_.detail);
      break;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#ifdef CROSSPOINT_SERIAL_INPUT
std::string FeedSyncActivity::debugState() {
  static const char* const NAMES[] = {"connecting", "running", "done", "error"};
  std::string out = std::string("FeedSync ") + NAMES[static_cast<int>(state_)] + "\n";
  if (state_ == State::Error) out += error_ + "\n";
  out += shown_.headline + "\nsources " + std::to_string(shown_.sourcesDone) + "/" +
         std::to_string(shown_.sourcesTotal) + " saved " + std::to_string(shown_.saved) + " failed " +
         std::to_string(shown_.failed) + "\n" + shown_.detail + "\n";
  return out;
}
#endif
