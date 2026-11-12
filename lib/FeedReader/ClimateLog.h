#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace feed {

// Indoor temperature and humidity readings, logged only when they have moved enough to matter.
struct ClimateSample {
  uint32_t when = 0;       // seconds since 1970
  int16_t tempTenths = 0;  // tenths of a degree Celsius
  uint8_t humidity = 0;    // percent relative humidity
};

// A record in the log file: 8 bytes, little endian: when, temperature, humidity, one spare byte.
constexpr size_t CLIMATE_RECORD_BYTES = 8;
void encodeClimate(const ClimateSample& sample, uint8_t out[CLIMATE_RECORD_BYTES]);
ClimateSample decodeClimate(const uint8_t in[CLIMATE_RECORD_BYTES]);

// Whether `now` should be added to the log after `last` (when == 0 means there is no earlier sample): the
// temperature moved by `minTempTenths` or the humidity by `minHumidity`, or `heartbeatSeconds` have passed.
bool worthLogging(const ClimateSample& last, const ClimateSample& now, int minTempTenths = 3, int minHumidity = 2,
                  uint32_t heartbeatSeconds = 3 * 3600);

// The readings from `from` to `to` (seconds) as `buckets` equal slices, each the average of its samples, so a long
// log can be drawn as a short line. A slice without samples keeps `valid` false.
struct ClimateBucket {
  bool valid = false;
  float tempC = 0;
  float humidity = 0;
};
std::vector<ClimateBucket> bucketClimate(const std::vector<ClimateSample>& samples, uint32_t from, uint32_t to,
                                         size_t buckets);

}  // namespace feed
