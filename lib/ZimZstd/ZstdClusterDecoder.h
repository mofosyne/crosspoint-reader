#pragma once

#include <ZimReader.h>

#include "ZimMemory.h"

struct ZSTD_DCtx_s;

namespace zim {

// Single-shot zstd decoding of one ZIM cluster. Kiwix frames do not declare their content
// size, so the caller's buffer must hold the whole cluster; a buffer that is too small
// returns DECODE_TOO_SMALL so the caller can retry with a bigger one.
class ZstdClusterDecoder : public ClusterDecoder {
 public:
  // The decompression context (about 96 KB) is allocated through `memory`. Check valid().
  explicit ZstdClusterDecoder(const ZimMemory& memory);
  ~ZstdClusterDecoder() override;
  ZstdClusterDecoder(const ZstdClusterDecoder&) = delete;
  ZstdClusterDecoder& operator=(const ZstdClusterDecoder&) = delete;

  bool valid() const { return dctx_ != nullptr; }

  // `src` may run past the end of the frame; only the first frame is decoded.
  int64_t decode(Compression compression, const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) override;

 private:
  ZimMemory memory_;
  ZSTD_DCtx_s* dctx_ = nullptr;
};

}  // namespace zim
