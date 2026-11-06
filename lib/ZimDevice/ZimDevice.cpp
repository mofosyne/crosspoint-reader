#include "ZimDevice.h"

#include <HalMemory.h>
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

bool SdFileByteSource::open(const std::string& path) {
  file_ = Storage.open(path.c_str(), O_RDONLY);
  return file_ && file_.isOpen();
}

int32_t SdFileByteSource::readAt(uint64_t offset, void* dst, uint32_t len) {
  const int64_t start = esp_timer_get_time();
  int32_t result = -1;
  if (file_ && file_.seek64(offset)) result = file_.read(dst, len);
  ++stats_.reads;
  stats_.bytes += len;
  stats_.micros += esp_timer_get_time() - start;
  return result;
}

}  // namespace zim
