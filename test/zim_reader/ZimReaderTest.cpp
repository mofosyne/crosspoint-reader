#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "ZimReader.h"

#ifdef ZIM_TEST_HAVE_ZSTD
#include <zstd.h>
#endif

using zim::Blob;
using zim::Compression;
using zim::Entry;
using zim::EntryKind;
using zim::Status;
using zim::Workspace;
using zim::ZimReader;

namespace {

using Bytes = std::vector<uint8_t>;

void put16(Bytes& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v));
  b.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(Bytes& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put64(Bytes& b, uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void putStr(Bytes& b, const std::string& s) {
  b.insert(b.end(), s.begin(), s.end());
  b.push_back(0);
}
void patch64(Bytes& b, size_t at, uint64_t v) {
  for (int i = 0; i < 8; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

class MemorySource : public freeink::content::ByteSource {
 public:
  explicit MemorySource(Bytes data) : data_(std::move(data)) {}
  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override {
    if (offset >= data_.size()) return 0;
    const size_t n = std::min<uint64_t>(len, data_.size() - offset);
    memcpy(dst, data_.data() + offset, n);
    return static_cast<int32_t>(n);
  }
  uint64_t size() const override { return data_.size(); }
  Bytes& bytes() { return data_; }

 private:
  Bytes data_;
};

class FileSource : public freeink::content::ByteSource {
 public:
  explicit FileSource(const char* path) : f_(fopen(path, "rb")) {
    if (f_ && fseeko(f_, 0, SEEK_END) == 0) size_ = static_cast<uint64_t>(ftello(f_));
  }
  ~FileSource() override {
    if (f_) fclose(f_);
  }
  bool ok() const { return f_ != nullptr; }
  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override {
    if (!f_ || fseeko(f_, static_cast<off_t>(offset), SEEK_SET) != 0) return -1;
    return static_cast<int32_t>(fread(dst, 1, len, f_));
  }
  uint64_t size() const override { return size_; }

 private:
  FILE* f_ = nullptr;
  uint64_t size_ = 0;
};

#ifdef ZIM_TEST_HAVE_ZSTD
class ZstdDecoder : public zim::ClusterDecoder {
 public:
  int64_t decode(Compression compression, const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) override {
    if (compression != Compression::Zstd) return zim::DECODE_ERROR;
    const size_t frameLen = ZSTD_findFrameCompressedSize(src, srcLen);
    if (ZSTD_isError(frameLen)) return zim::DECODE_ERROR;
    srcLen = frameLen;  // the last cluster's source runs past its frame
    const unsigned long long declared = ZSTD_getFrameContentSize(src, srcLen);
    if (declared != ZSTD_CONTENTSIZE_UNKNOWN && declared != ZSTD_CONTENTSIZE_ERROR && declared > dstCap) {
      return zim::DECODE_TOO_SMALL;
    }
    const size_t r = ZSTD_decompress(dst, dstCap, src, srcLen);
    return ZSTD_isError(r) ? zim::DECODE_ERROR : static_cast<int64_t>(r);
  }
};
#endif

struct TestEntry {
  char ns;
  std::string url;
  std::string title;
  bool redirect;
  uint16_t mime;
  uint32_t cluster;  // content: cluster; redirect: target URL index
  uint32_t blob;
};

const std::string APPLE = "<html>Apple</html>";
const std::string BANANA = "<html>Banana</html>";
const std::string CHERRY = "Cherry text";
const std::string TITLE_TEXT = "Title text";
const std::string ZEBRA = "Zebra extended";
const std::string LAST_TEXT = "<html>Last</html>";

Bytes makeCluster(const std::vector<std::string>& blobs, size_t offsetSize) {
  Bytes body;
  uint64_t pos = (blobs.size() + 1) * offsetSize;
  auto putOffset = [&](uint64_t v) {
    if (offsetSize == 8) {
      put64(body, v);
    } else {
      put32(body, static_cast<uint32_t>(v));
    }
  };
  putOffset(pos);
  for (const auto& blob : blobs) {
    pos += blob.size();
    putOffset(pos);
  }
  for (const auto& blob : blobs) body.insert(body.end(), blob.begin(), blob.end());
  return body;
}

// Info byte plus the body, zstd-compressed when available.
Bytes packCluster(const Bytes& body, bool useZstd) {
  Bytes c;
#ifdef ZIM_TEST_HAVE_ZSTD
  if (useZstd) {
    Bytes packed(ZSTD_compressBound(body.size()));
    const size_t n = ZSTD_compress(packed.data(), packed.size(), body.data(), body.size(), 3);
    packed.resize(n);
    c.push_back(5);
    c.insert(c.end(), packed.begin(), packed.end());
  }
#endif
  if (c.empty()) {
    c.push_back(1);
    c.insert(c.end(), body.begin(), body.end());
  }
  return c;
}

// Laid out like real archives: clusters first, then the entries, then the pointer tables.
// Redirect "Berry" (index 2) points at `berryTarget`.
Bytes buildZim(bool useZstd, uint32_t berryTarget = 1, bool reverseDirents = false) {
  const std::vector<TestEntry> entries = {
      {'A', "Apple", "Apple fruit", false, 0, 0, 0}, {'A', "Banana", "", false, 0, 0, 1},
      {'A', "Berry", "Berry", true, 0, berryTarget, 0}, {'A', "Cherry", "Aardvark", false, 0, 1, 0},
      {'A', "Zebra", "Zebra", false, 1, 2, 0},       {'M', "Title", "", false, 1, 1, 1},
  };
  const std::vector<uint32_t> titleOrder = {3, 0, 1, 2, 4, 5};

  Bytes mime;
  putStr(mime, "text/html");
  putStr(mime, "text/plain");
  mime.push_back(0);

  Bytes dirents;
  std::vector<uint64_t> direntPos;  // relative to the dirent block
  for (const auto& e : entries) {
    direntPos.push_back(dirents.size());
    if (e.redirect) {
      put16(dirents, 0xFFFF);
      dirents.push_back(0);
      dirents.push_back(static_cast<uint8_t>(e.ns));
      put32(dirents, 0);
      put32(dirents, e.cluster);
    } else {
      put16(dirents, e.mime);
      dirents.push_back(0);
      dirents.push_back(static_cast<uint8_t>(e.ns));
      put32(dirents, 0);
      put32(dirents, e.cluster);
      put32(dirents, e.blob);
    }
    putStr(dirents, e.url);
    putStr(dirents, e.title);
  }
  if (reverseDirents) {
    Bytes flipped;
    std::vector<uint64_t> flippedPos(entries.size());
    for (size_t i = entries.size(); i-- > 0;) {
      const uint64_t end = i + 1 < entries.size() ? direntPos[i + 1] : dirents.size();
      flippedPos[i] = flipped.size();
      flipped.insert(flipped.end(), dirents.begin() + direntPos[i], dirents.begin() + end);
    }
    dirents = flipped;
    direntPos = flippedPos;
  }

  std::vector<Bytes> clusters;
  clusters.push_back(packCluster(makeCluster({APPLE, BANANA}, 4), useZstd));
  {
    Bytes body = makeCluster({CHERRY, TITLE_TEXT}, 4);
    Bytes c = {1};
    c.insert(c.end(), body.begin(), body.end());
    clusters.push_back(c);
  }
  {
    Bytes body = makeCluster({ZEBRA}, 8);
    Bytes c = {0x11};
    c.insert(c.end(), body.begin(), body.end());
    clusters.push_back(c);
  }
  clusters.push_back(packCluster(makeCluster({LAST_TEXT}, 4), useZstd));

  const uint64_t n = entries.size();
  const uint64_t mimePos = 80;
  std::vector<uint64_t> clusterPos;
  uint64_t clustersEnd = mimePos + mime.size();
  for (const auto& c : clusters) {
    clusterPos.push_back(clustersEnd);
    clustersEnd += c.size();
  }
  const uint64_t direntBase = clustersEnd;
  const uint64_t urlPtrPos = direntBase + dirents.size();
  const uint64_t titlePtrPos = urlPtrPos + 8 * n;
  const uint64_t clusterPtrPos = titlePtrPos + 4 * n;
  const uint64_t checksumPos = clusterPtrPos + 8 * clusters.size();

  Bytes f;
  put32(f, 0x44D495A);
  put16(f, 6);
  put16(f, 0);
  f.insert(f.end(), 16, 0xAB);  // uuid
  put32(f, static_cast<uint32_t>(n));
  put32(f, static_cast<uint32_t>(clusters.size()));
  put64(f, urlPtrPos);
  put64(f, titlePtrPos);
  put64(f, clusterPtrPos);
  put64(f, mimePos);
  put32(f, 0);           // main page: Apple
  put32(f, 0xFFFFFFFF);  // layout page
  put64(f, checksumPos);
  f.insert(f.end(), mime.begin(), mime.end());
  for (const auto& c : clusters) f.insert(f.end(), c.begin(), c.end());
  f.insert(f.end(), dirents.begin(), dirents.end());
  for (const uint64_t p : direntPos) put64(f, direntBase + p);
  for (const uint32_t t : titleOrder) put32(f, t);
  for (const uint64_t p : clusterPos) put64(f, p);
  f.insert(f.end(), 16, 0xCD);  // checksum
  return f;
}

struct Buffers {
  std::vector<uint8_t> packed = std::vector<uint8_t>(4096);
  std::vector<uint8_t> plain = std::vector<uint8_t>(4096);
  Workspace ws() {
    Workspace w;
    w.packed = packed.data();
    w.packedCap = packed.size();
    w.plain = plain.data();
    w.plainCap = plain.size();
    return w;
  }
};

std::string text(const Blob& b) { return std::string(reinterpret_cast<const char*>(b.data), b.size); }

#ifdef ZIM_TEST_HAVE_ZSTD
constexpr bool HAVE_ZSTD = true;
#else
constexpr bool HAVE_ZSTD = false;
#endif

}  // namespace

TEST(ZimReader, ReadsHeaderAndMimeTypes) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  EXPECT_EQ(zr.header().majorVersion, 6);
  EXPECT_EQ(zr.entryCount(), 6u);
  EXPECT_EQ(zr.header().clusterCount, 4u);
  EXPECT_EQ(zr.header().mainPage, 0u);
  EXPECT_EQ(zr.mimeType(0), "text/html");
  EXPECT_EQ(zr.mimeType(1), "text/plain");
  EXPECT_EQ(zr.mimeType(2), "");
}

TEST(ZimReader, FindsEveryEntryByPath) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  const struct {
    char ns;
    const char* url;
    uint32_t index;
  } cases[] = {{'A', "Apple", 0}, {'A', "Banana", 1}, {'A', "Berry", 2},
               {'A', "Cherry", 3}, {'A', "Zebra", 4}, {'M', "Title", 5}};
  for (const auto& c : cases) {
    Entry e;
    ASSERT_EQ(zr.findByPath(c.ns, c.url, e), Status::Ok) << c.url;
    EXPECT_EQ(e.index, c.index) << c.url;
    EXPECT_EQ(e.url, c.url);
  }
  Entry e;
  EXPECT_EQ(zr.findByPath('A', "Missing", e), Status::NotFound);
  EXPECT_EQ(zr.findByPath('C', "Apple", e), Status::NotFound);
  EXPECT_EQ(zr.findByPath('A', "", e), Status::NotFound);
}

TEST(ZimReader, ContentEntryCarriesClusterAndTitle) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  Entry e;
  ASSERT_EQ(zr.entryByUrlIndex(3, e), Status::Ok);
  EXPECT_EQ(e.kind, EntryKind::Content);
  EXPECT_EQ(e.clusterNumber, 1u);
  EXPECT_EQ(e.blobNumber, 0u);
  EXPECT_EQ(e.title, "Aardvark");
  ASSERT_EQ(zr.entryByUrlIndex(1, e), Status::Ok);
  EXPECT_EQ(e.title, "Banana");  // an empty stored title falls back to the URL
  EXPECT_EQ(zr.entryByUrlIndex(6, e), Status::NotFound);
}

TEST(ZimReader, FindContentFallsBackToTheOldNamespace) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  Entry e;
  ASSERT_EQ(zr.findContent("Cherry", e), Status::Ok);
  EXPECT_EQ(e.ns, 'A');
  EXPECT_EQ(zr.findContent("Title", e), Status::NotFound);  // namespace 'M' is not content
}

TEST(ZimReader, ResolvesRedirects) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  Entry e;
  ASSERT_EQ(zr.findContent("Berry", e), Status::Ok);
  EXPECT_EQ(e.kind, EntryKind::Redirect);
  ASSERT_EQ(zr.resolveRedirects(e), Status::Ok);
  EXPECT_EQ(e.kind, EntryKind::Content);
  EXPECT_EQ(e.url, "Banana");
  EXPECT_EQ(e.index, 1u);
}

TEST(ZimReader, RedirectLoopIsReportedAsCorrupt) {
  MemorySource src(buildZim(HAVE_ZSTD, 2));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  Entry e;
  ASSERT_EQ(zr.findContent("Berry", e), Status::Ok);
  EXPECT_EQ(zr.resolveRedirects(e), Status::Corrupt);
}

TEST(ZimReader, TitleSearchUsesTitleOrder) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  uint32_t idx = 99;
  Entry e;
  ASSERT_EQ(zr.lowerBoundTitle('A', "Ap", idx), Status::Ok);
  EXPECT_EQ(idx, 1u);
  ASSERT_EQ(zr.entryByTitleIndex(idx, e), Status::Ok);
  EXPECT_EQ(e.url, "Apple");
  ASSERT_EQ(zr.lowerBoundTitle('A', "", idx), Status::Ok);
  EXPECT_EQ(idx, 0u);
  ASSERT_EQ(zr.entryByTitleIndex(idx, e), Status::Ok);
  EXPECT_EQ(e.title, "Aardvark");
  ASSERT_EQ(zr.lowerBoundTitle('A', "Zz", idx), Status::Ok);
  EXPECT_EQ(idx, 5u);  // first entry of the next namespace
  ASSERT_EQ(zr.lowerBoundTitle('Z', "", idx), Status::Ok);
  EXPECT_EQ(idx, 6u);
  EXPECT_EQ(zr.entryByTitleIndex(6, e), Status::NotFound);
}

TEST(ZimReader, ReadsBlobsFromEveryClusterKind) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  Buffers buf;
  Blob b;
#ifdef ZIM_TEST_HAVE_ZSTD
  ZstdDecoder decoder;
  zim::ClusterDecoder* dec = &decoder;
#else
  zim::ClusterDecoder* dec = nullptr;
#endif
  ASSERT_EQ(zr.readBlob(0, 0, dec, buf.ws(), b), Status::Ok);
  EXPECT_EQ(text(b), APPLE);
  ASSERT_EQ(zr.readBlob(0, 1, dec, buf.ws(), b), Status::Ok);
  EXPECT_EQ(text(b), BANANA);
  ASSERT_EQ(zr.readBlob(1, 0, nullptr, buf.ws(), b), Status::Ok);
  EXPECT_EQ(text(b), CHERRY);
  ASSERT_EQ(zr.readBlob(1, 1, nullptr, buf.ws(), b), Status::Ok);
  EXPECT_EQ(text(b), TITLE_TEXT);
  ASSERT_EQ(zr.readBlob(2, 0, nullptr, buf.ws(), b), Status::Ok);
  EXPECT_EQ(text(b), ZEBRA);
  ASSERT_EQ(zr.readBlob(3, 0, dec, buf.ws(), b), Status::Ok);
  EXPECT_EQ(text(b), LAST_TEXT);
}

TEST(ZimReader, ClusterInfoDescribesEachCluster) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  zim::ClusterInfo ci;
  ASSERT_EQ(zr.clusterInfo(0, ci), Status::Ok);
  EXPECT_EQ(ci.compression, HAVE_ZSTD ? Compression::Zstd : Compression::None);
  ASSERT_EQ(zr.clusterInfo(2, ci), Status::Ok);
  EXPECT_EQ(ci.compression, Compression::None);
  EXPECT_TRUE(ci.extended);
  EXPECT_EQ(zr.clusterInfo(4, ci), Status::NotFound);
}

TEST(ZimReader, OutOfRangeBlobAndClusterAreNotFound) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  Buffers buf;
  Blob b;
  EXPECT_EQ(zr.readBlob(1, 2, nullptr, buf.ws(), b), Status::NotFound);
  EXPECT_EQ(zr.readBlob(2, 1, nullptr, buf.ws(), b), Status::NotFound);
  EXPECT_EQ(zr.readBlob(4, 0, nullptr, buf.ws(), b), Status::NotFound);
}

TEST(ZimReader, SmallPlainBufferReportsTheBlobSize) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  Buffers buf;
  Workspace w = buf.ws();
  w.plainCap = 4;
  Blob b;
  EXPECT_EQ(zr.readBlob(1, 0, nullptr, w, b), Status::BufferTooSmall);
  EXPECT_EQ(b.size, CHERRY.size());
  EXPECT_EQ(b.data, nullptr);
}

#ifdef ZIM_TEST_HAVE_ZSTD
TEST(ZimReader, ZstdClusterBufferLimits) {
  MemorySource src(buildZim(true));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  ZstdDecoder decoder;
  Buffers buf;
  Blob b;
  Workspace w = buf.ws();
  w.plainCap = 8;
  EXPECT_EQ(zr.readBlob(0, 0, &decoder, w, b), Status::BufferTooSmall);
  w = buf.ws();
  w.packedCap = 4;
  EXPECT_EQ(zr.readBlob(0, 0, &decoder, w, b), Status::BufferTooSmall);
  EXPECT_EQ(zr.readBlob(0, 0, nullptr, buf.ws(), b), Status::Unsupported);
}

TEST(ZimReader, CorruptZstdClusterIsReported) {
  MemorySource src(buildZim(true));
  zim::ClusterInfo ci;
  {
    ZimReader zr;
    ASSERT_EQ(zr.open(src), Status::Ok);
    ASSERT_EQ(zr.clusterInfo(0, ci), Status::Ok);
  }
  for (uint64_t i = 1; i < ci.packedSize && i < 12; ++i) src.bytes()[ci.offset + i] ^= 0xFF;
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  ZstdDecoder decoder;
  Buffers buf;
  Blob b;
  EXPECT_EQ(zr.readBlob(0, 0, &decoder, buf.ws(), b), Status::Corrupt);
}
#endif

TEST(ZimReader, RejectsBadFiles) {
  {
    Bytes f = buildZim(HAVE_ZSTD);
    f[0] ^= 0xFF;
    MemorySource src(f);
    ZimReader zr;
    EXPECT_EQ(zr.open(src), Status::Corrupt);
  }
  {
    Bytes f = buildZim(HAVE_ZSTD);
    f[4] = 7;  // major version
    MemorySource src(f);
    ZimReader zr;
    EXPECT_EQ(zr.open(src), Status::Unsupported);
  }
  {
    Bytes f = buildZim(HAVE_ZSTD);
    f.resize(120);  // the tables run past the end
    MemorySource src(f);
    ZimReader zr;
    EXPECT_EQ(zr.open(src), Status::Corrupt);
  }
  {
    MemorySource src(Bytes(40, 0));
    ZimReader zr;
    EXPECT_EQ(zr.open(src), Status::IoError);
  }
}

TEST(ZimReader, DirentPointerPastTheFileIsCorrupt) {
  Bytes f = buildZim(HAVE_ZSTD);
  MemorySource probe(f);
  ZimReader zr0;
  ASSERT_EQ(zr0.open(probe), Status::Ok);
  const uint64_t urlPtrPos = zr0.header().urlPtrPos;
  patch64(f, urlPtrPos + 8 * 2, f.size() + 100);
  MemorySource src(f);
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  Entry e;
  EXPECT_EQ(zr.entryByUrlIndex(2, e), Status::Corrupt);
}

// Runs against a real archive: ZIM_TEST_FILE=/path/to/file.zim
TEST(ZimRealFile, MainPageAndTitleSearch) {
  const char* path = getenv("ZIM_TEST_FILE");
  if (!path) GTEST_SKIP() << "set ZIM_TEST_FILE to a .zim path to run this test";
  FileSource src(path);
  ASSERT_TRUE(src.ok()) << path;
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  printf("version %u.%u, %u entries, %u clusters\n", zr.header().majorVersion, zr.header().minorVersion,
         zr.entryCount(), zr.header().clusterCount);

  bool hasHtml = false;
  for (uint16_t i = 0; i < 64 && !zr.mimeType(i).empty(); ++i) hasHtml |= zr.mimeType(i) == "text/html";
  EXPECT_TRUE(hasHtml);

  if (zr.header().mainPage == 0xFFFFFFFF) GTEST_SKIP() << "archive has no main page";
  Entry main;
  ASSERT_EQ(zr.entryByUrlIndex(zr.header().mainPage, main), Status::Ok);
  ASSERT_EQ(zr.resolveRedirects(main), Status::Ok) << main.url;
  printf("main page: ns=%c url=%s title=%s cluster=%u blob=%u\n", main.ns, main.url.c_str(), main.title.c_str(),
         main.clusterNumber, main.blobNumber);

  Entry again;
  ASSERT_EQ(zr.findByPath(main.ns, main.url, again), Status::Ok) << "binary search must find the main page";
  EXPECT_EQ(again.index, main.index);

  zim::ClusterInfo ci;
  ASSERT_EQ(zr.clusterInfo(main.clusterNumber, ci), Status::Ok);
#ifdef ZIM_TEST_HAVE_ZSTD
  ZstdDecoder decoder;
  zim::ClusterDecoder* dec = &decoder;
#else
  zim::ClusterDecoder* dec = nullptr;
  if (ci.compression == Compression::Zstd) GTEST_SKIP() << "built without libzstd";
#endif
  if (ci.compression == Compression::Xz) GTEST_SKIP() << "cluster uses xz, which has no test decoder";

  std::vector<uint8_t> packed(16u << 20);
  std::vector<uint8_t> plain(16u << 20);
  Workspace w;
  w.packed = packed.data();
  w.packedCap = packed.size();
  w.plain = plain.data();
  w.plainCap = plain.size();
  Blob b;
  ASSERT_EQ(zr.readBlob(main.clusterNumber, main.blobNumber, dec, w, b), Status::Ok);
  EXPECT_GT(b.size, 0u);
  const std::string head(reinterpret_cast<const char*>(b.data), std::min<size_t>(b.size, 200));
  printf("main page blob: %zu bytes, mime %s, starts: %.80s\n", b.size, std::string(zr.mimeType(main.mimeIndex)).c_str(),
         head.c_str());

  const std::string prefix = main.title.substr(0, std::min<size_t>(main.title.size(), 3));
  uint32_t idx = 0;
  ASSERT_EQ(zr.lowerBoundTitle(main.ns, prefix, idx), Status::Ok);
  Entry hit;
  ASSERT_EQ(zr.entryByTitleIndex(idx, hit), Status::Ok) << "title list lookup";
  EXPECT_EQ(hit.title.compare(0, prefix.size(), prefix), 0) << hit.title << " does not start with " << prefix;
}

// Looks up one article and lists the titles that follow a prefix.
// Needs ZIM_TEST_FILE; ZIM_TEST_PATH and ZIM_TEST_PREFIX are optional.
TEST(ZimRealFile, ArticleAndTitlePrefix) {
  const char* file = getenv("ZIM_TEST_FILE");
  if (!file) GTEST_SKIP() << "set ZIM_TEST_FILE to a .zim path to run this test";
  const char* pathEnv = getenv("ZIM_TEST_PATH");
  const char* prefixEnv = getenv("ZIM_TEST_PREFIX");
  const std::string path = pathEnv ? pathEnv : "Albert_Einstein";
  const std::string prefix = prefixEnv ? prefixEnv : "Albert Ein";
  FileSource src(file);
  ASSERT_TRUE(src.ok()) << file;
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);

  Entry e;
  const Status found = zr.findContent(path, e);
  if (found == Status::NotFound) GTEST_SKIP() << path << " is not in this archive";
  ASSERT_EQ(found, Status::Ok);
  const bool wasRedirect = e.kind == EntryKind::Redirect;
  ASSERT_EQ(zr.resolveRedirects(e), Status::Ok);

  zim::ClusterInfo ci;
  ASSERT_EQ(zr.clusterInfo(e.clusterNumber, ci), Status::Ok);
#ifdef ZIM_TEST_HAVE_ZSTD
  ZstdDecoder decoder;
  zim::ClusterDecoder* dec = &decoder;
#else
  zim::ClusterDecoder* dec = nullptr;
  if (ci.compression == Compression::Zstd) GTEST_SKIP() << "built without libzstd";
#endif
  if (ci.compression == Compression::Xz) GTEST_SKIP() << "cluster uses xz, which has no test decoder";

  std::vector<uint8_t> packed(16u << 20);
  std::vector<uint8_t> plain(16u << 20);
  Workspace w;
  w.packed = packed.data();
  w.packedCap = packed.size();
  w.plain = plain.data();
  w.plainCap = plain.size();
  Blob b;
  ASSERT_EQ(zr.readBlob(e.clusterNumber, e.blobNumber, dec, w, b), Status::Ok);
  EXPECT_GT(b.size, 0u);
  printf("%s%s -> ns=%c url=%s cluster=%u (%s, %llu bytes packed) blob=%zu bytes mime=%s\n", path.c_str(),
         wasRedirect ? " (redirect)" : "", e.ns, e.url.c_str(), e.clusterNumber,
         ci.compression == Compression::Zstd ? "zstd" : "stored", static_cast<unsigned long long>(ci.packedSize),
         b.size, std::string(zr.mimeType(e.mimeIndex)).c_str());

  uint32_t idx = 0;
  ASSERT_EQ(zr.lowerBoundTitle(e.ns, prefix, idx), Status::Ok);
  for (uint32_t i = idx; i < idx + 5 && i < zr.entryCount(); ++i) {
    Entry t;
    ASSERT_EQ(zr.entryByTitleIndex(i, t), Status::Ok);
    printf("  title[%u] \"%s\" -> %s\n", i, t.title.c_str(), t.url.c_str());
  }
}

namespace {

// Counts the reads a lookup issues. `sectors` counts distinct 512-byte sectors, which
// approximates SD transactions when the card layer caches the last sector.
class CountingSource : public freeink::content::ByteSource {
 public:
  explicit CountingSource(freeink::content::ByteSource& inner) : inner_(inner) {}
  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override {
    ++calls;
    bytes += len;
    for (uint64_t s = offset / 512; len > 0 && s <= (offset + len - 1) / 512; ++s) sectors.insert(s);
    return inner_.readAt(offset, dst, len);
  }
  uint64_t size() const override { return inner_.size(); }
  void reset() {
    calls = 0;
    bytes = 0;
    sectors.clear();
  }
  uint32_t calls = 0;
  uint64_t bytes = 0;
  std::set<uint64_t> sectors;

 private:
  freeink::content::ByteSource& inner_;
};

}  // namespace

// Prints how many SD reads each operation needs. Needs ZIM_TEST_FILE.
TEST(ZimRealFile, ReadCostPerOperation) {
  const char* file = getenv("ZIM_TEST_FILE");
  if (!file) GTEST_SKIP() << "set ZIM_TEST_FILE to a .zim path to run this test";
  FileSource real(file);
  ASSERT_TRUE(real.ok()) << file;
  CountingSource src(real);
  auto report = [&](const char* what) {
    printf("  %-34s %3u reads %4zu sectors %9llu bytes\n", what, src.calls, src.sectors.size(),
           static_cast<unsigned long long>(src.bytes));
    src.reset();
  };
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  report("open");

  const char* paths[] = {"Albert_Einstein", "Paris", "Moon", "Python_(programming_language)", "Zebra"};
  Entry e;
  for (const char* p : paths) {
    if (zr.findContent(p, e) != Status::Ok) continue;
    char label[64];
    snprintf(label, sizeof(label), "findContent %.20s", p);
    report(label);
  }

  if (zr.findContent("Albert_Einstein", e) != Status::Ok) GTEST_SKIP() << "sample article is not in this archive";
  src.reset();
  uint32_t idx = 0;
  ASSERT_EQ(zr.lowerBoundTitle('A', "Albert Ein", idx), Status::Ok);
  report("lowerBoundTitle 'Albert Ein'");
  for (uint32_t i = idx; i < idx + 5; ++i) ASSERT_EQ(zr.entryByTitleIndex(i, e), Status::Ok);
  report("list 5 matching titles");

  if (zr.findContent("Albert_Einstein", e) != Status::Ok) GTEST_SKIP() << "sample article is not in this archive";
  src.reset();
  zim::ClusterInfo ci;
  ASSERT_EQ(zr.clusterInfo(e.clusterNumber, ci), Status::Ok);
#ifdef ZIM_TEST_HAVE_ZSTD
  ZstdDecoder decoder;
  zim::ClusterDecoder* dec = &decoder;
#else
  zim::ClusterDecoder* dec = nullptr;
  if (ci.compression == Compression::Zstd) GTEST_SKIP() << "built without libzstd";
#endif
  if (ci.compression == Compression::Xz) GTEST_SKIP() << "cluster uses xz";
  std::vector<uint8_t> packed(16u << 20);
  std::vector<uint8_t> plain(16u << 20);
  Workspace w;
  w.packed = packed.data();
  w.packedCap = packed.size();
  w.plain = plain.data();
  w.plainCap = plain.size();
  src.reset();
  Blob b;
  ASSERT_EQ(zr.readBlob(e.clusterNumber, e.blobNumber, dec, w, b), Status::Ok);
  report("readBlob (cluster read)");
}

TEST(ZimReader, LookupsWorkWhenEntriesAreNotStoredInUrlOrder) {
  MemorySource src(buildZim(HAVE_ZSTD, 1, true));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  const char* urls[] = {"Apple", "Banana", "Berry", "Cherry", "Zebra"};
  for (uint32_t i = 0; i < 5; ++i) {
    Entry e;
    ASSERT_EQ(zr.findByPath('A', urls[i], e), Status::Ok) << urls[i];
    EXPECT_EQ(e.index, i);
    EXPECT_EQ(e.url, urls[i]);
  }
  Entry e;
  ASSERT_EQ(zr.findByPath('M', "Title", e), Status::Ok);
  EXPECT_EQ(e.index, 5u);
}

TEST(ZimReader, LastClusterEndsAtTheNextTable) {
  MemorySource src(buildZim(HAVE_ZSTD));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  zim::ClusterInfo ci;
  ASSERT_EQ(zr.clusterInfo(3, ci), Status::Ok);
  EXPECT_FALSE(ci.exactExtent);
  // The entries sit between the last cluster and the tables, so this bound is generous.
  EXPECT_EQ(ci.offset + ci.packedSize, zr.header().urlPtrPos);
  ASSERT_EQ(zr.clusterInfo(2, ci), Status::Ok);
  EXPECT_TRUE(ci.exactExtent);
}

#ifdef ZIM_TEST_HAVE_ZSTD
TEST(ZimReader, LastClusterReadsThroughABufferShorterThanItsBound) {
  MemorySource src(buildZim(true));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  zim::ClusterInfo ci;
  ASSERT_EQ(zr.clusterInfo(3, ci), Status::Ok);
  const size_t frame = ZSTD_findFrameCompressedSize(&src.bytes()[ci.offset + 1], ci.packedSize - 1);
  ASSERT_FALSE(ZSTD_isError(frame));
  ASSERT_LT(frame, ci.packedSize - 1);
  ZstdDecoder decoder;
  Buffers buf;
  Workspace w = buf.ws();
  w.packedCap = frame;
  Blob b;
  ASSERT_EQ(zr.readBlob(3, 0, &decoder, w, b), Status::Ok);
  EXPECT_EQ(text(b), LAST_TEXT);
}

class OversizedDecoder : public zim::ClusterDecoder {
 public:
  int64_t decode(Compression, const uint8_t*, size_t, uint8_t*, size_t dstCap) override {
    return static_cast<int64_t>(dstCap + 1);
  }
};

TEST(ZimReader, DecoderReportingMoreThanTheBufferIsCorrupt) {
  MemorySource src(buildZim(true));
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  OversizedDecoder decoder;
  Buffers buf;
  Blob b;
  EXPECT_EQ(zr.readBlob(0, 0, &decoder, buf.ws(), b), Status::Corrupt);
}
#endif

TEST(ZimReader, FailedOpenLeavesTheReaderUnusable) {
  MemorySource good(buildZim(HAVE_ZSTD));
  Bytes badBytes = buildZim(HAVE_ZSTD);
  badBytes[0] ^= 0xFF;
  MemorySource bad(badBytes);
  ZimReader zr;
  ASSERT_EQ(zr.open(good), Status::Ok);
  ASSERT_NE(zr.open(bad), Status::Ok);
  Buffers buf;
  Blob b;
  Entry e;
  EXPECT_EQ(zr.readBlob(1, 0, nullptr, buf.ws(), b), Status::NotFound);
  EXPECT_EQ(zr.entryByUrlIndex(0, e), Status::NotFound);
  EXPECT_EQ(zr.entryCount(), 0u);
}

TEST(ZimReader, OutOfRangeReferencesAreCorrupt) {
  {
    MemorySource src(buildZim(HAVE_ZSTD, 99));  // Berry redirects to entry 99 of 6
    ZimReader zr;
    ASSERT_EQ(zr.open(src), Status::Ok);
    Entry e;
    ASSERT_EQ(zr.findContent("Berry", e), Status::Ok);
    EXPECT_EQ(zr.resolveRedirects(e), Status::Corrupt);
  }
  {
    Bytes f = buildZim(HAVE_ZSTD);
    MemorySource probe(f);
    ZimReader zr0;
    ASSERT_EQ(zr0.open(probe), Status::Ok);
    const size_t at = static_cast<size_t>(zr0.header().titlePtrPos) + 4 * 3;  // first title probe
    f[at] = 99;
    MemorySource src(f);
    ZimReader zr;
    ASSERT_EQ(zr.open(src), Status::Ok);
    uint32_t idx = 0;
    Entry e;
    EXPECT_EQ(zr.lowerBoundTitle('A', "x", idx), Status::Corrupt);
    EXPECT_EQ(zr.entryByTitleIndex(3, e), Status::Corrupt);
  }
}

TEST(ZimReader, MimeListWithTooManyTypesIsCorrupt) {
  Bytes f;
  put32(f, 0x44D495A);
  put16(f, 6);
  put16(f, 0);
  f.insert(f.end(), 16, 0);
  put32(f, 0);  // entries
  put32(f, 0);  // clusters
  put64(f, 0);
  put64(f, 0);
  put64(f, 0);
  put64(f, 80);  // MIME list
  put32(f, 0xFFFFFFFF);
  put32(f, 0xFFFFFFFF);
  put64(f, 0);
  for (int i = 0; i < 300; ++i) putStr(f, "t");
  f.push_back(0);
  patch64(f, 72, f.size());
  MemorySource src(f);
  ZimReader zr;
  EXPECT_EQ(zr.open(src), Status::Corrupt);
}

TEST(ZimReader, FindContentRemembersWhichNamespaceHoldsContent) {
  MemorySource inner(buildZim(HAVE_ZSTD));
  CountingSource src(inner);
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  Entry e;
  src.reset();
  ASSERT_EQ(zr.findContent("Zebra", e), Status::Ok);  // searches 'C' first, then 'A'
  const uint32_t first = src.calls;
  src.reset();
  ASSERT_EQ(zr.findContent("Zebra", e), Status::Ok);
  EXPECT_LT(src.calls, first);
  EXPECT_EQ(zr.findContent("Missing", e), Status::NotFound);
}

// Real archives keep the entries and tables after the last cluster.
TEST(ZimRealFile, LastClusterExtentStopsAtTheNextTable) {
  const char* file = getenv("ZIM_TEST_FILE");
  if (!file) GTEST_SKIP() << "set ZIM_TEST_FILE to a .zim path to run this test";
  FileSource src(file);
  ASSERT_TRUE(src.ok()) << file;
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  zim::ClusterInfo ci;
  ASSERT_EQ(zr.clusterInfo(zr.header().clusterCount - 1, ci), Status::Ok);
  const zim::Header& h = zr.header();
  EXPECT_FALSE(ci.exactExtent);
  EXPECT_LE(ci.offset + ci.packedSize, std::min({h.urlPtrPos, h.titlePtrPos, h.clusterPtrPos, h.checksumPos}) + 0u)
      << "the extent must not run past the first table that follows the cluster";
  printf("last cluster: %s, extended=%d, up to %llu bytes\n",
         ci.compression == Compression::None ? "stored" : (ci.compression == Compression::Zstd ? "zstd" : "other"),
         ci.extended, static_cast<unsigned long long>(ci.packedSize));
}

// The URL-ordered and title-ordered lists are searched independently, so an article
// that one lists must be found by the other.
TEST(ZimRealFile, PathAndTitleSearchAgree) {
  const char* file = getenv("ZIM_TEST_FILE");
  if (!file) GTEST_SKIP() << "set ZIM_TEST_FILE to a .zim path to run this test";
  FileSource src(file);
  ASSERT_TRUE(src.ok()) << file;
  ZimReader zr;
  ASSERT_EQ(zr.open(src), Status::Ok);
  const char* paths[] = {"Albert_Einstein", "Paris", "Moon", "Zebra", "Python_(programming_language)", "Main_Page",
                         "index"};
  int checked = 0;
  for (const char* p : paths) {
    std::string title = p;
    for (char& c : title) {
      if (c == '_') c = ' ';
    }
    for (const char ns : {'A', 'C'}) {
      uint32_t idx = 0;
      ASSERT_EQ(zr.lowerBoundTitle(ns, title, idx), Status::Ok);
      Entry t;
      bool listed = false;
      for (uint32_t i = idx; i < idx + 8 && i < zr.entryCount() && !listed; ++i) {
        if (zr.entryByTitleIndex(i, t) != Status::Ok || t.ns != ns || t.title != title) break;
        listed = t.url == p;
      }
      Entry e;
      const Status byPath = zr.findByPath(ns, p, e);
      printf("  %c %-32s title list: %-3s path search: %s\n", ns, p, listed ? "yes" : "no",
             byPath == Status::Ok ? "found" : "missing");
      if (listed) {
        EXPECT_EQ(byPath, Status::Ok) << p << " is in the title list but the path search misses it";
        ++checked;
      }
    }
  }
  if (checked == 0) GTEST_SKIP() << "none of the sample articles are in this archive";
}
