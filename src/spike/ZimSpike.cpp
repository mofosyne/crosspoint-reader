#include "ZimSpike.h"

#ifdef ZIM_SPIKE

#include <Arduino.h>
#include <ByteSource.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ZimReader.h>
#include <esp_timer.h>

#include <algorithm>
#include <string>
#include <vector>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include "esp_heap_caps.h"

namespace {

constexpr const char* ZIM_PATH = "/zim/wikipedia_en_all_mini_2024-04.zim";
constexpr const char* SKIP_MARKER = "/zim/skip_spike";

int64_t nowUs() { return esp_timer_get_time(); }

void logHeap(const char* when) {
  const auto internal = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  LOG_INF("ZSPK", "heap %s: internal free=%u largest=%u min=%u | psram free=%u largest=%u total=%u", when,
          static_cast<unsigned>(internal.freeBytes), static_cast<unsigned>(internal.largestBlockBytes),
          static_cast<unsigned>(internal.minFreeBytes), static_cast<unsigned>(psram.freeBytes),
          static_cast<unsigned>(psram.largestBlockBytes), static_cast<unsigned>(psram.totalBytes));
}

// ByteSource over an SD file that also times the reads.
class SdSource : public freeink::content::ByteSource {
 public:
  bool open(const char* path) {
    file_ = Storage.open(path, O_RDONLY);
    return file_ && file_.isOpen();
  }
  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override {
    const int64_t t0 = nowUs();
    int32_t r = -1;
    if (file_ && file_.seek64(offset)) r = file_.read(dst, len);
    usInRead += nowUs() - t0;
    ++calls;
    bytes += len;
    return r;
  }
  uint64_t size() const override { return file_ ? file_.fileSize64() : 0; }

  uint32_t calls = 0;
  uint64_t bytes = 0;
  int64_t usInRead = 0;

 private:
  mutable HalFile file_;
};

void* zstdAlloc(void*, size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void zstdFree(void*, void* ptr) { heap_caps_free(ptr); }

// Decoder with its context in PSRAM; times the decode itself.
class ZstdDecoder : public zim::ClusterDecoder {
 public:
  bool init() {
    ZSTD_customMem mem = {zstdAlloc, zstdFree, nullptr};
    dctx_ = ZSTD_createDCtx_advanced(mem);
    return dctx_ != nullptr;
  }
  ~ZstdDecoder() override {
    if (dctx_) ZSTD_freeDCtx(dctx_);
  }
  int64_t decode(zim::Compression compression, const uint8_t* src, size_t srcLen, uint8_t* dst,
                 size_t dstCap) override {
    if (compression != zim::Compression::Zstd || !dctx_) return zim::DECODE_ERROR;
    const size_t frame = ZSTD_findFrameCompressedSize(src, srcLen);
    if (ZSTD_isError(frame)) return zim::DECODE_ERROR;
    const unsigned long long declared = ZSTD_getFrameContentSize(src, frame);
    lastDeclared = declared;
    if (declared != ZSTD_CONTENTSIZE_UNKNOWN && declared != ZSTD_CONTENTSIZE_ERROR && declared > dstCap) {
      return zim::DECODE_TOO_SMALL;
    }
    const int64_t t0 = nowUs();
    const size_t r = ZSTD_decompressDCtx(dctx_, dst, dstCap, src, frame);
    decodeUs += nowUs() - t0;
    return ZSTD_isError(r) ? zim::DECODE_ERROR : static_cast<int64_t>(r);
  }

  int64_t decodeUs = 0;
  unsigned long long lastDeclared = 0;

 private:
  ZSTD_DCtx* dctx_ = nullptr;
};

struct Stats {
  int64_t sum = 0;
  int64_t min = INT64_MAX;
  int64_t max = 0;
  uint32_t n = 0;
  void add(int64_t v) {
    sum += v;
    min = std::min(min, v);
    max = std::max(max, v);
    ++n;
  }
  void log(const char* what) const {
    if (n == 0) return;
    LOG_INF("ZSPK", "%-26s n=%u avg=%lld min=%lld max=%lld us", what, static_cast<unsigned>(n),
            static_cast<long long>(sum / n), static_cast<long long>(min), static_cast<long long>(max));
  }
};

uint32_t xorshift(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

void benchSequential(SdSource& src, uint8_t* buf, size_t bufCap) {
  const uint64_t fileSize = src.size();
  constexpr size_t BLOCK = 256 * 1024;
  if (bufCap < BLOCK) return;
  for (int i = 1; i <= 3; ++i) {
    const uint64_t off = (fileSize / 4) * i;
    const int64_t t0 = nowUs();
    const int32_t r = src.readAt(off, buf, BLOCK);
    const int64_t dt = nowUs() - t0;
    LOG_INF("ZSPK", "seq 256KB single read @%llu: r=%d %lld us = %lld KB/s", static_cast<unsigned long long>(off),
            static_cast<int>(r), static_cast<long long>(dt),
            dt > 0 ? static_cast<long long>(BLOCK * 1000000ll / dt / 1024) : 0ll);
    delay(1);
  }
  constexpr size_t CHUNK = 32 * 1024;
  for (int i = 1; i <= 2; ++i) {
    const uint64_t off = (fileSize / 5) * i + 7 * 1024 * 1024;
    const int64_t t0 = nowUs();
    for (size_t done = 0; done < BLOCK; done += CHUNK) src.readAt(off + done, buf, CHUNK);
    const int64_t dt = nowUs() - t0;
    LOG_INF("ZSPK", "seq 256KB as 8x32KB @%llu: %lld us = %lld KB/s", static_cast<unsigned long long>(off),
            static_cast<long long>(dt), dt > 0 ? static_cast<long long>(BLOCK * 1000000ll / dt / 1024) : 0ll);
    delay(1);
  }
}

void benchRandom(SdSource& src, uint8_t* buf) {
  const uint64_t sectors = src.size() / 512;
  uint32_t seed = 0x1234567u;
  Stats stats;
  for (int i = 0; i < 300; ++i) {
    const uint64_t sector = ((static_cast<uint64_t>(xorshift(seed)) << 16) ^ xorshift(seed)) % sectors;
    const int64_t t0 = nowUs();
    src.readAt(sector * 512, buf, 512);
    stats.add(nowUs() - t0);
    if ((i & 15) == 0) delay(1);
  }
  stats.log("random 512B read");
}

bool pickArticle(zim::ZimReader& zr, uint32_t& seed, zim::Entry& out) {
  for (int tries = 0; tries < 200; ++tries) {
    const uint32_t idx = xorshift(seed) % zr.entryCount();
    if (zr.entryByTitleIndex(idx, out) != zim::Status::Ok) continue;
    if (out.ns == 'A' && out.kind == zim::EntryKind::Content && zr.mimeType(out.mimeIndex) == "text/html") return true;
  }
  return false;
}

void benchZim(SdSource& src, uint8_t* packed, size_t packedCap, uint8_t* plain, size_t plainCap) {
  auto zr = makeUniqueNoThrow<zim::ZimReader>();
  auto decoder = makeUniqueNoThrow<ZstdDecoder>();
  if (!zr || !decoder || !decoder->init()) {
    LOG_ERR("ZSPK", "OOM: reader or decoder");
    return;
  }
  src.calls = 0;
  src.usInRead = 0;
  int64_t t0 = nowUs();
  const zim::Status st = zr->open(src);
  LOG_INF("ZSPK", "ZimReader::open st=%d %lld us, %u reads, entries=%u clusters=%u", static_cast<int>(st),
          static_cast<long long>(nowUs() - t0), static_cast<unsigned>(src.calls),
          static_cast<unsigned>(zr->entryCount()), static_cast<unsigned>(zr->header().clusterCount));
  if (st != zim::Status::Ok) return;
  logHeap("after zim open");

  zim::Workspace ws{packed, packedCap, plain, plainCap};
  zim::Entry e;
  zim::Blob blob;
  Stats lookupUs, lookupReads, blobTotalUs, blobSdUs, blobDecodeUs, blobBytes;

  // The known article first, then random ones from the title list.
  uint32_t seed = 0xC0FFEEu;
  for (int i = 0; i < 21; ++i) {
    zim::Entry pick;
    if (i == 0) {
      pick.url = "Albert_Einstein";
    } else if (!pickArticle(*zr, seed, pick)) {
      break;
    }
    const std::string url = pick.url;

    src.calls = 0;
    t0 = nowUs();
    const zim::Status fs = zr->findContent(url, e);
    const int64_t findUs = nowUs() - t0;
    if (fs != zim::Status::Ok || zr->resolveRedirects(e) != zim::Status::Ok) {
      LOG_ERR("ZSPK", "lookup failed for %s st=%d", url.c_str(), static_cast<int>(fs));
      continue;
    }
    const uint32_t findReads = src.calls;
    lookupUs.add(findUs);
    lookupReads.add(findReads);

    src.usInRead = 0;
    decoder->decodeUs = 0;
    t0 = nowUs();
    const zim::Status bs = zr->readBlob(e.clusterNumber, e.blobNumber, decoder.get(), ws, blob);
    const int64_t totalUs = nowUs() - t0;
    if (bs != zim::Status::Ok) {
      LOG_ERR("ZSPK", "readBlob failed for %s st=%d", url.c_str(), static_cast<int>(bs));
      continue;
    }
    blobTotalUs.add(totalUs);
    blobSdUs.add(src.usInRead);
    blobDecodeUs.add(decoder->decodeUs);
    blobBytes.add(static_cast<int64_t>(blob.size));
    LOG_INF(
        "ZSPK",
        "%-34.34s find=%lld us (%u reads) blob=%u B cluster=%u total=%lld sd=%lld decode=%lld us (frame says %llu B)",
        url.c_str(), static_cast<long long>(findUs), static_cast<unsigned>(findReads), static_cast<unsigned>(blob.size),
        static_cast<unsigned>(e.clusterNumber), static_cast<long long>(totalUs), static_cast<long long>(src.usInRead),
        static_cast<long long>(decoder->decodeUs), decoder->lastDeclared);
    delay(1);
  }
  lookupUs.log("findContent");
  lookupReads.log("findContent reads");
  blobTotalUs.log("readBlob total");
  blobSdUs.log("readBlob SD time");
  blobDecodeUs.log("readBlob zstd decode");
  blobBytes.log("article HTML bytes");

  // Title-prefix search, as typing would do.
  uint32_t idx = 0;
  src.calls = 0;
  t0 = nowUs();
  zr->lowerBoundTitle('A', "Albert Ein", idx);
  for (uint32_t i = idx; i < idx + 5; ++i) zr->entryByTitleIndex(i, e);
  LOG_INF("ZSPK", "title search 'Albert Ein' + 5 entries: %lld us, %u reads", static_cast<long long>(nowUs() - t0),
          static_cast<unsigned>(src.calls));
}

}  // namespace

void zimSpikeRun() {
  LOG_INF("ZSPK", "=== ZIM spike start ===");
  if (Storage.exists(SKIP_MARKER)) {
    LOG_INF("ZSPK", "skip marker found, not running");
    return;
  }
  logHeap("boot");

  SdSource src;
  if (!src.open(ZIM_PATH)) {
    LOG_ERR("ZSPK", "cannot open %s", ZIM_PATH);
    return;
  }
  LOG_INF("ZSPK", "file %s size=%llu bytes", ZIM_PATH, static_cast<unsigned long long>(src.size()));

  constexpr size_t PACKED_BYTES = 1u << 20;
  constexpr size_t PLAIN_BYTES = 3u << 20;
  auto packed = HalMemory::allocatePsram(PACKED_BYTES);
  auto plain = HalMemory::allocatePsram(PLAIN_BYTES);
  if (!packed || !plain) {
    LOG_ERR("ZSPK", "PSRAM allocation failed (packed=%d plain=%d)", packed ? 1 : 0, plain ? 1 : 0);
    logHeap("after failed alloc");
    return;
  }
  logHeap("after 4MiB buffers");

  benchSequential(src, plain.get(), PLAIN_BYTES);
  benchRandom(src, plain.get());
  benchZim(src, packed.get(), PACKED_BYTES, plain.get(), PLAIN_BYTES);

  logHeap("end");
  LOG_INF("ZSPK", "=== ZIM spike done ===");
}

#else

void zimSpikeRun() {}

#endif
