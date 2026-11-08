#pragma once

#include <cstdint>
#include <string>

// A viewer that can be reopened after deep sleep (ZIM and feed articles).
struct SleepResume {
  uint8_t kind = 0;       // 1 = ZIM article, 2 = feed article
  std::string container;  // ZIM file path (kind 1)
  std::string target;     // article path inside the archive, or the feed article URL
  int page = 0;
};
