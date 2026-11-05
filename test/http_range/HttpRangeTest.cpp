#include <gtest/gtest.h>

#include "network/HttpRange.h"

using httprange::clampEnd;
using httprange::Kind;
using httprange::parse;

TEST(HttpRange, ClosedRange) {
  const auto r = parse("bytes=0-79", 1000);
  EXPECT_EQ(r.kind, Kind::Partial);
  EXPECT_EQ(r.start, 0u);
  EXPECT_EQ(r.end, 79u);
}

TEST(HttpRange, EndClampedToSize) {
  const auto r = parse("bytes=900-5000", 1000);
  EXPECT_EQ(r.kind, Kind::Partial);
  EXPECT_EQ(r.start, 900u);
  EXPECT_EQ(r.end, 999u);
}

TEST(HttpRange, OpenEnded) {
  const auto r = parse("bytes=500-", 1000);
  EXPECT_EQ(r.kind, Kind::Partial);
  EXPECT_EQ(r.start, 500u);
  EXPECT_EQ(r.end, 999u);
}

TEST(HttpRange, Suffix) {
  const auto r = parse("bytes=-100", 1000);
  EXPECT_EQ(r.kind, Kind::Partial);
  EXPECT_EQ(r.start, 900u);
  EXPECT_EQ(r.end, 999u);
}

TEST(HttpRange, SuffixLargerThanFile) {
  const auto r = parse("bytes=-5000", 1000);
  EXPECT_EQ(r.kind, Kind::Partial);
  EXPECT_EQ(r.start, 0u);
  EXPECT_EQ(r.end, 999u);
}

TEST(HttpRange, LastByte) {
  const auto r = parse("bytes=999-999", 1000);
  EXPECT_EQ(r.kind, Kind::Partial);
  EXPECT_EQ(r.start, 999u);
  EXPECT_EQ(r.end, 999u);
}

TEST(HttpRange, StartPastEndIsUnsatisfiable) {
  EXPECT_EQ(parse("bytes=1000-", 1000).kind, Kind::Unsatisfiable);
  EXPECT_EQ(parse("bytes=1000-1005", 1000).kind, Kind::Unsatisfiable);
}

TEST(HttpRange, ZeroSuffixIsUnsatisfiable) { EXPECT_EQ(parse("bytes=-0", 1000).kind, Kind::Unsatisfiable); }

TEST(HttpRange, EmptyFileIsUnsatisfiable) {
  EXPECT_EQ(parse("bytes=0-", 0).kind, Kind::Unsatisfiable);
  EXPECT_EQ(parse("bytes=-1", 0).kind, Kind::Unsatisfiable);
}

TEST(HttpRange, IgnoredFormsServeFull) {
  EXPECT_EQ(parse("", 1000).kind, Kind::Full);
  EXPECT_EQ(parse("items=0-1", 1000).kind, Kind::Full);
  EXPECT_EQ(parse("bytes=0-1,5-9", 1000).kind, Kind::Full);
  EXPECT_EQ(parse("bytes=", 1000).kind, Kind::Full);
  EXPECT_EQ(parse("bytes=-", 1000).kind, Kind::Full);
  EXPECT_EQ(parse("bytes=abc-def", 1000).kind, Kind::Full);
  EXPECT_EQ(parse("bytes=9-5", 1000).kind, Kind::Full);
  EXPECT_EQ(parse("bytes=5", 1000).kind, Kind::Full);
  EXPECT_EQ(parse("bytes=-5-", 1000).kind, Kind::Full);
}

TEST(HttpRange, NumberOverflowIsIgnored) { EXPECT_EQ(parse("bytes=99999999999999999999999-", 1000).kind, Kind::Full); }

TEST(HttpRange, ToleratesWhitespace) {
  const auto r = parse("  bytes= 10 - 19 ", 1000);
  EXPECT_EQ(r.kind, Kind::Partial);
  EXPECT_EQ(r.start, 10u);
  EXPECT_EQ(r.end, 19u);
}

TEST(HttpRange, ClampEndLimitsBodyLength) {
  EXPECT_EQ(clampEnd(0, 99, 100), 99u);
  EXPECT_EQ(clampEnd(0, 100, 100), 99u);
  EXPECT_EQ(clampEnd(10, 5000, 100), 109u);
  const uint64_t big = 6ull << 30;
  const uint64_t maxLen = 0xFFFFFFFDull;
  EXPECT_EQ(clampEnd(0, big - 1, maxLen) + 1, maxLen);
  EXPECT_EQ(clampEnd(big - 100, big - 1, maxLen), big - 1);
}

TEST(HttpRange, OffsetsBeyond4GiB) {
  const uint64_t size = 6ull << 30;
  const auto r = parse("bytes=5368709120-5368709199", size);
  EXPECT_EQ(r.kind, Kind::Partial);
  EXPECT_EQ(r.start, 5368709120ull);
  EXPECT_EQ(r.end, 5368709199ull);
}
