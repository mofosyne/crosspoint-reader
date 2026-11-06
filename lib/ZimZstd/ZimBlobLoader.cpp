#include "ZimBlobLoader.h"

#include <Logging.h>

namespace zim {

namespace {
// Each attempt can enlarge one buffer at most once, so two retries cover both.
constexpr int MAX_RETRIES = 2;
}  // namespace

void ZimBlobLoader::release() {
  if (packed_) memory_.release(packed_);
  if (plain_) memory_.release(plain_);
  packed_ = plain_ = nullptr;
  packedCap_ = plainCap_ = 0;
}

// The old buffer is freed before the new one is allocated so both never coexist.
bool ZimBlobLoader::ensurePacked(size_t bytes) {
  if (packed_ && packedCap_ >= bytes) return true;
  if (packed_) memory_.release(packed_);
  packed_ = static_cast<uint8_t*>(memory_.allocate(bytes));
  packedCap_ = packed_ ? bytes : 0;
  return packed_ != nullptr;
}

bool ZimBlobLoader::ensurePlain(size_t bytes) {
  if (plain_ && plainCap_ >= bytes) return true;
  if (plain_) memory_.release(plain_);
  plain_ = static_cast<uint8_t*>(memory_.allocate(bytes));
  plainCap_ = plain_ ? bytes : 0;
  return plain_ != nullptr;
}

Status ZimBlobLoader::read(ZimReader& reader, ClusterDecoder* decoder, uint32_t clusterNumber, uint32_t blobNumber,
                           Blob& out) {
  if (!memory_.allocate || !memory_.release) return Status::OutOfMemory;
  if (!ensurePacked(limits_.packedBytes) || !ensurePlain(limits_.initialPlainBytes)) {
    LOG_ERR("ZIM", "Cluster buffers: out of memory");
    return Status::OutOfMemory;
  }
  for (int attempt = 0;; ++attempt) {
    const Workspace workspace{packed_, packedCap_, plain_, plainCap_};
    const Status status = reader.readBlob(clusterNumber, blobNumber, decoder, workspace, out);
    if (status != Status::BufferTooSmall || attempt == MAX_RETRIES) return status;

    // Work out which buffer was too small, and how big it has to be.
    ClusterInfo info;
    const Status infoStatus = reader.clusterInfo(clusterNumber, info);
    if (infoStatus != Status::Ok) return infoStatus;
    const bool packedTooSmall =
        info.compression != Compression::None && info.exactExtent && info.packedSize - 1 > packedCap_;
    size_t wanted;
    if (packedTooSmall) {
      wanted = static_cast<size_t>(info.packedSize - 1);
    } else if (info.compression == Compression::None) {
      wanted = out.size;  // readBlob reports the blob size for stored clusters
    } else {
      wanted = limits_.maxBufferBytes;  // decoded size is unknown: go straight to the cap
    }
    if (wanted > limits_.maxBufferBytes || wanted <= (packedTooSmall ? packedCap_ : plainCap_)) {
      LOG_ERR("ZIM", "Cluster %u needs %u bytes, cap %u", static_cast<unsigned>(clusterNumber),
              static_cast<unsigned>(wanted), static_cast<unsigned>(limits_.maxBufferBytes));
      return Status::BufferTooSmall;
    }
    if (!(packedTooSmall ? ensurePacked(wanted) : ensurePlain(wanted))) {
      LOG_ERR("ZIM", "Cluster buffer of %u bytes: out of memory", static_cast<unsigned>(wanted));
      return Status::OutOfMemory;
    }
  }
}

}  // namespace zim
