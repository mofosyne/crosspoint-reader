#pragma once

#include <ZimReader.h>

#include "ZimMemory.h"

namespace zim {

// Owns the two large cluster buffers and reads blobs through them. The plain buffer starts
// at `initialPlainBytes` and grows once, to `maxBufferBytes`, when a cluster does not fit.
// Nothing is allocated until the first read, and every buffer is released on destruction.
class ZimBlobLoader {
 public:
  struct Limits {
    size_t packedBytes = 1024 * 1024;         // compressed cluster as stored (seen: 130-400 KB)
    size_t initialPlainBytes = 2560 * 1024;   // decoded cluster (seen: 1.83-2.09 MB)
    size_t maxBufferBytes = 4 * 1024 * 1024;  // hard cap for either buffer
  };

  explicit ZimBlobLoader(const ZimMemory& memory) : memory_(memory) {}
  ZimBlobLoader(const ZimMemory& memory, const Limits& limits) : memory_(memory), limits_(limits) {}
  ~ZimBlobLoader() { release(); }
  ZimBlobLoader(const ZimBlobLoader&) = delete;
  ZimBlobLoader& operator=(const ZimBlobLoader&) = delete;

  // Same contract as ZimReader::readBlob, except that a buffer that is too small is
  // replaced by a larger one (up to maxBufferBytes) and the read is retried. Returns
  // BufferTooSmall only when the cluster needs more than the cap, and OutOfMemory when
  // a buffer cannot be allocated. `out` stays valid until the next read.
  Status read(ZimReader& reader, ClusterDecoder* decoder, uint32_t clusterNumber, uint32_t blobNumber, Blob& out);

  // Frees both buffers (for example when leaving the ZIM screen).
  void release();

  size_t packedCapacity() const { return packedCap_; }
  size_t plainCapacity() const { return plainCap_; }

 private:
  bool ensurePacked(size_t bytes);
  bool ensurePlain(size_t bytes);

  ZimMemory memory_;
  Limits limits_;
  uint8_t* packed_ = nullptr;
  size_t packedCap_ = 0;
  uint8_t* plain_ = nullptr;
  size_t plainCap_ = 0;
};

}  // namespace zim
