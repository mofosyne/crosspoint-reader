#include "ZstdClusterDecoder.h"

#define ZSTD_STATIC_LINKING_ONLY  // ZSTD_createDCtx_advanced
#include <zstd.h>
#include <zstd_errors.h>

namespace zim {

namespace {
void* zstdAllocate(void* opaque, size_t bytes) { return static_cast<const ZimMemory*>(opaque)->allocate(bytes); }
void zstdRelease(void* opaque, void* ptr) { static_cast<const ZimMemory*>(opaque)->release(ptr); }
}  // namespace

ZstdClusterDecoder::ZstdClusterDecoder(const ZimMemory& memory) : memory_(memory) {
  if (!memory_.allocate || !memory_.release) return;
  // zstd keeps the pointer, so it must point at memory_ (a member), not at the argument.
  const ZSTD_customMem custom = {zstdAllocate, zstdRelease, &memory_};
  dctx_ = ZSTD_createDCtx_advanced(custom);
}

ZstdClusterDecoder::~ZstdClusterDecoder() {
  if (dctx_) ZSTD_freeDCtx(dctx_);
}

int64_t ZstdClusterDecoder::decode(Compression compression, const uint8_t* src, size_t srcLen, uint8_t* dst,
                                   size_t dstCap) {
  if (compression != Compression::Zstd || !dctx_ || !src || !dst) return DECODE_ERROR;
  // The last cluster has no known end, so cut the input at the end of its first frame.
  const size_t frameLen = ZSTD_findFrameCompressedSize(src, srcLen);
  if (ZSTD_isError(frameLen)) return DECODE_ERROR;
  const unsigned long long declared = ZSTD_getFrameContentSize(src, frameLen);
  if (declared == ZSTD_CONTENTSIZE_ERROR) return DECODE_ERROR;
  if (declared != ZSTD_CONTENTSIZE_UNKNOWN && declared > dstCap) return DECODE_TOO_SMALL;
  const size_t result = ZSTD_decompressDCtx(dctx_, dst, dstCap, src, frameLen);
  if (ZSTD_isError(result)) {
    return ZSTD_getErrorCode(result) == ZSTD_error_dstSize_tooSmall ? DECODE_TOO_SMALL : DECODE_ERROR;
  }
  return static_cast<int64_t>(result);
}

}  // namespace zim
