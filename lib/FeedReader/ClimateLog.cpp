#include "ClimateLog.h"

#include <cstdlib>

namespace feed {

void encodeClimate(const ClimateSample& s, uint8_t out[CLIMATE_RECORD_BYTES]) {
  out[0] = static_cast<uint8_t>(s.when);
  out[1] = static_cast<uint8_t>(s.when >> 8);
  out[2] = static_cast<uint8_t>(s.when >> 16);
  out[3] = static_cast<uint8_t>(s.when >> 24);
  const uint16_t t = static_cast<uint16_t>(s.tempTenths);
  out[4] = static_cast<uint8_t>(t);
  out[5] = static_cast<uint8_t>(t >> 8);
  out[6] = s.humidity;
  out[7] = 0;
}

ClimateSample decodeClimate(const uint8_t in[CLIMATE_RECORD_BYTES]) {
  ClimateSample s;
  s.when = static_cast<uint32_t>(in[0]) | static_cast<uint32_t>(in[1]) << 8 | static_cast<uint32_t>(in[2]) << 16 |
           static_cast<uint32_t>(in[3]) << 24;
  s.tempTenths = static_cast<int16_t>(static_cast<uint16_t>(in[4]) | static_cast<uint16_t>(in[5]) << 8);
  s.humidity = in[6];
  return s;
}

bool worthLogging(const ClimateSample& last, const ClimateSample& now, int minTempTenths, int minHumidity,
                  uint32_t heartbeatSeconds) {
  if (last.when == 0) return true;
  if (now.when >= last.when && now.when - last.when >= heartbeatSeconds) return true;
  if (std::abs(now.tempTenths - last.tempTenths) >= minTempTenths) return true;
  return std::abs(static_cast<int>(now.humidity) - static_cast<int>(last.humidity)) >= minHumidity;
}

std::vector<ClimateBucket> bucketClimate(const std::vector<ClimateSample>& samples, uint32_t from, uint32_t to,
                                         size_t buckets) {
  std::vector<ClimateBucket> out(buckets);
  if (buckets == 0 || to <= from) return out;
  std::vector<int> count(buckets, 0);
  const double span = static_cast<double>(to - from);
  for (const auto& s : samples) {
    if (s.when < from || s.when >= to) continue;
    size_t i = static_cast<size_t>(static_cast<double>(s.when - from) / span * static_cast<double>(buckets));
    if (i >= buckets) i = buckets - 1;
    out[i].tempC += static_cast<float>(s.tempTenths) / 10.0f;
    out[i].humidity += static_cast<float>(s.humidity);
    ++count[i];
  }
  for (size_t i = 0; i < buckets; ++i) {
    if (count[i] == 0) continue;
    out[i].valid = true;
    out[i].tempC /= static_cast<float>(count[i]);
    out[i].humidity /= static_cast<float>(count[i]);
  }
  return out;
}

}  // namespace feed
