#include "ZimDeviceTest.h"

#ifdef ZIM_DEVICE_TEST

#include <Arduino.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ZimBlobLoader.h>
#include <ZimDevice.h>
#include <ZimReader.h>
#include <ZstdClusterDecoder.h>
#include <esp_timer.h>

#include <string>

namespace {

constexpr const char* ZIM_PATH = "/zim/wikipedia_en_all_mini_2024-04.zim";
constexpr const char* SKIP_MARKER = "/zim/skip_test";
constexpr int ARTICLES = 12;

int64_t nowUs() { return esp_timer_get_time(); }

void logHeap(const char* when) {
  const auto internal = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  LOG_INF("ZDEV", "heap %s: internal free=%u largest=%u min=%u | psram free=%u largest=%u total=%u", when,
          static_cast<unsigned>(internal.freeBytes), static_cast<unsigned>(internal.largestBlockBytes),
          static_cast<unsigned>(internal.minFreeBytes), static_cast<unsigned>(psram.freeBytes),
          static_cast<unsigned>(psram.largestBlockBytes), static_cast<unsigned>(psram.totalBytes));
}

uint32_t xorshift(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

bool pickArticle(zim::ZimReader& zr, uint32_t& seed, zim::Entry& out) {
  for (int tries = 0; tries < 200; ++tries) {
    if (zr.entryByTitleIndex(xorshift(seed) % zr.entryCount(), out) != zim::Status::Ok) continue;
    if (out.ns == 'A' && out.kind == zim::EntryKind::Content && zr.mimeType(out.mimeIndex) == "text/html") return true;
  }
  return false;
}

void run() {
  zim::SdFileByteSource source;
  if (!source.open(ZIM_PATH)) {
    LOG_ERR("ZDEV", "cannot open %s", ZIM_PATH);
    return;
  }
  auto reader = makeUniqueNoThrow<zim::ZimReader>();
  auto decoder = makeUniqueNoThrow<zim::ZstdClusterDecoder>(zim::psramMemory());
  auto loader = makeUniqueNoThrow<zim::ZimBlobLoader>(zim::psramMemory());
  if (!reader || !decoder || !loader || !decoder->valid()) {
    LOG_ERR("ZDEV", "OOM: reader, decoder or loader");
    return;
  }
  int64_t t0 = nowUs();
  const zim::Status openStatus = reader->open(source);
  LOG_INF("ZDEV", "open st=%d %lld us entries=%u clusters=%u", static_cast<int>(openStatus),
          static_cast<long long>(nowUs() - t0), static_cast<unsigned>(reader->entryCount()),
          static_cast<unsigned>(reader->header().clusterCount));
  if (openStatus != zim::Status::Ok) return;
  logHeap("after open + decoder");

  zim::Entry entry;
  zim::Blob blob;
  uint32_t seed = 0xC0FFEEu;
  int ok = 0;
  int64_t sumUs = 0;
  for (int i = 0; i < ARTICLES; ++i) {
    zim::Entry pick;
    if (i == 0) {
      pick.url = "Albert_Einstein";
    } else if (!pickArticle(*reader, seed, pick)) {
      break;
    }
    const std::string url = pick.url;
    t0 = nowUs();
    zim::Status st = reader->findContent(url, entry);
    if (st == zim::Status::Ok) st = reader->resolveRedirects(entry);
    const int64_t findUs = nowUs() - t0;
    if (st != zim::Status::Ok) {
      LOG_ERR("ZDEV", "lookup failed %s st=%d", url.c_str(), static_cast<int>(st));
      continue;
    }
    t0 = nowUs();
    st = loader->read(*reader, decoder.get(), entry.clusterNumber, entry.blobNumber, blob);
    const int64_t readUs = nowUs() - t0;
    if (st != zim::Status::Ok) {
      LOG_ERR("ZDEV", "read failed %s st=%d", url.c_str(), static_cast<int>(st));
      continue;
    }
    ++ok;
    sumUs += readUs;
    LOG_INF("ZDEV", "%-34.34s find=%lld us read=%lld us blob=%u B plainCap=%u packedCap=%u", url.c_str(),
            static_cast<long long>(findUs), static_cast<long long>(readUs), static_cast<unsigned>(blob.size),
            static_cast<unsigned>(loader->plainCapacity()), static_cast<unsigned>(loader->packedCapacity()));
    delay(1);
  }
  if (ok > 0) LOG_INF("ZDEV", "read ok=%d/%d avg=%lld us", ok, ARTICLES, static_cast<long long>(sumUs / ok));
  logHeap("with buffers");

  loader->release();
  logHeap("after release");
}

}  // namespace

void zimDeviceTestRun() {
  LOG_INF("ZDEV", "=== ZIM device test start ===");
  if (Storage.exists(SKIP_MARKER)) {
    LOG_INF("ZDEV", "skip marker found, not running");
    return;
  }
  const bool supported = zim::deviceSupportsZim();
  LOG_INF("ZDEV", "deviceSupportsZim=%d psram total=%u", supported ? 1 : 0,
          static_cast<unsigned>(HalMemory::getPsramHeap().totalBytes));
  if (!supported) return;
  logHeap("boot");
  run();  // reader, decoder and loader are destroyed here, before the final heap line
  logHeap("end");
  LOG_INF("ZDEV", "=== ZIM device test done ===");
}

#else

void zimDeviceTestRun() {}

#endif
