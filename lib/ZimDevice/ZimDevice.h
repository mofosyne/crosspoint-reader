#pragma once

#include <ByteSource.h>
#include <HalStorage.h>
#include <ZimMemory.h>

#include <string>

// Firmware glue for the ZIM reader: SD access and PSRAM buffers.
namespace zim {

// Smallest PSRAM (total, not free) that fits the cluster buffers plus the zstd context
// with room for the rest of the firmware: 4 + 1 MiB buffers, 96 KB context, ~1 MiB spare.
constexpr size_t MIN_PSRAM_BYTES = 6 * 1024 * 1024;

// True when this board has enough PSRAM for the ZIM reader. Checked at run time so one
// binary is not tied to a board name.
bool deviceSupportsZim();

// Allocates from PSRAM only; never falls back to internal RAM.
const ZimMemory& psramMemory();

// Read-only ByteSource over an SD file. One open handle per instance.
class SdFileByteSource : public freeink::content::ByteSource {
 public:
  bool open(const std::string& path);
  void close() { file_.close(); }
  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override;
  uint64_t size() const override { return file_ ? file_.fileSize64() : 0; }

  // Read counters for benchmarks; reset() before the operation being measured.
  struct Stats {
    uint32_t reads = 0;
    uint64_t bytes = 0;
    int64_t micros = 0;
  };
  const Stats& stats() const { return stats_; }
  void resetStats() { stats_ = Stats{}; }

 private:
  mutable HalFile file_;
  Stats stats_;
};

}  // namespace zim
