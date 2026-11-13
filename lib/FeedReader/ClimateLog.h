#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace feed {

// Indoor temperature and humidity readings, logged only when they have moved enough to matter.
struct ClimateSample {
  uint32_t when = 0;        // seconds since 1970
  int16_t tempTenths = 0;   // tenths of a degree Celsius
  uint8_t humidity = 0;     // percent relative humidity
  uint8_t battery = 0;      // percent charge, 0 = not recorded
  uint16_t millivolts = 0;  // battery voltage, 0 = not recorded
  uint8_t flags = 0;        // CLIMATE_CHARGING, CLIMATE_EXTERNAL_POWER, and the wake kind in the top four bits
  uint8_t awake = 0;        // a sensor wake: how long the device was awake, in 10 ms steps (255 = 2.55 s or more)
};

// Bits of ClimateSample::flags. The wake kind says what the device was doing when the reading was taken, so a
// battery report can tell a night asleep from a day of reading.
constexpr uint8_t CLIMATE_CHARGING = 1;
constexpr uint8_t CLIMATE_EXTERNAL_POWER = 2;
enum ClimateWake : uint8_t {
  WAKE_OTHER = 0,     // a cold boot, or the dashboard being opened
  WAKE_BUTTON = 1,    // the power button
  WAKE_SYNC = 2,      // the timed sync
  WAKE_REDRAW = 3,    // the timed midnight redraw
  WAKE_PAGE = 4,      // a page button on the dashboard
  WAKE_SENSOR = 5,    // a sensor-only wake
  WAKE_DASHBOARD = 6  // the dashboard drawn while awake
};
inline uint8_t climateWakeOf(const ClimateSample& s) { return static_cast<uint8_t>(s.flags >> 4); }

// A record in the log file: 12 bytes, little endian: when, temperature, humidity, battery percent, battery millivolts,
// flags, awake time.
constexpr size_t CLIMATE_RECORD_BYTES = 12;
void encodeClimate(const ClimateSample& sample, uint8_t out[CLIMATE_RECORD_BYTES]);
ClimateSample decodeClimate(const uint8_t in[CLIMATE_RECORD_BYTES]);

// Whether `now` should be added to the log after `last` (when == 0 means there is no earlier sample): the
// temperature moved by `minTempTenths`, the humidity by `minHumidity` or the battery by `minBattery`, or
// `heartbeatSeconds` have passed.
bool worthLogging(const ClimateSample& last, const ClimateSample& now, int minTempTenths = 3, int minHumidity = 2,
                  int minBattery = 2, uint32_t heartbeatSeconds = 3 * 3600);

// The readings from `from` to `to` (seconds) as `buckets` equal slices, each the average of its samples, so a long
// log can be drawn as a short line. A slice without samples keeps `valid` false.
struct ClimateBucket {
  bool valid = false;
  float tempC = 0;
  float humidity = 0;
  bool hasBattery = false;
  float battery = 0;  // percent, averaged over the readings that recorded it
};
std::vector<ClimateBucket> bucketClimate(const std::vector<ClimateSample>& samples, uint32_t from, uint32_t to,
                                         size_t buckets);

}  // namespace feed
