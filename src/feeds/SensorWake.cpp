#include "SensorWake.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <Calendar.h>
#include <HalClock.h>
#include <PowerManager.h>
#include <esp_sleep.h>

#include <cstdlib>

#include "FeedStore.h"

namespace feeds {
namespace {

RTC_DATA_ATTR SleepPlan g_plan;
RTC_DATA_ATTR feed::ClimateSample g_ring[RING_CAPACITY];
RTC_DATA_ATTR size_t g_ringCount = 0;
RTC_DATA_ATTR uint32_t g_magic = 0;
constexpr uint32_t MAGIC = 0x434c4d54;  // "CLMT"

constexpr int REDRAW_TEMP_TENTHS = 10;  // a degree
constexpr int REDRAW_HUMIDITY = 6;      // points

void ensureInitialised() {
  if (g_magic == MAGIC) return;  // the retained memory is only zeroed by a power-on reset
  g_plan = SleepPlan{};
  g_ringCount = 0;
  g_magic = MAGIC;
}

}  // namespace

SleepPlan& sleepPlan() {
  ensureInitialised();
  return g_plan;
}

size_t pendingReadings() {
  ensureInitialised();
  return g_ringCount;
}

const feed::ClimateSample* pendingReading(size_t index) { return index < pendingReadings() ? &g_ring[index] : nullptr; }

void clearPendingReadings() { g_ringCount = 0; }

void noteDrawn(int16_t tempTenths, uint8_t humidity) {
  SleepPlan& plan = sleepPlan();
  plan.drawnKnown = true;
  plan.drawnTempTenths = tempTenths;
  plan.drawnHumidity = humidity;
}

void flushPendingReadings() {
  ensureInitialised();
  if (g_ringCount == 0) return;
  appendClimateRecords(g_ring, g_ringCount);
  g_plan.lastLogged = g_ring[g_ringCount - 1];
  g_ringCount = 0;
}

bool runSensorWake() {
  ensureInitialised();
  SleepPlan& plan = g_plan;

  // The time comes from the clock chip, not the chip's own timer, which drifts by minutes a day.
  halClock.begin();
  struct tm utc;
  uint32_t now = 0;
  if (halClock.localTime(utc)) {  // no time zone is set yet, so this is UTC
    now = static_cast<uint32_t>(feed::daysFromCivil(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday)) * 86400u +
          static_cast<uint32_t>(utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec);
  }
  feed::ClimateSample sample;
  const bool roomOk = takeClimateReading(feed::WAKE_SENSOR, sample);
  sample.when = now;
  // millis() counts from the start of this program, so this is the whole awake time bar the boot loader (~0.25 s)
  const unsigned long awakeSoFar = millis();
  sample.awake = static_cast<uint8_t>(awakeSoFar / 10 > 255 ? 255 : awakeSoFar / 10);
  if (now == 0 || !roomOk) return true;  // cannot date or read it: let a full start sort it out

  if ((plan.logEvery || feed::worthLogging(plan.lastLogged, sample)) && g_ringCount < RING_CAPACITY) {
    g_ring[g_ringCount++] = sample;
    plan.lastLogged = sample;
  }
  const bool changed = plan.drawnKnown && (std::abs(sample.tempTenths - plan.drawnTempTenths) >= REDRAW_TEMP_TENTHS ||
                                           std::abs(static_cast<int>(sample.humidity) - plan.drawnHumidity) >= REDRAW_HUMIDITY);
  const bool nearlyFull = g_ringCount + 4 >= RING_CAPACITY;
  if (changed || nearlyFull) return true;  // a full start redraws the dashboard and writes the card

  // Back to sleep until the next sensor reading, or the sync or redraw if that comes first.
  const uint32_t interval = plan.intervalSeconds > 0 ? plan.intervalSeconds : 600;
  uint32_t seconds = interval - (now % interval);  // on the clock, so the readings line up
  if (seconds < 20) seconds += interval;
  uint8_t kind = feed::WAKE_SENSOR;
  if (plan.majorAt != 0 && plan.majorAt <= now + seconds) {
    seconds = plan.majorAt > now + 5 ? plan.majorAt - now : 5;
    kind = plan.majorKind;
  }
  plan.nextKind = kind;
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  const auto& in = BoardConfig::ACTIVE.input;
  uint64_t mask = 0;  // the power button and the page buttons still wake it
  for (const int8_t pin : {in.power, in.up, in.down}) {
    if (pin >= 0) mask |= 1ULL << pin;
  }
  freeink::PowerManager::armWakeOnPins(mask, /*wakeLow=*/!in.powerActiveHigh);
  freeink::PowerManager::deepSleep();
}

}  // namespace feeds
