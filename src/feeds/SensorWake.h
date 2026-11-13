#pragma once

#include <ClimateLog.h>

#include <cstddef>
#include <cstdint>

namespace feeds {

// The state kept in the chip's retained memory across deep sleep, so a sensor wake can decide what to do without
// starting the SD card: what the next wake is for, when the next sync or redraw is due, what is on the panel, and
// the readings taken since the card was last written.
struct SleepPlan {
  uint8_t nextKind = 0;         // a feed::ClimateWake: WAKE_SENSOR means the next timer wake is a sensor-only one
  uint32_t intervalSeconds = 0;  // between sensor wakes; 0 = no sensor wakes
  uint32_t majorAt = 0;         // seconds since 1970 of the next sync or redraw, 0 = none
  uint8_t majorKind = 0;        // WAKE_SYNC or WAKE_REDRAW
  bool drawnKnown = false;      // the panel shows these room readings
  int16_t drawnTempTenths = 0;
  uint8_t drawnHumidity = 0;
  bool logEvery = false;           // testing: keep every reading, not only the ones that moved
  feed::ClimateSample lastLogged;  // the newest reading in the log or the ring
};
SleepPlan& sleepPlan();

// Readings waiting for the next time the SD card is up.
constexpr size_t RING_CAPACITY = 160;
size_t pendingReadings();
void clearPendingReadings();
const feed::ClimateSample* pendingReading(size_t index);

// Early in boot after a sensor-timer wake: reads the room and the battery, keeps the reading, and goes back to
// sleep. Returns only when a full start is wanted instead (the room changed enough to redraw the dashboard, the
// readings are about to overflow, or the clock cannot be read); it never returns when it went back to sleep.
bool runSensorWake();

// Writes the waiting readings to the SD card log (call once the card is up).
void flushPendingReadings();

// The dashboard remembers what it drew, so a sensor wake knows when the room has changed enough to redraw it.
void noteDrawn(int16_t tempTenths, uint8_t humidity);

}  // namespace feeds
