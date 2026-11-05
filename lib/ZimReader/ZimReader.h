#pragma once

#include <ByteSource.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Read-only access to a ZIM archive (format 5.x and 6.x) through a random-access
// byte source. No decompressor is linked in: clusters are decoded by a
// caller-supplied ClusterDecoder, and the caller owns the large buffers.
namespace zim {

enum class Status : uint8_t {
  Ok,
  NotFound,
  IoError,
  Corrupt,
  Unsupported,
  BufferTooSmall,
  OutOfMemory,
};

enum class Compression : uint8_t { None, Xz, Zstd, Unknown };

enum class EntryKind : uint8_t { Content, Redirect, Other };

struct Header {
  uint16_t majorVersion = 0;
  uint16_t minorVersion = 0;
  uint32_t entryCount = 0;
  uint32_t clusterCount = 0;
  uint64_t urlPtrPos = 0;
  uint64_t titlePtrPos = 0;
  uint64_t clusterPtrPos = 0;
  uint64_t mimeListPos = 0;
  uint32_t mainPage = 0xFFFFFFFF;  // URL-order index; 0xFFFFFFFF when absent
  uint64_t checksumPos = 0;
};

// Reuse one Entry across lookups: its strings keep their capacity, so a long session
// does not allocate for every result.
struct Entry {
  EntryKind kind = EntryKind::Content;
  char ns = 0;
  uint32_t index = 0;          // position in URL order
  uint16_t mimeIndex = 0;      // Content only
  uint32_t clusterNumber = 0;  // Content only
  uint32_t blobNumber = 0;     // Content only
  uint32_t redirectIndex = 0;  // Redirect only: URL-order index of the target
  std::string url;
  std::string title;  // the URL when the file stores no title
};

struct ClusterInfo {
  Compression compression = Compression::None;
  bool extended = false;   // 8-byte blob offsets instead of 4-byte
  uint64_t offset = 0;     // file offset of the cluster's info byte
  uint64_t packedSize = 0; // bytes on disk, including the info byte
  // False for the last cluster: the archive stores no end offset for it and the
  // directory entries and pointer tables follow it, so packedSize is only an upper bound.
  bool exactExtent = true;
};

constexpr int64_t DECODE_ERROR = -1;
constexpr int64_t DECODE_TOO_SMALL = -2;

class ClusterDecoder {
 public:
  virtual ~ClusterDecoder() = default;
  // Writes the decoded cluster body to dst and returns its size (at most dstCap),
  // DECODE_TOO_SMALL when dstCap cannot hold it, or DECODE_ERROR for any other failure.
  // src can run past the end of the compressed frame (the last cluster has no known
  // end), so the decoder must stop at the end of the frame.
  virtual int64_t decode(Compression compression, const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) = 0;
};

// Caller-owned buffers for readBlob. `packed` holds the cluster as stored and is
// unused for uncompressed clusters; `plain` receives the blob or decoded cluster.
struct Workspace {
  uint8_t* packed = nullptr;
  size_t packedCap = 0;
  uint8_t* plain = nullptr;
  size_t plainCap = 0;
};

// Points into Workspace::plain and stays valid until the next readBlob call.
// When readBlob returns BufferTooSmall for an uncompressed cluster, `size` is the blob size.
struct Blob {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

class ZimReader {
 public:
  // `source` must outlive the reader. A failed open leaves the reader unusable.
  Status open(freeink::content::ByteSource& source);

  const Header& header() const { return header_; }
  uint32_t entryCount() const { return header_.entryCount; }
  std::string_view mimeType(uint16_t index) const;

  Status entryByUrlIndex(uint32_t index, Entry& out);
  // Binary search of the URL-ordered list by namespace and path.
  Status findByPath(char ns, std::string_view path, Entry& out);
  // Tries namespace 'C' (6.1+ layout) and then 'A' (older layout), then keeps using
  // whichever namespace the first hit was in.
  Status findContent(std::string_view path, Entry& out);
  // Follows redirects until a content entry; fails on a loop or a non-content target.
  // On failure `entry` holds the last entry reached, not the original.
  Status resolveRedirects(Entry& entry);

  Status entryByTitleIndex(uint32_t titleIndex, Entry& out);
  // First title-order index whose (namespace, title) is not less than (ns, prefix).
  // May equal entryCount() when every title sorts before the prefix.
  Status lowerBoundTitle(char ns, std::string_view prefix, uint32_t& titleIndex);

  Status clusterInfo(uint32_t clusterNumber, ClusterInfo& out);
  // Reads one blob. `decoder` may be null when the cluster is uncompressed.
  Status readBlob(uint32_t clusterNumber, uint32_t blobNumber, ClusterDecoder* decoder, const Workspace& workspace,
                  Blob& out);

 private:
  struct Dirent;

  static bool parseDirent(const uint8_t* buf, size_t len, Dirent& out);
  Status load(freeink::content::ByteSource& source);
  uint64_t lastClusterEnd(uint64_t start) const;
  Status readDirent(uint32_t urlIndex, Dirent& out);
  Status urlIndexForTitle(uint32_t titleIndex, uint32_t& urlIndex);
  Status readStoredBlob(const ClusterInfo& info, uint32_t blobNumber, const Workspace& workspace, Blob& out);
  Status readPackedBlob(const ClusterInfo& info, uint32_t blobNumber, ClusterDecoder* decoder,
                        const Workspace& workspace, Blob& out);

  freeink::content::ByteSource* source_ = nullptr;
  uint64_t fileSize_ = 0;
  Header header_;
  bool titleListValid_ = false;
  char contentNs_ = 0;  // namespace of the first findContent hit; 0 until then
  std::vector<std::string> mimeTypes_;
  std::unique_ptr<uint8_t[]> scratch_;  // one directory entry; reused by every lookup
};

}  // namespace zim
