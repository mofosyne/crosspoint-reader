#pragma once

#include <stddef.h>

namespace zim {

// Where the large ZIM buffers and the zstd context live. Passing the allocator in keeps
// this library free of platform code: the firmware supplies PSRAM, the host tests malloc.
// allocate() returns nullptr on failure; release() accepts nullptr.
struct ZimMemory {
  void* (*allocate)(size_t bytes) = nullptr;
  void (*release)(void* ptr) = nullptr;
};

}  // namespace zim
