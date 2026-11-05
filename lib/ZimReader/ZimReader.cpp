#include "ZimReader.h"

#include <Logging.h>
#include <Memory.h>
#include <string.h>

#include <algorithm>

namespace zim {

namespace {

constexpr uint32_t MAGIC = 0x44D495A;
constexpr size_t HEADER_SIZE = 80;
constexpr size_t DIRENT_READ_MAX = 1024;
constexpr size_t MIME_LIST_MAX = 8192;
constexpr size_t MIME_COUNT_MAX = 256;
constexpr uint16_t MIME_REDIRECT = 0xFFFF;
constexpr uint16_t MIME_FIRST_SPECIAL = 0xFFFD;  // linktarget and deleted entries
constexpr int MAX_REDIRECTS = 8;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t le64(const uint8_t* p) { return static_cast<uint64_t>(le32(p)) | (static_cast<uint64_t>(le32(p + 4)) << 32); }

uint64_t loadOffset(const uint8_t* p, size_t offsetSize) { return offsetSize == 8 ? le64(p) : le32(p); }

bool readExact(freeink::content::ByteSource& source, uint64_t offset, void* dst, size_t len) {
  if (len == 0) return true;
  if (len > INT32_MAX) return false;
  return source.readAt(offset, dst, static_cast<uint32_t>(len)) == static_cast<int32_t>(len);
}

bool tableFits(uint64_t pos, uint64_t bytes, uint64_t fileSize) { return pos <= fileSize && bytes <= fileSize - pos; }

// Entries sort by namespace, then by the bytes of the path or title.
int compareKey(char nsA, std::string_view a, char nsB, std::string_view b) {
  if (nsA != nsB) return static_cast<uint8_t>(nsA) < static_cast<uint8_t>(nsB) ? -1 : 1;
  const int c = a.compare(b);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

Compression compressionFromInfo(uint8_t info) {
  switch (info & 0x0F) {
    case 0:
    case 1:
      return Compression::None;
    case 4:
      return Compression::Xz;
    case 5:
      return Compression::Zstd;
    default:
      return Compression::Unknown;
  }
}

}  // namespace

// The views point into scratch_ and stay valid until the next readDirent call.
struct ZimReader::Dirent {
  EntryKind kind = EntryKind::Content;
  char ns = 0;
  uint16_t mimeIndex = 0;
  uint32_t clusterNumber = 0;
  uint32_t blobNumber = 0;
  uint32_t redirectIndex = 0;
  std::string_view url;
  std::string_view title;
};

bool ZimReader::parseDirent(const uint8_t* buf, size_t len, Dirent& d) {
  if (len < 8) return false;
  const uint16_t mime = le16(buf);
  d.ns = static_cast<char>(buf[3]);
  size_t fixed = 16;
  if (mime == MIME_REDIRECT) {
    fixed = 12;
    if (len < fixed) return false;
    d.kind = EntryKind::Redirect;
    d.redirectIndex = le32(buf + 8);
  } else if (mime >= MIME_FIRST_SPECIAL) {
    fixed = 8;
    d.kind = EntryKind::Other;
  } else {
    if (len < fixed) return false;
    d.kind = EntryKind::Content;
    d.mimeIndex = mime;
    d.clusterNumber = le32(buf + 8);
    d.blobNumber = le32(buf + 12);
  }
  const char* s = reinterpret_cast<const char*>(buf + fixed);
  size_t remaining = len - fixed;
  const void* end = memchr(s, 0, remaining);
  if (!end) return false;
  size_t n = static_cast<size_t>(static_cast<const char*>(end) - s);
  d.url = std::string_view(s, n);
  s += n + 1;
  remaining -= n + 1;
  end = memchr(s, 0, remaining);
  if (!end) return false;
  n = static_cast<size_t>(static_cast<const char*>(end) - s);
  d.title = std::string_view(s, n);
  return true;
}

Status ZimReader::open(freeink::content::ByteSource& source) {
  source_ = nullptr;
  header_ = Header{};
  mimeTypes_.clear();
  scratch_.reset();
  titleListValid_ = false;
  contentNs_ = 0;
  const Status st = load(source);
  if (st == Status::Ok) {
    source_ = &source;
  } else {
    header_ = Header{};
    mimeTypes_.clear();
    scratch_.reset();
  }
  return st;
}

Status ZimReader::load(freeink::content::ByteSource& source) {
  fileSize_ = source.size();
  uint8_t h[HEADER_SIZE];
  if (!readExact(source, 0, h, HEADER_SIZE)) {
    LOG_ERR("ZIM", "Header read failed");
    return Status::IoError;
  }
  if (le32(h) != MAGIC) {
    LOG_ERR("ZIM", "Bad magic number");
    return Status::Corrupt;
  }
  header_ = Header{};
  header_.majorVersion = le16(h + 4);
  header_.minorVersion = le16(h + 6);
  if (header_.majorVersion != 5 && header_.majorVersion != 6) {
    LOG_ERR("ZIM", "Unsupported version %u.%u", static_cast<unsigned>(header_.majorVersion),
            static_cast<unsigned>(header_.minorVersion));
    return Status::Unsupported;
  }
  header_.entryCount = le32(h + 24);
  header_.clusterCount = le32(h + 28);
  header_.urlPtrPos = le64(h + 32);
  header_.titlePtrPos = le64(h + 40);
  header_.clusterPtrPos = le64(h + 48);
  header_.mimeListPos = le64(h + 56);
  header_.mainPage = le32(h + 64);
  header_.checksumPos = le64(h + 72);

  if (!tableFits(header_.urlPtrPos, 8ull * header_.entryCount, fileSize_) ||
      !tableFits(header_.clusterPtrPos, 8ull * header_.clusterCount, fileSize_) || header_.mimeListPos >= fileSize_ ||
      header_.checksumPos > fileSize_) {
    LOG_ERR("ZIM", "Header tables lie outside the file");
    return Status::Corrupt;
  }
  // Tolerated when missing so a file without the legacy title list still opens.
  titleListValid_ = tableFits(header_.titlePtrPos, 4ull * header_.entryCount, fileSize_);

  const size_t mimeBytes = static_cast<size_t>(std::min<uint64_t>(MIME_LIST_MAX, fileSize_ - header_.mimeListPos));
  auto mimeBuf = makeUniqueNoThrow<uint8_t[]>(mimeBytes);
  if (!mimeBuf) {
    LOG_ERR("ZIM", "OOM: %u byte MIME list", static_cast<unsigned>(mimeBytes));
    return Status::OutOfMemory;
  }
  if (!readExact(source, header_.mimeListPos, mimeBuf.get(), mimeBytes)) {
    LOG_ERR("ZIM", "MIME list read failed");
    return Status::IoError;
  }
  const char* p = reinterpret_cast<const char*>(mimeBuf.get());
  size_t left = mimeBytes;
  mimeTypes_.reserve(32);
  for (;;) {
    const void* nul = memchr(p, 0, left);
    if (!nul) {
      LOG_ERR("ZIM", "Unterminated MIME list");
      return Status::Corrupt;
    }
    const size_t n = static_cast<size_t>(static_cast<const char*>(nul) - p);
    if (n == 0) break;
    if (mimeTypes_.size() >= MIME_COUNT_MAX) {
      LOG_ERR("ZIM", "Too many MIME types");
      return Status::Corrupt;
    }
    mimeTypes_.emplace_back(p, n);
    p += n + 1;
    left -= n + 1;
  }

  scratch_ = makeUniqueNoThrow<uint8_t[]>(DIRENT_READ_MAX);
  if (!scratch_) {
    LOG_ERR("ZIM", "OOM: %u byte entry buffer", static_cast<unsigned>(DIRENT_READ_MAX));
    return Status::OutOfMemory;
  }
  return Status::Ok;
}

std::string_view ZimReader::mimeType(uint16_t index) const {
  return index < mimeTypes_.size() ? std::string_view(mimeTypes_[index]) : std::string_view();
}

Status ZimReader::readDirent(uint32_t urlIndex, Dirent& d) {
  if (!source_ || !scratch_ || urlIndex >= header_.entryCount) return Status::NotFound;
  uint8_t ptr[16];
  const bool hasNext = urlIndex + 1 < header_.entryCount;
  if (!readExact(*source_, header_.urlPtrPos + 8ull * urlIndex, ptr, hasNext ? 16 : 8)) return Status::IoError;
  const uint64_t pos = le64(ptr);
  if (pos >= fileSize_) {
    LOG_ERR("ZIM", "Entry %u points past the file", static_cast<unsigned>(urlIndex));
    return Status::Corrupt;
  }
  const size_t maxLen = static_cast<size_t>(std::min<uint64_t>(DIRENT_READ_MAX, fileSize_ - pos));
  // Writers lay entries out in URL order, so the next pointer bounds this entry and
  // keeps the read to about one sector. A different layout falls back to the full read.
  size_t len = maxLen;
  if (hasNext) {
    const uint64_t next = le64(ptr + 8);
    if (next > pos && next - pos < maxLen) len = static_cast<size_t>(next - pos);
  }
  if (!readExact(*source_, pos, scratch_.get(), len)) return Status::IoError;
  d = Dirent{};
  if (!parseDirent(scratch_.get(), len, d)) {
    d = Dirent{};
    if (len == maxLen || !readExact(*source_, pos, scratch_.get(), maxLen) ||
        !parseDirent(scratch_.get(), maxLen, d)) {
      LOG_ERR("ZIM", "Bad directory entry %u", static_cast<unsigned>(urlIndex));
      return Status::Corrupt;
    }
  }
  return Status::Ok;
}

static void fillEntry(Entry& e, uint32_t index, EntryKind kind, char ns, uint16_t mimeIndex, uint32_t cluster,
                      uint32_t blob, uint32_t redirect, std::string_view url, std::string_view title) {
  e.kind = kind;
  e.ns = ns;
  e.index = index;
  e.mimeIndex = mimeIndex;
  e.clusterNumber = cluster;
  e.blobNumber = blob;
  e.redirectIndex = redirect;
  e.url.assign(url);
  e.title.assign(title.empty() ? url : title);
}

Status ZimReader::entryByUrlIndex(uint32_t index, Entry& out) {
  Dirent d;
  const Status st = readDirent(index, d);
  if (st != Status::Ok) return st;
  fillEntry(out, index, d.kind, d.ns, d.mimeIndex, d.clusterNumber, d.blobNumber, d.redirectIndex, d.url, d.title);
  return Status::Ok;
}

Status ZimReader::findByPath(char ns, std::string_view path, Entry& out) {
  uint32_t lo = 0;
  uint32_t hi = header_.entryCount;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    Dirent d;
    const Status st = readDirent(mid, d);
    if (st != Status::Ok) return st;
    const int c = compareKey(d.ns, d.url, ns, path);
    if (c == 0) {
      fillEntry(out, mid, d.kind, d.ns, d.mimeIndex, d.clusterNumber, d.blobNumber, d.redirectIndex, d.url, d.title);
      return Status::Ok;
    }
    if (c < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return Status::NotFound;
}

Status ZimReader::findContent(std::string_view path, Entry& out) {
  if (contentNs_ != 0) return findByPath(contentNs_, path, out);
  for (const char ns : {'C', 'A'}) {
    const Status st = findByPath(ns, path, out);
    if (st == Status::Ok) contentNs_ = ns;
    if (st != Status::NotFound) return st;
  }
  return Status::NotFound;
}

Status ZimReader::resolveRedirects(Entry& entry) {
  for (int hops = 0; hops <= MAX_REDIRECTS; ++hops) {
    if (entry.kind == EntryKind::Content) return Status::Ok;
    if (entry.kind != EntryKind::Redirect) return Status::Unsupported;
    const uint32_t target = entry.redirectIndex;
    if (target >= header_.entryCount) {
      LOG_ERR("ZIM", "Redirect target %u is out of range", static_cast<unsigned>(target));
      return Status::Corrupt;
    }
    const Status st = entryByUrlIndex(target, entry);
    if (st != Status::Ok) return st;
  }
  LOG_ERR("ZIM", "Redirect chain too long");
  return Status::Corrupt;
}

Status ZimReader::urlIndexForTitle(uint32_t titleIndex, uint32_t& urlIndex) {
  if (!source_) return Status::NotFound;
  if (!titleListValid_) return Status::Unsupported;
  if (titleIndex >= header_.entryCount) return Status::NotFound;
  uint8_t buf[4];
  if (!readExact(*source_, header_.titlePtrPos + 4ull * titleIndex, buf, sizeof(buf))) return Status::IoError;
  urlIndex = le32(buf);
  if (urlIndex >= header_.entryCount) {
    LOG_ERR("ZIM", "Title %u points at entry %u", static_cast<unsigned>(titleIndex), static_cast<unsigned>(urlIndex));
    return Status::Corrupt;
  }
  return Status::Ok;
}

Status ZimReader::entryByTitleIndex(uint32_t titleIndex, Entry& out) {
  uint32_t urlIndex = 0;
  const Status st = urlIndexForTitle(titleIndex, urlIndex);
  return st == Status::Ok ? entryByUrlIndex(urlIndex, out) : st;
}

Status ZimReader::lowerBoundTitle(char ns, std::string_view prefix, uint32_t& titleIndex) {
  uint32_t lo = 0;
  uint32_t hi = header_.entryCount;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    uint32_t urlIndex = 0;
    Status st = urlIndexForTitle(mid, urlIndex);
    if (st != Status::Ok) return st;
    Dirent d;
    st = readDirent(urlIndex, d);
    if (st != Status::Ok) return st;
    const std::string_view title = d.title.empty() ? d.url : d.title;
    if (compareKey(d.ns, title, ns, prefix) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  titleIndex = lo;
  return Status::Ok;
}

Status ZimReader::clusterInfo(uint32_t clusterNumber, ClusterInfo& out) {
  if (!source_) return Status::NotFound;
  if (clusterNumber >= header_.clusterCount) return Status::NotFound;
  const bool hasNext = clusterNumber + 1 < header_.clusterCount;
  uint8_t buf[16];
  const size_t want = hasNext ? 16 : 8;
  if (!readExact(*source_, header_.clusterPtrPos + 8ull * clusterNumber, buf, want)) return Status::IoError;
  const uint64_t start = le64(buf);
  const uint64_t next = hasNext ? le64(buf + 8) : lastClusterEnd(start);
  if (start >= next || next > fileSize_) {
    LOG_ERR("ZIM", "Cluster %u has a bad extent", static_cast<unsigned>(clusterNumber));
    return Status::Corrupt;
  }
  uint8_t info = 0;
  if (!readExact(*source_, start, &info, 1)) return Status::IoError;
  out.compression = compressionFromInfo(info);
  out.extended = (info & 0x10) != 0;
  out.offset = start;
  out.packedSize = next - start;
  out.exactExtent = hasNext;
  return Status::Ok;
}

// Real archives write the directory entries and pointer tables after the clusters,
// so the checksum is far past the last cluster. Use the nearest structure that follows.
uint64_t ZimReader::lastClusterEnd(uint64_t start) const {
  uint64_t end = header_.checksumPos;
  const uint64_t tables[] = {header_.urlPtrPos, titleListValid_ ? header_.titlePtrPos : 0, header_.clusterPtrPos};
  for (const uint64_t pos : tables) {
    if (pos > start && pos < end) end = pos;
  }
  return end;
}

Status ZimReader::readBlob(uint32_t clusterNumber, uint32_t blobNumber, ClusterDecoder* decoder,
                           const Workspace& workspace, Blob& out) {
  out = Blob{};
  ClusterInfo info;
  const Status st = clusterInfo(clusterNumber, info);
  if (st != Status::Ok) return st;
  switch (info.compression) {
    case Compression::None:
      return readStoredBlob(info, blobNumber, workspace, out);
    case Compression::Unknown:
      LOG_ERR("ZIM", "Cluster %u: unknown compression", static_cast<unsigned>(clusterNumber));
      return Status::Unsupported;
    default:
      return readPackedBlob(info, blobNumber, decoder, workspace, out);
  }
}

// An uncompressed cluster can be huge, so read only its offset entries and the blob itself.
Status ZimReader::readStoredBlob(const ClusterInfo& info, uint32_t blobNumber, const Workspace& workspace, Blob& out) {
  const size_t offSize = info.extended ? 8 : 4;
  const uint64_t base = info.offset + 1;
  const uint64_t bodySize = info.packedSize - 1;
  uint8_t buf[16];
  if (bodySize < offSize) return Status::Corrupt;
  if (!readExact(*source_, base, buf, offSize)) {
    LOG_ERR("ZIM", "Cluster offset read failed");
    return Status::IoError;
  }
  const uint64_t tableBytes = loadOffset(buf, offSize);
  if (tableBytes < offSize || tableBytes % offSize != 0 || tableBytes > bodySize) return Status::Corrupt;
  const uint64_t blobCount = tableBytes / offSize - 1;
  if (blobNumber >= blobCount) return Status::NotFound;
  if (!readExact(*source_, base + static_cast<uint64_t>(blobNumber) * offSize, buf, 2 * offSize)) {
    LOG_ERR("ZIM", "Blob offset read failed");
    return Status::IoError;
  }
  const uint64_t start = loadOffset(buf, offSize);
  const uint64_t end = loadOffset(buf + offSize, offSize);
  if (start < tableBytes || end < start || end > bodySize) return Status::Corrupt;
  const uint64_t size = end - start;
  if (size != static_cast<size_t>(size)) {
    LOG_ERR("ZIM", "Blob is too large for this platform");
    return Status::Unsupported;
  }
  out.size = static_cast<size_t>(size);
  if (!workspace.plain || size > workspace.plainCap) return Status::BufferTooSmall;
  if (!readExact(*source_, base + start, workspace.plain, static_cast<size_t>(size))) {
    LOG_ERR("ZIM", "Blob read failed");
    return Status::IoError;
  }
  out.data = workspace.plain;
  return Status::Ok;
}

Status ZimReader::readPackedBlob(const ClusterInfo& info, uint32_t blobNumber, ClusterDecoder* decoder,
                                 const Workspace& workspace, Blob& out) {
  if (!decoder) return Status::Unsupported;
  const size_t offSize = info.extended ? 8 : 4;
  const uint64_t bodySize = info.packedSize - 1;
  if (!workspace.packed || !workspace.plain) return Status::BufferTooSmall;
  uint64_t toRead = bodySize;
  if (bodySize > workspace.packedCap) {
    // The last cluster's size is only an upper bound; the decoder stops at the frame end.
    if (info.exactExtent) return Status::BufferTooSmall;
    toRead = workspace.packedCap;
  }
  if (!readExact(*source_, info.offset + 1, workspace.packed, static_cast<size_t>(toRead))) {
    LOG_ERR("ZIM", "Cluster read failed");
    return Status::IoError;
  }
  const int64_t decoded = decoder->decode(info.compression, workspace.packed, static_cast<size_t>(toRead),
                                          workspace.plain, workspace.plainCap);
  if (decoded == DECODE_TOO_SMALL) return Status::BufferTooSmall;
  if (decoded < 0) {
    LOG_ERR("ZIM", "Cluster decode failed");
    return Status::Corrupt;
  }
  const uint64_t plainSize = static_cast<uint64_t>(decoded);
  if (plainSize > workspace.plainCap) {
    LOG_ERR("ZIM", "Decoder reported %llu bytes for a %u byte buffer", static_cast<unsigned long long>(plainSize),
            static_cast<unsigned>(workspace.plainCap));
    return Status::Corrupt;
  }
  if (plainSize < offSize) return Status::Corrupt;
  const uint64_t tableBytes = loadOffset(workspace.plain, offSize);
  if (tableBytes < offSize || tableBytes % offSize != 0 || tableBytes > plainSize) return Status::Corrupt;
  const uint64_t blobCount = tableBytes / offSize - 1;
  if (blobNumber >= blobCount) return Status::NotFound;
  const uint8_t* entry = workspace.plain + static_cast<uint64_t>(blobNumber) * offSize;
  const uint64_t start = loadOffset(entry, offSize);
  const uint64_t end = loadOffset(entry + offSize, offSize);
  if (start < tableBytes || end < start || end > plainSize) return Status::Corrupt;
  out.data = workspace.plain + start;
  out.size = static_cast<size_t>(end - start);
  return Status::Ok;
}

}  // namespace zim
