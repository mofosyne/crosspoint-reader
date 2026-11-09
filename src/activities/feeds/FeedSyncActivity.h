#pragma once

#include <memory>
#include <string>

#include "activities/Activity.h"
#include "feeds/FeedSync.h"

// Joins Wi-Fi if needed, runs one feed sync a step at a time and shows its progress. Back cancels.
// Leaving the screen after Wi-Fi was used restarts the firmware, as the OPDS browser does, to give the
// heap back.
class FeedSyncActivity final : public Activity {
 public:
  // `scheduled`: started by the daily timer wake. It draws little, and sleeps the device when it ends.
  FeedSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool checkAll, bool scheduled = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
#ifdef CROSSPOINT_SERIAL_INPUT
  std::string debugState() override;
#endif

 private:
  enum class State { Connecting, Running, Done, Error };

  void startSync();
  bool wifiUp() const;

  const bool checkAll_;
  const bool scheduled_;
  State state_ = State::Connecting;
  std::string error_;
  std::unique_ptr<feeds::FeedSync> sync_;
  feeds::FeedSync::Status shown_;
};
