#include "ZimDevice.h"

#include <HalMemory.h>
#include <Logging.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace zim {

bool deviceSupportsZim() { return HalMemory::getPsramHeap().totalBytes >= MIN_PSRAM_BYTES; }

const ZimMemory& psramMemory() {
  // Same capabilities as HalMemory::allocatePsram. A raw pointer is needed because the
  // buffers change size and zstd takes C-style allocators; the loader and decoder own them.
  static const ZimMemory memory = {
      [](size_t bytes) -> void* { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); },
      [](void* ptr) { heap_caps_free(ptr); },
  };
  return memory;
}

bool SdFileByteSource::openFile(const std::string& path) {
  file_ = Storage.open(path.c_str(), O_RDONLY);
  return file_ && file_.isOpen();
}

int32_t SdFileByteSource::readAt(uint64_t offset, void* dst, uint32_t len) {
  const int64_t start = esp_timer_get_time();
  int32_t result = -1;
  int64_t seekUs = 0;
  if (file_ && file_.seek64(offset)) {
    seekUs = esp_timer_get_time() - start;
    result = file_.read(dst, len);
  }
  const int64_t totalUs = esp_timer_get_time() - start;
  ++stats_.reads;
  stats_.bytes += len;
  stats_.micros += totalUs;
  // A slow seek points at a fragmented exFAT file (the FAT chain is walked on each seek).
  if (totalUs > SLOW_READ_US && slowLogged_ < MAX_SLOW_LOGS) {
    ++slowLogged_;
    LOG_INF("ZIM", "Slow SD read: offset %llu, %u bytes, seek %lld ms, total %lld ms",
            static_cast<unsigned long long>(offset), static_cast<unsigned>(len), static_cast<long long>(seekUs / 1000),
            static_cast<long long>(totalUs / 1000));
  }
  return result;
}

}  // namespace zim
