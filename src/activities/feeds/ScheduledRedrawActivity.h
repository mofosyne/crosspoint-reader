#pragma once

#include "SleepRequest.h"
#include "activities/Activity.h"

// The midnight timer wake: nothing to do but go back to sleep, which redraws the dashboard with the new
// date and the events stored on the card. It draws nothing itself.
class ScheduledRedrawActivity final : public Activity {
 public:
  ScheduledRedrawActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ScheduledRedraw", renderer, mappedInput) {}

  void loop() override { requestDeepSleep(); }
  void render(RenderLock&&) override {}
};
