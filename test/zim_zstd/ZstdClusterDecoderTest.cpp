#include <gtest/gtest.h>
#include <zstd.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "ZstdClusterDecoder.h"
#include "ZstdFixtures.h"

using zim::Compression;
using zim::DECODE_ERROR;
using zim::DECODE_TOO_SMALL;
using zim::ZimMemory;
using zim::ZstdClusterDecoder;

namespace {

// Must match fixtureText() in gen_fixtures.c.
std::vector<uint8_t> fixtureText(size_t n) {
  static const char* words[] = {"apple", "tree", "river", "stone", "light", "the", "of", "and", "paris", "moon"};
  std::vector<uint8_t> out(n);
  uint32_t s = 12345;
  size_t pos = 0;
  while (pos < n) {
    s = s * 1103515245u + 12345u;
    for (const char* w = words[(s >> 16) % 10]; *w && pos < n; ++w) out[pos++] = static_cast<uint8_t>(*w);
    if (pos < n) out[pos++] = ' ';
  }
  return out;
}

// Counts live allocations so leaks and double frees show up.
struct CountingMemory {
  static inline int live = 0;
  static inline int allocations = 0;
  static inline bool failNext = false;
  static void* allocate(size_t bytes) {
    if (failNext) return nullptr;
    ++live;
    ++allocations;
    return malloc(bytes);
  }
  static void release(void* ptr) {
    if (!ptr) return;
    --live;
    free(ptr);
  }
  static ZimMemory memory() { return {allocate, release}; }
  static void reset() { live = allocations = 0, failNext = false; }
};

std::vector<uint8_t> streamed() { return {std::begin(STREAMED_FRAME), std::end(STREAMED_FRAME)}; }
std::vector<uint8_t> known() { return {std::begin(KNOWN_FRAME), std::end(KNOWN_FRAME)}; }

}  // namespace

class ZstdClusterDecoderTest : public ::testing::Test {
 protected:
  void SetUp() override { CountingMemory::reset(); }
  void TearDown() override { EXPECT_EQ(CountingMemory::live, 0) << "leaked allocation"; }
};

TEST_F(ZstdClusterDecoderTest, FixturesHaveTheExpectedHeaders) {
  // Kiwix frames carry no content size; the streamed fixture must behave the same way.
  EXPECT_EQ(ZSTD_getFrameContentSize(STREAMED_FRAME, sizeof(STREAMED_FRAME)), ZSTD_CONTENTSIZE_UNKNOWN);
  EXPECT_EQ(ZSTD_getFrameContentSize(KNOWN_FRAME, sizeof(KNOWN_FRAME)), KNOWN_PLAIN_SIZE);
}

TEST_F(ZstdClusterDecoderTest, DecodesAFrameWithoutDeclaredSize) {
  ZstdClusterDecoder decoder(CountingMemory::memory());
  ASSERT_TRUE(decoder.valid());
  const auto frame = streamed();
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE + 100);
  const int64_t n = decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), dst.size());
  ASSERT_EQ(n, static_cast<int64_t>(STREAMED_PLAIN_SIZE));
  EXPECT_EQ(0, memcmp(dst.data(), fixtureText(STREAMED_PLAIN_SIZE).data(), STREAMED_PLAIN_SIZE));
}

TEST_F(ZstdClusterDecoderTest, ExactFitBufferWorks) {
  ZstdClusterDecoder decoder(CountingMemory::memory());
  const auto frame = streamed();
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), dst.size()),
            static_cast<int64_t>(STREAMED_PLAIN_SIZE));
}

TEST_F(ZstdClusterDecoderTest, SmallBufferIsReportedAsTooSmallNotAsError) {
  ZstdClusterDecoder decoder(CountingMemory::memory());
  const auto frame = streamed();
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE - 1);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), dst.size()), DECODE_TOO_SMALL);
  // Also when the output is far too small, and when there is no room at all.
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), 16), DECODE_TOO_SMALL);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), 0), DECODE_TOO_SMALL);
}

TEST_F(ZstdClusterDecoderTest, DeclaredSizeIsCheckedBeforeDecoding) {
  ZstdClusterDecoder decoder(CountingMemory::memory());
  const auto frame = known();
  std::vector<uint8_t> dst(KNOWN_PLAIN_SIZE);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), KNOWN_PLAIN_SIZE - 1),
            DECODE_TOO_SMALL);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), KNOWN_PLAIN_SIZE),
            static_cast<int64_t>(KNOWN_PLAIN_SIZE));
}

TEST_F(ZstdClusterDecoderTest, StopsAtTheEndOfTheFrame) {
  // The last cluster of a ZIM file has no stored end: bytes after the frame must be ignored.
  ZstdClusterDecoder decoder(CountingMemory::memory());
  auto frame = streamed();
  const size_t frameSize = frame.size();
  for (int i = 0; i < 4096; ++i) frame.push_back(static_cast<uint8_t>(0xA5 ^ i));
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE + 64);
  ASSERT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), dst.size()),
            static_cast<int64_t>(STREAMED_PLAIN_SIZE));
  // Bytes after the frame must not change the result even when they look like a frame.
  frame.resize(frameSize);
  const auto second = known();
  frame.insert(frame.end(), second.begin(), second.end());
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), dst.size()),
            static_cast<int64_t>(STREAMED_PLAIN_SIZE));
}

TEST_F(ZstdClusterDecoderTest, ReusesOneContextAcrossCalls) {
  ZstdClusterDecoder decoder(CountingMemory::memory());
  const int afterCreate = CountingMemory::allocations;
  const auto a = streamed();
  const auto b = known();
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE);
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(decoder.decode(Compression::Zstd, a.data(), a.size(), dst.data(), dst.size()),
              static_cast<int64_t>(STREAMED_PLAIN_SIZE));
    ASSERT_EQ(decoder.decode(Compression::Zstd, b.data(), b.size(), dst.data(), dst.size()),
              static_cast<int64_t>(KNOWN_PLAIN_SIZE));
  }
  // Single-shot decoding allocates only the context, once.
  EXPECT_EQ(CountingMemory::allocations, afterCreate);
}

TEST_F(ZstdClusterDecoderTest, FailedCallDoesNotBreakTheNextOne) {
  ZstdClusterDecoder decoder(CountingMemory::memory());
  const auto frame = streamed();
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), 100), DECODE_TOO_SMALL);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), dst.size()),
            static_cast<int64_t>(STREAMED_PLAIN_SIZE));
}

TEST_F(ZstdClusterDecoderTest, RejectsBadInput) {
  ZstdClusterDecoder decoder(CountingMemory::memory());
  const auto frame = streamed();
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE);
  EXPECT_EQ(decoder.decode(Compression::None, frame.data(), frame.size(), dst.data(), dst.size()), DECODE_ERROR);
  EXPECT_EQ(decoder.decode(Compression::Xz, frame.data(), frame.size(), dst.data(), dst.size()), DECODE_ERROR);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), 0, dst.data(), dst.size()), DECODE_ERROR);
  EXPECT_EQ(decoder.decode(Compression::Zstd, nullptr, 10, dst.data(), dst.size()), DECODE_ERROR);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), nullptr, dst.size()), DECODE_ERROR);
  const uint8_t garbage[64] = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_EQ(decoder.decode(Compression::Zstd, garbage, sizeof(garbage), dst.data(), dst.size()), DECODE_ERROR);
}

TEST_F(ZstdClusterDecoderTest, TruncatedFrameIsAnError) {
  ZstdClusterDecoder decoder(CountingMemory::memory());
  const auto frame = streamed();
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE);
  for (const size_t keep : {frame.size() - 1, frame.size() / 2, size_t{8}}) {
    EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), keep, dst.data(), dst.size()), DECODE_ERROR)
        << "keep=" << keep;
  }
}

TEST_F(ZstdClusterDecoderTest, CorruptedFramesNeverCrash) {
  // Flip one byte at a time through the whole frame: the result is a decode error or, when
  // the damage is in literal data, a different but bounded output. Never a crash or overrun.
  ZstdClusterDecoder decoder(CountingMemory::memory());
  const auto frame = streamed();
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE + 64, 0xEE);
  for (size_t i = 0; i < frame.size(); i += 7) {
    auto bad = frame;
    bad[i] ^= 0x5A;
    const int64_t n = decoder.decode(Compression::Zstd, bad.data(), bad.size(), dst.data(), STREAMED_PLAIN_SIZE);
    EXPECT_LE(n, static_cast<int64_t>(STREAMED_PLAIN_SIZE));
    EXPECT_EQ(dst[STREAMED_PLAIN_SIZE], 0xEE) << "wrote past dstCap, i=" << i;
  }
}

TEST_F(ZstdClusterDecoderTest, ContextAllocationFailureLeavesAnInvalidDecoder) {
  CountingMemory::failNext = true;
  ZstdClusterDecoder decoder(CountingMemory::memory());
  EXPECT_FALSE(decoder.valid());
  const auto frame = streamed();
  std::vector<uint8_t> dst(STREAMED_PLAIN_SIZE);
  EXPECT_EQ(decoder.decode(Compression::Zstd, frame.data(), frame.size(), dst.data(), dst.size()), DECODE_ERROR);
}

TEST_F(ZstdClusterDecoderTest, MissingAllocatorIsRejected) {
  ZstdClusterDecoder decoder(ZimMemory{});
  EXPECT_FALSE(decoder.valid());
}
